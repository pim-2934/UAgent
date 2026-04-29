#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "Components/ActorComponent.h"
#include "Components/MeshComponent.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"

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

class FSetComponentMaterialTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/set_component_material");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Assign a material to a mesh component's material slot. Two modes: on "
        "a Blueprint (blueprintPath + componentName → writes the SCS "
        "component_template, recompiles, and by default updates already-placed "
        "actors that were inheriting the prior material) or on a placed actor "
        "(actor + componentName → writes the live component instance, "
        "affecting only that actor). Wraps UMeshComponent::SetMaterial, so the "
        "OverrideMaterials array is sized and flagged correctly — prefer this "
        "over set_component_property for materials.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
						"type": "object",
						"properties": {
							"blueprintPath": { "type": "string", "description": "Blueprint asset path. Use with componentName to write an SCS component template." },
							"actor":         { "type": "string", "description": "Placed actor name or label. Use with componentName to write a live component instance." },
							"componentName": { "type": "string", "description": "SCS node variable name or component instance name, e.g. 'CubeMesh'. Must resolve to a UMeshComponent." },
							"slot":          { "type": "integer", "minimum": 0, "description": "Material slot index. Default 0." },
							"materialPath":  { "type": "string", "description": "Asset path of the material or material instance, e.g. '/Game/Materials/M_Green.M_Green'" },
							"saveAsset":     { "type": "boolean", "description": "Blueprint mode only: save the asset after compile. Default true." },
							"propagate":     { "type": "boolean", "description": "Blueprint mode only. Default true — leave unset for normal 'change this material' requests. When true, already-placed actors that were showing the prior material get updated to the new one (matching how a manual edit in the details panel behaves); actors with explicit per-instance overrides are left alone. Set false only when the user explicitly wants the Blueprint changed without touching actors already in levels (rare — e.g., scripted bulk edits where the caller will update placed actors separately)." }
						},
						"required": ["componentName", "materialPath"]
					})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString BPPath, ActorName, CompName, MaterialPath;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("actor"), ActorName);
    Params->TryGetStringField(TEXT("componentName"), CompName);
    Params->TryGetStringField(TEXT("materialPath"), MaterialPath);
    if (CompName.IsEmpty() || MaterialPath.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("componentName and materialPath required"));
    }
    const bool bHasBP = !BPPath.IsEmpty();
    const bool bHasActor = !ActorName.IsEmpty();
    if (bHasBP == bHasActor) {
      return FToolResponse::InvalidParams(
          TEXT("set exactly one of 'blueprintPath' or 'actor'"));
    }

    int32 Slot = 0;
    double SlotNum = 0.0;
    if (Params->TryGetNumberField(TEXT("slot"), SlotNum))
      Slot = static_cast<int32>(SlotNum);
    if (Slot < 0)
      return FToolResponse::InvalidParams(TEXT("slot must be >= 0"));

    FString LoadErr;
    UObject *MatObj = Common::LoadAssetByPath(
        MaterialPath, UMaterialInterface::StaticClass(), LoadErr);
    UMaterialInterface *Material = Cast<UMaterialInterface>(MatObj);
    if (!Material)
      return FToolResponse::Fail(-32000, LoadErr);

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
      UMeshComponent *Mesh = Cast<UMeshComponent>(Component);
      if (!Mesh)
        return FToolResponse::Fail(
            -32000, FString::Printf(
                        TEXT("component '%s' is a %s, not a UMeshComponent"),
                        *CompName, *Component->GetClass()->GetName()));

      const FScopedTransaction Tx(
          LOCTEXT("SetComponentMaterialTx", "Set Component Material"));
      A->Modify();
      Mesh->Modify();
      Mesh->SetMaterial(Slot, Material);
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

    UMeshComponent *Template = Cast<UMeshComponent>(Node->ComponentTemplate);
    if (!Template) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(
              TEXT("SCS node '%s' template is not a UMeshComponent (%s)"),
              *CompName,
              Node->ComponentTemplate
                  ? *Node->ComponentTemplate->GetClass()->GetName()
                  : TEXT("null")));
    }

    // Capture the template's OverrideMaterials array *before* mutation so
    // we can hand old/new values to the engine's propagation helper below.
    // Direct read is safe on the game thread; the "don't set directly"
    // warning on OverrideMaterials is about GC vs. render-thread races on
    // writes, which SetMaterial handles for us.
    const TArray<TObjectPtr<UMaterialInterface>> OldMaterials =
        Template->OverrideMaterials;

    const FScopedTransaction Tx(
        LOCTEXT("SetComponentMaterialTx", "Set Component Material"));
    BP->Modify();
    Node->Modify();
    Template->Modify();
    Template->SetMaterial(Slot, Material);

    const TArray<TObjectPtr<UMaterialInterface>> NewMaterials =
        Template->OverrideMaterials;

    // Propagate to placed instances. The BP recompile reinstancer preserves
    // per-instance property state, so an instance that was inheriting the
    // template's (empty) OverrideMaterials would keep an empty array after
    // compile — the new template default silently gets overwritten by the
    // instance's prior state.
    // FComponentEditorUtils::PropagateDefaultValueChange is the
    // engine-canonical fix: it walks archetype instances and only updates ones
    // whose current value still matches the old template default, leaving
    // genuine per-instance overrides alone.
    TSet<USceneComponent *> UpdatedInstances;
    if (bPropagate) {
      FProperty *OverrideMaterialsProp = FindFProperty<FProperty>(
          UMeshComponent::StaticClass(),
          GET_MEMBER_NAME_CHECKED(UMeshComponent, OverrideMaterials));
      check(OverrideMaterialsProp);
      FComponentEditorUtils::PropagateDefaultValueChange<
          TArray<TObjectPtr<UMaterialInterface>>>(
          Template, OverrideMaterialsProp, OldMaterials, NewMaterials,
          UpdatedInstances);
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("propagatedToInstances"),
                           UpdatedInstances.Num());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateSetComponentMaterialTool() {
  return MakeShared<FSetComponentMaterialTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
