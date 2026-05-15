#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/PieActorLookup.h"
#include "../Common/PropertyToJson.h"
#include "BlueprintQueries.h"

#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UnrealType.h"

namespace UAgent {
namespace {
AActor *FindActor(const FString &NameOrLabel) {
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

class FGetComponentPropertiesTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/get_component_properties");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Dump a component's properties as JSON. Modes: on a Blueprint "
        "(blueprintPath + componentName → reads the SCS component template, "
        "the GeneratedClass CDO for inherited C++ default subobjects, or — "
        "for components authored on an ancestor BP's SCS — the child's "
        "InheritableComponentHandler override if present, otherwise the "
        "parent's SCS template), on a placed editor-world actor (actor + "
        "componentName → reads the live instance), or on a live PIE-world "
        "actor (pie=true + componentName, optionally with actor or "
        "controllerIndex → reads the running component on the player pawn "
        "or a named PIE actor). Optional propertyNames narrows the dump. "
        "The `source` field distinguishes blueprint, blueprint_inherited, "
        "blueprint_inherited_scs, actor, pie_player, and pie_named.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string", "description": "Blueprint asset path. Use together with componentName to read a component template (SCS) or inherited C++ default subobject (CDO)." },
						"actor":         { "type": "string", "description": "Actor name or label. With pie=false, looks in the editor world; with pie=true, looks in the PIE world. Omit (with pie=true) to read the local player pawn." },
						"pie": { "type": "boolean", "description": "Target the active PIE world. Default false (editor world). Mutually exclusive with blueprintPath." },
						"controllerIndex": { "type": "integer", "description": "PIE only — index of the local player controller whose pawn to read. Default 0. Ignored when `actor` is set.", "minimum": 0 },
						"componentName": { "type": "string", "description": "Component variable name. Matches an SCS node, or an inherited C++ component declared via CreateDefaultSubobject (e.g. 'EquipmentComponent')." },
						"propertyNames": {
							"type": "array",
							"items": { "type": "string" },
							"description": "Optional whitelist — only these property names are dumped."
						}
					},
					"required": ["componentName"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, ActorName, CompName;
    bool bPie = false;
    int32 ControllerIndex = 0;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("actor"), ActorName);
    Params->TryGetStringField(TEXT("componentName"), CompName);
    Params->TryGetBoolField(TEXT("pie"), bPie);
    Params->TryGetNumberField(TEXT("controllerIndex"), ControllerIndex);
    if (CompName.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("'componentName' is required"));

    const bool bHasBP = !BPPath.IsEmpty();
    if (bHasBP && bPie) {
      return FToolResponse::InvalidParams(
          TEXT("'blueprintPath' and 'pie' are mutually exclusive"));
    }
    const bool bHasActor = !ActorName.IsEmpty();
    if (!bPie && !bHasBP && !bHasActor) {
      return FToolResponse::InvalidParams(
          TEXT("set 'blueprintPath', 'actor', or 'pie=true'"));
    }
    if (bHasBP && bHasActor) {
      return FToolResponse::InvalidParams(
          TEXT("set exactly one of 'blueprintPath' or 'actor'"));
    }

    TSet<FName> Whitelist;
    const TArray<TSharedPtr<FJsonValue>> *NameArr = nullptr;
    if (Params->TryGetArrayField(TEXT("propertyNames"), NameArr) && NameArr) {
      for (const TSharedPtr<FJsonValue> &V : *NameArr) {
        FString S;
        if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
          Whitelist.Add(FName(*S));
      }
    }

    UActorComponent *Component = nullptr;
    FString Source, OwnerName;

    if (bHasBP) {
      FString Err;
      UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
      if (!BP)
        return FToolResponse::Fail(-32000, Err);
      BlueprintAccess::FResolvedBlueprintComponent Resolved =
          BlueprintAccess::ResolveBlueprintComponent(BP, CompName);
      if (!Resolved.Component) {
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("component '%s' not found on BP (checked "
                                 "SCS, ancestor-BP SCS, and inherited C++ "
                                 "subobjects)"),
                            *CompName));
      }
      Component = Resolved.Component;
      switch (Resolved.Source) {
      case BlueprintAccess::EBlueprintComponentSource::Inherited:
        Source = TEXT("blueprint_inherited");
        break;
      case BlueprintAccess::EBlueprintComponentSource::InheritedSCS:
        Source = TEXT("blueprint_inherited_scs");
        break;
      case BlueprintAccess::EBlueprintComponentSource::SCS:
      default:
        Source = TEXT("blueprint");
        break;
      }
      OwnerName = BP->GetPathName();
    } else if (bPie) {
      UWorld *PieWorld = Common::GetActivePIEWorld();
      if (!PieWorld) {
        return FToolResponse::Fail(
            -32000, TEXT("pie=true but no Play-In-Editor session is running"));
      }
      AActor *A = nullptr;
      if (bHasActor) {
        A = Common::FindActorInWorld(PieWorld, ActorName);
        if (!A) {
          return FToolResponse::Fail(
              -32000, FString::Printf(TEXT("actor not found in PIE world: %s"),
                                      *ActorName));
        }
        Source = TEXT("pie_named");
      } else {
        APawn *Pawn = Common::GetPlayerPawn(PieWorld, ControllerIndex);
        if (!Pawn) {
          return FToolResponse::Fail(
              -32000,
              FString::Printf(TEXT("no player pawn at controllerIndex=%d "
                                   "in PIE world"),
                              ControllerIndex));
        }
        A = Pawn;
        Source = TEXT("pie_player");
      }
      Component = FindComponentOnActor(A, CompName);
      if (!Component)
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("component '%s' not found on %s"),
                                    *CompName, *A->GetName()));
      OwnerName = A->GetName();
    } else {
      AActor *A = FindActor(ActorName);
      if (!A)
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("actor not found: %s"), *ActorName));
      Component = FindComponentOnActor(A, CompName);
      if (!Component)
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("component '%s' not found on %s"),
                                    *CompName, *A->GetName()));
      Source = TEXT("actor");
      OwnerName = A->GetName();
    }

    if (!Component) {
      return FToolResponse::Fail(-32000, TEXT("component template is null"));
    }

    TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
    if (Whitelist.Num() == 0) {
      Props = Common::PropertiesToJsonObject(Component->GetClass(), Component);
    } else {
      TArray<TSharedPtr<FJsonValue>> Missing;
      for (const FName &PropName : Whitelist) {
        FProperty *Prop = Component->GetClass()->FindPropertyByName(PropName);
        if (!Prop) {
          Missing.Add(MakeShared<FJsonValueString>(PropName.ToString()));
          continue;
        }
        Props->SetField(PropName.ToString(),
                        Common::PropertyValueToJson(Prop, Component));
      }
      if (Missing.Num() > 0) {
        // Attach a sibling note so callers can see which names didn't resolve.
        Props->SetArrayField(TEXT("_missingProperties"), Missing);
      }
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("source"), Source);
    Root->SetStringField(TEXT("owner"), OwnerName);
    Root->SetStringField(TEXT("componentName"), CompName);
    Root->SetStringField(TEXT("componentClass"),
                         Component->GetClass()->GetPathName());
    Root->SetObjectField(TEXT("properties"), Props);
    return FToolResponse::Ok(Root);
  }
};
} // namespace

TSharedRef<IACPTool> CreateGetComponentPropertiesTool() {
  return MakeShared<FGetComponentPropertiesTool>();
}
} // namespace UAgent
