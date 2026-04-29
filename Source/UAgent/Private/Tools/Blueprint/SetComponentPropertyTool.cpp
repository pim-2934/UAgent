#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UnrealType.h"

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

AActor *FindActorByAny(const FString &NameOrLabel) {
  UEditorActorSubsystem *Sub =
      GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
  if (!Sub)
    return nullptr;
  for (AActor *A : Sub->GetAllLevelActors()) {
    if (!A)
      continue;
    if (A->GetName().Equals(NameOrLabel, ESearchCase::IgnoreCase))
      return A;
    if (A->GetActorLabel().Equals(NameOrLabel, ESearchCase::IgnoreCase))
      return A;
  }
  return nullptr;
}

UActorComponent *FindComponentOnActor(AActor *A, const FString &Name) {
  TArray<UActorComponent *> Comps;
  A->GetComponents(Comps);
  for (UActorComponent *C : Comps) {
    if (!C)
      continue;
    if (C->GetName().Equals(Name, ESearchCase::IgnoreCase))
      return C;
    if (C->GetFName().ToString().Equals(Name + TEXT("_GEN_VARIABLE"),
                                        ESearchCase::IgnoreCase))
      return C;
  }
  return nullptr;
}

class FSetComponentPropertyTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/set_component_property");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Set a property on a component. Two modes: on a Blueprint "
        "(blueprintPath + componentName → writes the SCS node's "
        "component_template, recompiles, and by default updates already-placed "
        "actors that were inheriting the prior value) or on a placed actor "
        "(actor + componentName → writes the live component instance on that "
        "actor, bypassing the Blueprint). Use the live-instance mode to tweak "
        "one actor without affecting siblings spawned from the same Blueprint. "
        "Value is the FProperty ImportText form.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
						"type": "object",
						"properties": {
							"blueprintPath": { "type": "string", "description": "Blueprint asset path. Use with componentName to write an SCS component template." },
							"actor":         { "type": "string", "description": "Placed actor name or label. Use with componentName to write a live component instance." },
							"componentName": { "type": "string", "description": "SCS node variable name, or component instance name, e.g. 'CubeMesh'" },
							"propertyName": { "type": "string", "description": "Property on the component, e.g. 'StaticMesh'" },
							"value": { "type": "string", "description": "ImportText form (object paths like /Engine/BasicShapes/Cube.Cube are accepted for UObject properties)" },
							"saveAsset": { "type": "boolean", "description": "Blueprint mode only: save the asset after compile. Default true." },
							"propagate": { "type": "boolean", "description": "Blueprint mode only. Default true — leave unset for normal 'change this value' requests. When true, already-placed actors that were showing the prior value get updated to the new one (matching how a manual edit in the details panel behaves); actors with explicit per-instance overrides are left alone. Set false only when the user explicitly wants the Blueprint changed without touching actors already in levels (rare — e.g., scripted bulk edits where the caller will update placed actors separately)." }
						},
						"required": ["componentName", "propertyName", "value"]
					})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, ActorName, CompName, PropName, Value;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("actor"), ActorName);
    Params->TryGetStringField(TEXT("componentName"), CompName);
    Params->TryGetStringField(TEXT("propertyName"), PropName);
    Params->TryGetStringField(TEXT("value"), Value);
    if (CompName.IsEmpty() || PropName.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("componentName and propertyName required"));
    }
    const bool bHasBP = !BPPath.IsEmpty();
    const bool bHasActor = !ActorName.IsEmpty();
    if (bHasBP == bHasActor) {
      return FToolResponse::InvalidParams(
          TEXT("set exactly one of 'blueprintPath' or 'actor'"));
    }

    if (bHasActor) {
      AActor *A = FindActorByAny(ActorName);
      if (!A)
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("actor not found: %s"), *ActorName));
      UActorComponent *Component = FindComponentOnActor(A, CompName);
      if (!Component)
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("component '%s' not found on %s"),
                                    *CompName, *A->GetName()));

      FProperty *Prop =
          Component->GetClass()->FindPropertyByName(FName(*PropName));
      if (!Prop) {
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("property '%s' not found on %s"), *PropName,
                            *Component->GetClass()->GetName()));
      }

      const FScopedTransaction Tx(
          LOCTEXT("SetComponentPropTx", "Set Component Property"));
      A->Modify();
      Component->Modify();

      void *Addr = Prop->ContainerPtrToValuePtr<void>(Component);
      if (!Prop->ImportText_Direct(*Value, Addr, Component, PPF_None)) {
        return FToolResponse::Fail(-32000, TEXT("ImportText failed"));
      }

      FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
      Component->PostEditChangeProperty(Evt);
      return FToolResponse::Ok();
    }

    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);
    bool bPropagate = true;
    Params->TryGetBoolField(TEXT("propagate"), bPropagate);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);
    if (!BP->SimpleConstructionScript) {
      return FToolResponse::Fail(
          -32000,
          TEXT("Blueprint has no SimpleConstructionScript (not an Actor?)"));
    }

    USCS_Node *Node =
        FindScsNodeByName(BP->SimpleConstructionScript, FName(*CompName));
    if (!Node) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("SCS component '%s' not found on BP "
                                       "(inherited components not supported)"),
                                  *CompName));
    }

    UActorComponent *Template = Node->ComponentTemplate;
    if (!Template) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("SCS node '%s' has no ComponentTemplate"),
                          *CompName));
    }

    FProperty *Prop =
        Template->GetClass()->FindPropertyByName(FName(*PropName));
    if (!Prop) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("property '%s' not found on %s"),
                                  *PropName, *Template->GetClass()->GetName()));
    }

    const FScopedTransaction Tx(
        LOCTEXT("SetComponentPropTx", "Set Component Property"));
    BP->Modify();
    Node->Modify();
    Template->Modify();

    void *TemplateAddr = Prop->ContainerPtrToValuePtr<void>(Template);

    // Snapshot the template's current value before mutation. We hand this to
    // the propagation loop below as the "old default" — instances whose
    // current value still matches it are ones that were inheriting, which
    // we update; instances that diverge are genuine per-instance overrides
    // and get left alone. DestroyValue + Free release any allocations held
    // by the FProperty (e.g. inner array element storage) on scope exit.
    uint8 *OldValueBuffer = static_cast<uint8 *>(
        FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment()));
    Prop->InitializeValue(OldValueBuffer);
    ON_SCOPE_EXIT {
      Prop->DestroyValue(OldValueBuffer);
      FMemory::Free(OldValueBuffer);
    };
    Prop->CopyCompleteValue(OldValueBuffer, TemplateAddr);

    if (!Prop->ImportText_Direct(*Value, TemplateAddr, Template, PPF_None)) {
      return FToolResponse::Fail(-32000, TEXT("ImportText failed"));
    }

    FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
    Template->PostEditChangeProperty(Evt);

    // Mirror FComponentEditorUtils::PropagateDefaultValueChange<T>, but
    // FProperty-generic: walk the template's archetype instances and, for any
    // instance whose current value still matches the old template default,
    // copy the new template value over. Without this the BP recompile
    // reinstancer copies per-instance state old→new and silently overwrites
    // the new template default with the instance's stale inherited value.
    // See CONTRIBUTING.md "Writing a tool that mutates a Blueprint component
    // template".
    int32 UpdatedInstances = 0;
    if (bPropagate && Template->HasAnyFlags(RF_ArchetypeObject)) {
      TArray<UObject *> ArchetypeInstances;
      Template->GetArchetypeInstances(ArchetypeInstances);
      for (UObject *Obj : ArchetypeInstances) {
        UActorComponent *Instance = Cast<UActorComponent>(Obj);
        if (!Instance)
          continue;
        void *InstanceAddr = Prop->ContainerPtrToValuePtr<void>(Instance);
        if (!Prop->Identical(InstanceAddr, OldValueBuffer, 0))
          continue;
        if (!Instance->IsCreatedByConstructionScript()) {
          Instance->SetFlags(RF_Transactional);
          Instance->Modify();
        }
        if (AActor *InstOwner = Instance->GetOwner())
          InstOwner->Modify();
        Prop->CopyCompleteValue(InstanceAddr, TemplateAddr);
        if (USceneComponent *Scene = Cast<USceneComponent>(Instance)) {
          if (Scene->IsRegistered()) {
            if (Scene->AllowReregistration())
              Scene->ReregisterComponent();
            else
              Scene->UpdateComponentToWorld();
          }
        }
        ++UpdatedInstances;
      }
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("propagatedToInstances"), UpdatedInstances);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateSetComponentPropertyTool() {
  return MakeShared<FSetComponentPropertyTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
