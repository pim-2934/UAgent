#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "EdGraph/EdGraph.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FAddEventTool : public IACPTool {
public:
  virtual FString GetMethod() const override { return TEXT("_ue5/add_event"); }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Add a custom event or override a parent-class event in a Blueprint's "
        "event graph. Pass mode='custom' for a new event, or mode='override' "
        "to implement an inherited function like 'ReceiveBeginPlay'.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"name": { "type": "string", "description": "Event name (custom) or parent UFunction name (override)" },
						"mode": { "type": "string", "enum": ["custom", "override"], "description": "Default 'custom'" },
						"posX": { "type": "number" },
						"posY": { "type": "number" },
						"saveAsset": { "type": "boolean" }
					},
					"required": ["blueprintPath", "name"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, Name, Mode;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("mode"), Mode);
    if (BPPath.IsEmpty() || Name.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath and name required"));
    }
    if (Mode.IsEmpty())
      Mode = TEXT("custom");
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);
    double X = 0, Y = 0;
    Params->TryGetNumberField(TEXT("posX"), X);
    Params->TryGetNumberField(TEXT("posY"), Y);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    UEdGraph *EventGraph = FBlueprintEditorUtils::FindEventGraph(BP);
    if (!EventGraph)
      return FToolResponse::Fail(-32000, TEXT("no event graph"));

    const FScopedTransaction Tx(LOCTEXT("AddEventTx", "Add Blueprint Event"));
    BP->Modify();
    EventGraph->Modify();

    UK2Node_Event *NewNode = nullptr;

    if (Mode == TEXT("override")) {
      if (!BP->ParentClass)
        return FToolResponse::Fail(-32000, TEXT("BP has no parent class"));
      const UFunction *ParentFn =
          BP->ParentClass->FindFunctionByName(FName(*Name));
      if (!ParentFn)
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("parent function '%s' not found on %s"), *Name,
                            *BP->ParentClass->GetName()));

      NewNode = NewObject<UK2Node_Event>(EventGraph);
      NewNode->EventReference.SetExternalMember(FName(*Name), BP->ParentClass);
      NewNode->bOverrideFunction = true;
    } else {
      UK2Node_CustomEvent *Custom = NewObject<UK2Node_CustomEvent>(EventGraph);
      Custom->CustomFunctionName = FName(*Name);
      NewNode = Custom;
    }

    NewNode->NodePosX = static_cast<int32>(X);
    NewNode->NodePosY = static_cast<int32>(Y);
    EventGraph->AddNode(NewNode, /*bFromUI=*/true, /*bSelectNewNode=*/false);
    NewNode->CreateNewGuid();
    NewNode->PostPlacedNewNode();
    NewNode->AllocateDefaultPins();

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateAddEventTool() {
  return MakeShared<FAddEventTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
