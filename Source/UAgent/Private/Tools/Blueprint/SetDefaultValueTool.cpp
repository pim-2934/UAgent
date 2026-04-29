#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FSetDefaultValueTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/set_default_value");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Set a variable's default or a CDO property on a Blueprint. "
        "target='variable' modifies FBPVariableDescription.DefaultValue; "
        "target='cdo' writes directly to the generated class default object. "
        "Value is the FProperty ImportText form (e.g. '(X=1.0,Y=2.0)' for a "
        "struct).");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"propertyName": { "type": "string" },
						"value": { "type": "string", "description": "ImportText form of the value" },
						"target": { "type": "string", "enum": ["variable", "cdo"], "description": "Default 'variable'" },
						"saveAsset": { "type": "boolean" }
					},
					"required": ["blueprintPath", "propertyName", "value"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, PropName, Value, Target;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("propertyName"), PropName);
    Params->TryGetStringField(TEXT("value"), Value);
    Params->TryGetStringField(TEXT("target"), Target);
    if (BPPath.IsEmpty() || PropName.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath and propertyName required"));
    }
    if (Target.IsEmpty())
      Target = TEXT("variable");
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    const FScopedTransaction Tx(
        LOCTEXT("SetDefaultTx", "Set Blueprint Default Value"));
    BP->Modify();

    const bool bLooksDotted = PropName.Contains(TEXT("."));
    const FString DottedHint =
        bLooksDotted
            ? TEXT(". Dotted paths aren't supported here — to write a "
                   "component sub-property on a Blueprint, use "
                   "set_component_property with blueprintPath + componentName "
                   "+ propertyName.")
            : FString();

    if (Target == TEXT("variable")) {
      const int32 Idx =
          FBlueprintEditorUtils::FindNewVariableIndex(BP, FName(*PropName));
      if (Idx == INDEX_NONE) {
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("variable '%s' not found on BP%s"),
                                    *PropName, *DottedHint));
      }
      BP->NewVariables[Idx].DefaultValue = Value;
    } else // cdo
    {
      UClass *GenClass = BP->GeneratedClass;
      if (!GenClass)
        return FToolResponse::Fail(
            -32000, TEXT("BP has no GeneratedClass — compile first"));
      UObject *CDO = GenClass->GetDefaultObject();
      FProperty *Prop = GenClass->FindPropertyByName(FName(*PropName));
      if (!Prop)
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("property '%s' not found on %s%s"), *PropName,
                            *GenClass->GetName(), *DottedHint));

      CDO->Modify();
      void *Addr = Prop->ContainerPtrToValuePtr<void>(CDO);
      const TCHAR *Result =
          Prop->ImportText_Direct(*Value, Addr, CDO, PPF_None);
      if (!Result)
        return FToolResponse::Fail(-32000, TEXT("ImportText failed"));

      FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
      CDO->PostEditChangeProperty(Evt);
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateSetDefaultValueTool() {
  return MakeShared<FSetDefaultValueTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
