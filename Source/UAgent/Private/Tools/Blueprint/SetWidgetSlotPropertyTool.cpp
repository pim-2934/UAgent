#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "Components/PanelSlot.h"
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
class FSetWidgetSlotPropertyTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/set_widget_slot_property");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Set a property on the UPanelSlot of a UWidget inside a Widget "
        "Blueprint's WidgetTree. Slot subobjects are auto-created when a "
        "widget is parented (UCanvasPanelSlot, UHorizontalBoxSlot, UGridSlot, "
        "etc.) and carry layout properties — anchors, offsets, padding, "
        "alignment, fill rules. The slot class depends on the parent panel; "
        "read it from the parent panel's docs or call get_class_info on the "
        "widget's Slot class. Value is the FProperty ImportText form.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
						"type": "object",
						"properties": {
							"blueprintPath": { "type": "string", "description": "Widget Blueprint asset path" },
							"widgetName": { "type": "string", "description": "Child widget whose slot is being modified. The slot belongs to the parent panel but is addressed via the child." },
							"propertyName": { "type": "string", "description": "Property on the slot, e.g. 'LayoutData', 'Anchors', 'Offsets', 'Padding'" },
							"value": { "type": "string", "description": "ImportText form of the value (e.g. '(Anchors=(Minimum=(X=0,Y=0),Maximum=(X=1,Y=0)),Offsets=(Left=0,Top=0,Right=0,Bottom=64))' for a UCanvasPanelSlot LayoutData)" },
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

    UPanelSlot *Slot = Widget->Slot;
    if (!Slot) {
      return FToolResponse::Fail(
          -32000, FString::Printf(
                      TEXT("widget '%s' has no Slot (is it the RootWidget or "
                           "unparented?)"),
                      *WidgetName));
    }

    FProperty *Prop = Slot->GetClass()->FindPropertyByName(FName(*PropName));
    if (!Prop) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("property '%s' not found on slot %s"),
                                  *PropName, *Slot->GetClass()->GetName()));
    }

    const FScopedTransaction Tx(
        LOCTEXT("SetWidgetSlotPropTx", "Set Widget Slot Property"));
    WBP->Modify();
    Slot->Modify();

    void *Addr = Prop->ContainerPtrToValuePtr<void>(Slot);
    if (!Prop->ImportText_Direct(*Value, Addr, Slot, PPF_None)) {
      return FToolResponse::Fail(-32000, TEXT("ImportText failed"));
    }

    FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
    Slot->PostEditChangeProperty(Evt);

    FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
    FKismetEditorUtilities::CompileBlueprint(WBP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(WBP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("slotClass"), Slot->GetClass()->GetName());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateSetWidgetSlotPropertyTool() {
  return MakeShared<FSetWidgetSlotPropertyTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
