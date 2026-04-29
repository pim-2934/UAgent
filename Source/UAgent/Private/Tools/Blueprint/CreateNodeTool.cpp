#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "BlueprintNodeFactory.h"
#include "BlueprintQueries.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FCreateNodeTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_node");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Spawn a new node in a Blueprint graph. Returns the new node's GUID as "
        "`nodeId`. Marks the Blueprint modified and recompiles.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": {
							"type": "string",
							"description": "Path to the owning Blueprint, e.g. /Game/MyFolder/BP_MyActor"
						},
						"graphName": {
							"type": "string",
							"description": "Name of the target graph (ubergraph/function/macro), e.g. 'EventGraph' or 'MyFunction'"
						},
						"nodeSpec": {
							"type": "string",
							"description": "What to spawn. One of: 'function:/Script/Module.Class.FunctionName', 'variable-get:VarName', 'variable-set:VarName', 'event:ReceiveBeginPlay', 'node:/Script/Module.ClassName' (raw UK2Node subclass)."
						},
						"posX": { "type": "number", "description": "Graph X position" },
						"posY": { "type": "number", "description": "Graph Y position" }
					},
					"required": ["blueprintPath", "graphName", "nodeSpec"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString BlueprintPath, GraphName, NodeSpec;
    Params->TryGetStringField(TEXT("blueprintPath"), BlueprintPath);
    Params->TryGetStringField(TEXT("graphName"), GraphName);
    Params->TryGetStringField(TEXT("nodeSpec"), NodeSpec);
    if (BlueprintPath.IsEmpty() || GraphName.IsEmpty() || NodeSpec.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, graphName, nodeSpec are required"));
    }

    double X = 0, Y = 0;
    Params->TryGetNumberField(TEXT("posX"), X);
    Params->TryGetNumberField(TEXT("posY"), Y);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BlueprintPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    UEdGraph *Graph = BlueprintAccess::FindGraph(BP, GraphName);
    if (!Graph) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("graph '%s' not found on %s"),
                                  *GraphName, *BP->GetName()));
    }

    const FScopedTransaction Transaction(
        LOCTEXT("CreateNodeTx", "Create Blueprint Node"));
    Graph->Modify();
    BP->Modify();

    UK2Node *Node = BlueprintAccess::SpawnNodeForSpec(Graph, BP, NodeSpec, Err);
    if (!Node)
      return FToolResponse::Fail(-32000, Err);

    Node->NodePosX = static_cast<int32>(X);
    Node->NodePosY = static_cast<int32>(Y);

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateNodeTool() {
  return MakeShared<FCreateNodeTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
