#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "BlueprintQueries.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FDeleteNodeTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/delete_node");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Remove a node from a Blueprint graph. Breaks all incident "
                "links, then destroys the node. Refuses if the node reports "
                "CanUserDeleteNode() == false (e.g. root event entries).");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"graphName":     { "type": "string" },
						"nodeId":        { "type": "string", "description": "GUID of the node to delete" }
					},
					"required": ["blueprintPath", "graphName", "nodeId"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString BlueprintPath, GraphName, NodeId;
    Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath);
    Params->TryGetStringField(TEXT("graphName"), GraphName);
    Params->TryGetStringField(TEXT("nodeId"), NodeId);

    if (BlueprintPath.IsEmpty() || GraphName.IsEmpty() || NodeId.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, graphName, nodeId are required"));
    }

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BlueprintPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    UEdGraph *Graph = BlueprintAccess::FindGraph(BP, GraphName);
    if (!Graph) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("graph '%s' not found"), *GraphName));
    }

    UEdGraphNode *Node = BlueprintAccess::FindNodeById(Graph, NodeId);
    if (!Node) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("node '%s' not found"), *NodeId));
    }

    if (!Node->CanUserDeleteNode()) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("node '%s' (%s) cannot be user-deleted"),
                                  *NodeId, *Node->GetName()));
    }

    const FScopedTransaction Tx(
        LOCTEXT("DeleteNodeTx", "Delete Blueprint Node"));
    Graph->Modify();
    Node->Modify();

    // DestroyNode breaks links, fires OnRemoveNode, and removes from the graph.
    Node->DestroyNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateDeleteNodeTool() {
  return MakeShared<FDeleteNodeTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
