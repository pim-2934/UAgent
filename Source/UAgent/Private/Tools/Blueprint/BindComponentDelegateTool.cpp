#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "EdGraph/EdGraph.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FBindComponentDelegateTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/bind_component_delegate");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Bind a multicast delegate on a Blueprint's component to a new "
        "ComponentBoundEvent handler in the target graph — the equivalent of "
        "right-clicking a component's delegate pin and choosing 'Add Event → "
        "Bind Event to <Delegate>' in the editor. Resolves the component as "
        "an FObjectProperty on the BP's SkeletonGeneratedClass (so SCS, "
        "parent-BP, and inherited C++ default-subobject components all work) "
        "and the delegate as an FMulticastDelegateProperty on that "
        "component's class. If a bound event already exists for the "
        "(component, delegate) pair, returns its GUID instead of creating a "
        "duplicate. Returns the handler node's GUID as `nodeId` plus the "
        "auto-generated `customFunctionName` (e.g. `BndEvt__<BP>_<Comp>_..."
        "_OnFoo`) for downstream wiring.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
                            "type": "object",
                            "properties": {
                                "blueprintPath": {
                                    "type": "string",
                                    "description": "Path to the Blueprint that will own the binding, e.g. /Game/Folder/BP_MyActor"
                                },
                                "graphName": {
                                    "type": "string",
                                    "description": "Target graph for the handler node. Defaults to the BP's event graph when omitted."
                                },
                                "componentName": {
                                    "type": "string",
                                    "description": "Component variable name on this BP — an SCS variable, a parent-BP SCS variable, or an inherited C++ default-subobject member."
                                },
                                "delegatePropertyName": {
                                    "type": "string",
                                    "description": "Name of the multicast delegate UPROPERTY on the component's class, e.g. 'OnDamageBlocked'."
                                },
                                "handlerEventName": {
                                    "type": "string",
                                    "description": "Optional override for the handler's CustomFunctionName. When omitted, the editor-convention name (BndEvt__<BP>_<Comp>_<NodeName>_<Delegate>) is kept."
                                },
                                "posX": { "type": "number" },
                                "posY": { "type": "number" },
                                "saveAsset": { "type": "boolean", "description": "Save the Blueprint after compile. Defaults true." }
                            },
                            "required": ["blueprintPath", "componentName", "delegatePropertyName"]
                        })JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString BPPath, GraphName, ComponentName, DelegateName, HandlerName;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("graphName"), GraphName);
    Params->TryGetStringField(TEXT("componentName"), ComponentName);
    Params->TryGetStringField(TEXT("delegatePropertyName"), DelegateName);
    Params->TryGetStringField(TEXT("handlerEventName"), HandlerName);
    if (BPPath.IsEmpty() || ComponentName.IsEmpty() || DelegateName.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath, componentName, delegatePropertyName required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);
    double X = 0, Y = 0;
    Params->TryGetNumberField(TEXT("posX"), X);
    Params->TryGetNumberField(TEXT("posY"), Y);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    // SkeletonGeneratedClass exposes both SCS-authored components and
    // inherited C++ default subobjects as FObjectProperty members; walking it
    // is what the editor itself does (see SSCSEditor.cpp / BlueprintDetails
    // Customization.cpp) when binding a component delegate.
    UClass *LookupClass = BP->SkeletonGeneratedClass
                              ? BP->SkeletonGeneratedClass.Get()
                              : BP->GeneratedClass.Get();
    if (!LookupClass) {
      return FToolResponse::Fail(
          -32000, TEXT("Blueprint has no generated class — compile it first"));
    }

    FObjectProperty *ComponentProp =
        FindFProperty<FObjectProperty>(LookupClass, FName(*ComponentName));
    if (!ComponentProp) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("component variable '%s' not found on "
                                       "%s (looked up FObjectProperty on the "
                                       "skeleton class)"),
                                  *ComponentName, *LookupClass->GetName()));
    }

    UClass *ComponentClass = ComponentProp->PropertyClass;
    if (!ComponentClass) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("component variable '%s' has no PropertyClass"),
                          *ComponentName));
    }

    FMulticastDelegateProperty *DelegateProp =
        FindFProperty<FMulticastDelegateProperty>(ComponentClass,
                                                  FName(*DelegateName));
    if (!DelegateProp) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("multicast delegate '%s' not found on component "
                               "class '%s'"),
                          *DelegateName, *ComponentClass->GetName()));
    }

    UEdGraph *Graph = nullptr;
    if (!GraphName.IsEmpty()) {
      Graph = BlueprintAccess::FindGraph(BP, GraphName);
      if (!Graph) {
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("graph '%s' not found on %s"),
                                    *GraphName, *BP->GetName()));
      }
    } else {
      Graph = FBlueprintEditorUtils::FindEventGraph(BP);
      if (!Graph)
        return FToolResponse::Fail(-32000, TEXT("no event graph on Blueprint"));
    }

    // Editor convention: a single bound event per (component, delegate). If
    // one already exists, return its GUID rather than creating a duplicate —
    // see SSCSEditor.cpp:4582 and BlueprintDetailsCustomization.cpp:281.
    if (const UK2Node_ComponentBoundEvent *Existing =
            FKismetEditorUtilities::FindBoundEventForComponent(
                BP, DelegateProp->GetFName(), ComponentProp->GetFName())) {
      TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
      Result->SetStringField(TEXT("nodeId"), Existing->NodeGuid.ToString());
      Result->SetStringField(TEXT("customFunctionName"),
                             Existing->CustomFunctionName.ToString());
      Result->SetBoolField(TEXT("alreadyBound"), true);
      return FToolResponse::Ok(Result);
    }

    const FScopedTransaction Tx(
        LOCTEXT("BindComponentDelegateTx", "Bind Component Delegate"));
    BP->Modify();
    Graph->Modify();

    UK2Node_ComponentBoundEvent *Node =
        NewObject<UK2Node_ComponentBoundEvent>(Graph);
    Node->NodePosX = static_cast<int32>(X);
    Node->NodePosY = static_cast<int32>(Y);
    Graph->AddNode(Node, /*bFromUI=*/true, /*bSelectNewNode=*/false);
    Node->CreateNewGuid();
    Node->PostPlacedNewNode();
    Node->AllocateDefaultPins();
    Node->InitializeComponentBoundEventParams(ComponentProp, DelegateProp);
    if (!HandlerName.IsEmpty())
      Node->CustomFunctionName = FName(*HandlerName);
    Node->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
    Result->SetStringField(TEXT("customFunctionName"),
                           Node->CustomFunctionName.ToString());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateBindComponentDelegateTool() {
  return MakeShared<FBindComponentDelegateTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
