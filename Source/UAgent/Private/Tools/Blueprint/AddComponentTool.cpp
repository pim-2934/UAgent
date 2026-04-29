#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/ClassResolver.h"
#include "BlueprintQueries.h"

#include "Components/ActorComponent.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
USCS_Node *FindScsNodeByName(USimpleConstructionScript *SCS,
                             const FName &Name) {
  if (!SCS)
    return nullptr;
  for (USCS_Node *N : SCS->GetAllNodes()) {
    if (N && N->GetVariableName() == Name)
      return N;
  }
  return nullptr;
}

class FAddComponentTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/add_component");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Add a component to an Actor Blueprint's construction script. Returns "
        "the new node's variable name. Compiles and saves by default.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"componentClass": { "type": "string", "description": "UClass name or path of the component to add, e.g. 'StaticMeshComponent' or '/Script/Engine.PointLightComponent'" },
						"name": { "type": "string", "description": "Variable name for the new component" },
						"attachTo": { "type": "string", "description": "Parent component name to attach to. Omit to add at root." },
						"saveAsset": { "type": "boolean" }
					},
					"required": ["blueprintPath", "componentClass", "name"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, CompClass, Name, AttachTo;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("componentClass"), CompClass);
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("attachTo"), AttachTo);
    if (BPPath.IsEmpty() || CompClass.IsEmpty() || Name.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, componentClass, name required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);
    if (!BP->SimpleConstructionScript) {
      return FToolResponse::Fail(
          -32000,
          TEXT("Blueprint has no SimpleConstructionScript (not an Actor?)"));
    }

    UClass *CompType = Common::ResolveClass(CompClass, Err);
    if (!CompType)
      return FToolResponse::Fail(-32000, Err);
    if (!CompType->IsChildOf(UActorComponent::StaticClass())) {
      return FToolResponse::Fail(
          -32000, TEXT("componentClass must derive from UActorComponent"));
    }

    const FScopedTransaction Tx(
        LOCTEXT("AddComponentTx", "Add Blueprint Component"));
    BP->Modify();
    BP->SimpleConstructionScript->Modify();

    USCS_Node *NewNode =
        BP->SimpleConstructionScript->CreateNode(CompType, FName(*Name));
    if (!NewNode)
      return FToolResponse::Fail(-32000, TEXT("CreateNode returned null"));

    if (!AttachTo.IsEmpty()) {
      USCS_Node *Parent =
          FindScsNodeByName(BP->SimpleConstructionScript, FName(*AttachTo));
      if (!Parent)
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("attachTo '%s' not found"), *AttachTo));
      Parent->AddChildNode(NewNode);
    } else {
      BP->SimpleConstructionScript->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), NewNode->GetVariableName().ToString());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateAddComponentTool() {
  return MakeShared<FAddComponentTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
