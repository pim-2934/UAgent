#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/ClassResolver.h"
#include "BlueprintQueries.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FAddWidgetTool : public IACPTool {
public:
  virtual FString GetMethod() const override { return TEXT("_ue5/add_widget"); }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Add a UWidget into a Widget Blueprint's WidgetTree. Without a parent "
        "the new widget becomes the tree's RootWidget (and must be a "
        "UPanelWidget if any further children will be added); with a parent "
        "the widget is attached as a child of the named UPanelWidget and a "
        "default UPanelSlot is auto-created — write slot properties (anchors, "
        "padding, alignment) via set_widget_slot_property. Marks the new "
        "widget as a BP variable so it's addressable from the event graph. "
        "Compiles and saves by default.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
						"type": "object",
						"properties": {
							"blueprintPath": { "type": "string", "description": "Widget Blueprint asset path, e.g. /Game/UI/WBP_HUD" },
							"widgetClass": { "type": "string", "description": "UWidget subclass to construct, e.g. 'CanvasPanel', 'Border', 'Image', 'TextBlock', or a path like '/Script/UMG.CanvasPanel'" },
							"name": { "type": "string", "description": "Variable name for the new widget; must be unique within the WidgetTree" },
							"parentName": { "type": "string", "description": "Existing UPanelWidget name to attach to. Omit only when the WidgetTree has no RootWidget yet (first widget added)." },
							"isVariable": { "type": "boolean", "description": "Expose as a BP variable accessible from the event graph. Default true (matches the UMG editor's default)." },
							"saveAsset": { "type": "boolean" }
						},
						"required": ["blueprintPath", "widgetClass", "name"]
					})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, WidgetClass, Name, ParentName;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("widgetClass"), WidgetClass);
    Params->TryGetStringField(TEXT("name"), Name);
    Params->TryGetStringField(TEXT("parentName"), ParentName);
    if (BPPath.IsEmpty() || WidgetClass.IsEmpty() || Name.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, widgetClass, name required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);
    bool bIsVariable = true;
    Params->TryGetBoolField(TEXT("isVariable"), bIsVariable);

    FString Err;
    UWidgetBlueprint *WBP =
        BlueprintAccess::LoadWidgetBlueprintByPath(BPPath, Err);
    if (!WBP)
      return FToolResponse::Fail(-32000, Err);
    if (!WBP->WidgetTree) {
      return FToolResponse::Fail(-32000,
                                 TEXT("Widget Blueprint has no WidgetTree"));
    }

    UClass *WClass = Common::ResolveClass(WidgetClass, Err);
    if (!WClass)
      return FToolResponse::Fail(-32000, Err);
    if (!WClass->IsChildOf(UWidget::StaticClass())) {
      return FToolResponse::Fail(-32000,
                                 TEXT("widgetClass must derive from UWidget"));
    }
    if (WClass->HasAnyClassFlags(CLASS_Abstract)) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("widgetClass '%s' is abstract"), *WidgetClass));
    }

    // Name uniqueness — both as a BP variable and as a WidgetTree subobject.
    if (BlueprintAccess::FindWidgetInTree(WBP, Name)) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("a widget named '%s' already exists"), *Name));
    }

    const FScopedTransaction Tx(LOCTEXT("AddWidgetTx", "Add UMG Widget"));
    WBP->Modify();
    WBP->WidgetTree->Modify();

    UPanelWidget *Parent = nullptr;
    if (!ParentName.IsEmpty()) {
      UWidget *ParentWidget =
          BlueprintAccess::FindWidgetInTree(WBP, ParentName);
      if (!ParentWidget) {
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("parentName '%s' not found in WidgetTree"),
                            *ParentName));
      }
      Parent = Cast<UPanelWidget>(ParentWidget);
      if (!Parent) {
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("parent '%s' is a %s, not a UPanelWidget"),
                            *ParentName, *ParentWidget->GetClass()->GetName()));
      }
    } else if (WBP->WidgetTree->RootWidget) {
      return FToolResponse::Fail(
          -32000, TEXT("WidgetTree already has a RootWidget; pass 'parentName' "
                       "to attach as a child"));
    }

    UWidget *NewWidget =
        WBP->WidgetTree->ConstructWidget<UWidget>(WClass, FName(*Name));
    if (!NewWidget)
      return FToolResponse::Fail(-32000, TEXT("ConstructWidget returned null"));
    NewWidget->bIsVariable = bIsVariable;

    UPanelSlot *NewSlot = nullptr;
    if (Parent) {
      Parent->Modify();
      NewSlot = Parent->AddChild(NewWidget);
      if (!NewSlot) {
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("AddChild failed (parent '%s' is a %s)"),
                            *ParentName, *Parent->GetClass()->GetName()));
      }
    } else {
      WBP->WidgetTree->RootWidget = NewWidget;
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
    FKismetEditorUtilities::CompileBlueprint(WBP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(WBP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), NewWidget->GetName());
    if (NewSlot) {
      Result->SetStringField(TEXT("slotClass"), NewSlot->GetClass()->GetName());
    }
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateAddWidgetTool() {
  return MakeShared<FAddWidgetTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
