#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "BlueprintQueries.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FUnlinkNodesTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/unlink_nodes");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Break the connection between two specific pins in a Blueprint "
                "graph. Mirror of link_nodes. If toNodeId/toPin are omitted, "
                "breaks all links on fromPin.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"graphName":     { "type": "string" },
						"fromNodeId":    { "type": "string" },
						"fromPin":       { "type": "string" },
						"toNodeId":      { "type": "string", "description": "Optional. Omit together with toPin to break all links on fromPin." },
						"toPin":         { "type": "string", "description": "Optional. Omit together with toNodeId to break all links on fromPin." }
					},
					"required": ["blueprintPath", "graphName", "fromNodeId", "fromPin"]
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

    if (BlueprintPath.IsEmpty() || GraphName.IsEmpty() ||
        FromNodeId.IsEmpty() || FromPin.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, graphName, fromNodeId, fromPin are required"));
    }

    const bool bTargetSpecified = !ToNodeId.IsEmpty() || !ToPin.IsEmpty();
    if (bTargetSpecified && (ToNodeId.IsEmpty() || ToPin.IsEmpty())) {
      return FToolResponse::InvalidParams(
          TEXT("toNodeId and toPin must be set together (or both omitted)"));
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

    UEdGraphNode *A = BlueprintAccess::FindNodeById(Graph, FromNodeId);
    if (!A)
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("from node '%s' not found"), *FromNodeId));

    UEdGraphPin *PA = BlueprintAccess::FindPinByName(A, FromPin);
    if (!PA)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("from pin '%s' not found on %s"),
                                  *FromPin, *A->GetName()));

    const UEdGraphSchema *Schema = Graph->GetSchema();
    if (!Schema)
      return FToolResponse::Fail(-32000, TEXT("graph has no schema"));

    const FScopedTransaction Tx(
        LOCTEXT("UnlinkNodesTx", "Unlink Blueprint Nodes"));
    A->Modify();

    int32 BrokenCount = 0;
    if (bTargetSpecified) {
      UEdGraphNode *B = BlueprintAccess::FindNodeById(Graph, ToNodeId);
      if (!B)
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("to node '%s' not found"), *ToNodeId));
      UEdGraphPin *PB = BlueprintAccess::FindPinByName(B, ToPin);
      if (!PB)
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("to pin '%s' not found on %s"), *ToPin,
                                    *B->GetName()));

      if (!PA->LinkedTo.Contains(PB)) {
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("pins are not connected: %s.%s <-> %s.%s"),
                            *A->GetName(), *FromPin, *B->GetName(), *ToPin));
      }

      B->Modify();
      Schema->BreakSinglePinLink(PA, PB);
      BrokenCount = 1;
    } else {
      BrokenCount = PA->LinkedTo.Num();
      if (BrokenCount > 0) {
        Schema->BreakPinLinks(*PA, /*bSendsNodeNotifcation=*/true);
      }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("broken"), BrokenCount);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateUnlinkNodesTool() {
  return MakeShared<FUnlinkNodesTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
