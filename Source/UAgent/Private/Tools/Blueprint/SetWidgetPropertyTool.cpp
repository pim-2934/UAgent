#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "Components/Widget.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FSetWidgetPropertyTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/set_widget_property");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Set a property on a UWidget inside a Widget Blueprint's WidgetTree "
        "(e.g. 'Background' on a Border, 'ColorAndOpacity' on an Image, 'Text' "
        "on a TextBlock). Looks up the widget by name in the tree, then writes "
        "the property via FProperty::ImportText. Value is the FProperty "
        "ImportText form. For slot properties (anchors, padding, alignment) "
        "use set_widget_slot_property instead.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
						"type": "object",
						"properties": {
							"blueprintPath": { "type": "string", "description": "Widget Blueprint asset path" },
							"widgetName": { "type": "string", "description": "Widget variable name as it appears in the WidgetTree (e.g. 'TopBar', 'BlackFill')" },
							"propertyName": { "type": "string", "description": "Property on the widget, e.g. 'Background' or 'ColorAndOpacity'" },
							"value": { "type": "string", "description": "ImportText form of the value (e.g. '(TintColor=(SpecifiedColor=(R=0,G=0,B=0,A=1)))' for an FSlateBrush, '(R=1,G=0,B=0,A=1)' for an FLinearColor)" },
							"saveAsset": { "type": "boolean" }
						},
						"required": ["blueprintPath", "widgetName", "propertyName", "value"]
					})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, WidgetName, PropName, Value;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("widgetName"), WidgetName);
    Params->TryGetStringField(TEXT("propertyName"), PropName);
    Params->TryGetStringField(TEXT("value"), Value);
    if (BPPath.IsEmpty() || WidgetName.IsEmpty() || PropName.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, widgetName, propertyName required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    FString Err;
    UWidgetBlueprint *WBP =
        BlueprintAccess::LoadWidgetBlueprintByPath(BPPath, Err);
    if (!WBP)
      return FToolResponse::Fail(-32000, Err);

    UWidget *Widget = BlueprintAccess::FindWidgetInTree(WBP, WidgetName);
    if (!Widget) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("widget '%s' not found in WidgetTree"),
                                  *WidgetName));
    }

    FProperty *Prop = Widget->GetClass()->FindPropertyByName(FName(*PropName));
    if (!Prop) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("property '%s' not found on %s"),
                                  *PropName, *Widget->GetClass()->GetName()));
    }

    const FScopedTransaction Tx(
        LOCTEXT("SetWidgetPropTx", "Set Widget Property"));
    WBP->Modify();
    Widget->Modify();

    void *Addr = Prop->ContainerPtrToValuePtr<void>(Widget);
    if (!Prop->ImportText_Direct(*Value, Addr, Widget, PPF_None)) {
      return FToolResponse::Fail(-32000, TEXT("ImportText failed"));
    }

    FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
    Widget->PostEditChangeProperty(Evt);

    FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
    FKismetEditorUtilities::CompileBlueprint(WBP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(WBP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateSetWidgetPropertyTool() {
  return MakeShared<FSetWidgetPropertyTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
