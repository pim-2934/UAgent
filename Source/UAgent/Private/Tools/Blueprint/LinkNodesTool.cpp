#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "BlueprintQueries.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FLinkNodesTool : public IACPTool {
public:
  virtual FString GetMethod() const override { return TEXT("_ue5/link_nodes"); }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Wire two pins in a Blueprint graph, respecting "
        "UEdGraphSchema_K2::TryCreateConnection (type-checks, exec vs data, "
        "etc.). Node IDs are GUIDs returned by read_blueprint or create_node.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"graphName":     { "type": "string" },
						"fromNodeId":    { "type": "string", "description": "GUID of the source node" },
						"fromPin":       { "type": "string", "description": "Pin name on the source node" },
						"toNodeId":      { "type": "string", "description": "GUID of the destination node" },
						"toPin":         { "type": "string", "description": "Pin name on the destination node" }
					},
					"required": ["blueprintPath", "graphName", "fromNodeId", "fromPin", "toNodeId", "toPin"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString BlueprintPath, GraphName, FromNodeId, FromPin, ToNodeId, ToPin;
    Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath);
    Params->TryGetStringField(TEXT("graphName"), GraphName);
    Params->TryGetStringField(TEXT("fromNodeId"), FromNodeId);
    Params->TryGetStringField(TEXT("fromPin"), FromPin);
    Params->TryGetStringField(TEXT("toNodeId"), ToNodeId);
    Params->TryGetStringField(TEXT("toPin"), ToPin);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BlueprintPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    UEdGraph *Graph = BlueprintAccess::FindGraph(BP, GraphName);
    if (!Graph) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("graph '%s' not found"), *GraphName));
    }

    UEdGraphNode *A = BlueprintAccess::FindNodeById(Graph, FromNodeId);
    UEdGraphNode *B = BlueprintAccess::FindNodeById(Graph, ToNodeId);
    if (!A)
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("from node '%s' not found"), *FromNodeId));
    if (!B)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("to node '%s' not found"), *ToNodeId));

    UEdGraphPin *PA = BlueprintAccess::FindPinByName(A, FromPin);
    UEdGraphPin *PB = BlueprintAccess::FindPinByName(B, ToPin);
    if (!PA)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("from pin '%s' not found on %s"),
                                  *FromPin, *A->GetName()));
    if (!PB)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("to pin '%s' not found on %s"), *ToPin,
                                  *B->GetName()));

    const UEdGraphSchema *SchemaBase = Graph->GetSchema();
    const UEdGraphSchema_K2 *K2 = Cast<UEdGraphSchema_K2>(SchemaBase);
    const UEdGraphSchema *Schema =
        K2 ? static_cast<const UEdGraphSchema *>(K2) : SchemaBase;
    if (!Schema)
      return FToolResponse::Fail(-32000, TEXT("graph has no schema"));

    const FScopedTransaction Transaction(
        LOCTEXT("LinkNodesTx", "Link Blueprint Nodes"));
    A->Modify();
    B->Modify();

    if (!Schema->TryCreateConnection(PA, PB)) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("schema refused connection %s.%s -> %s.%s"),
                          *A->GetName(), *FromPin, *B->GetName(), *ToPin));
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateLinkNodesTool() {
  return MakeShared<FLinkNodesTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
