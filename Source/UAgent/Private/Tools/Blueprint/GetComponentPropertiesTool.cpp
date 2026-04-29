#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/PropertyToJson.h"
#include "BlueprintQueries.h"

#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UnrealType.h"

namespace UAgent {
namespace {
USCS_Node *FindScsNode(USimpleConstructionScript *SCS, const FName &Name) {
  if (!SCS)
    return nullptr;
  for (USCS_Node *N : SCS->GetAllNodes()) {
    if (N && N->GetVariableName() == Name)
      return N;
  }
  return nullptr;
}

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
        "Dump a component's properties as JSON. Two modes: on a Blueprint "
        "(blueprintPath + componentName → reads the SCS node's "
        "component_template) or on a placed actor (actor + componentName → "
        "reads the live instance). Optional propertyNames narrows the dump.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string", "description": "Blueprint asset path. Use together with componentName to read an SCS component template." },
						"actor":         { "type": "string", "description": "Placed actor name or label. Use together with componentName to read a live component instance." },
						"componentName": { "type": "string", "description": "Component variable name (SCS node name, or component instance name)." },
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
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("actor"), ActorName);
    Params->TryGetStringField(TEXT("componentName"), CompName);
    if (CompName.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("'componentName' is required"));

    const bool bHasBP = !BPPath.IsEmpty();
    const bool bHasActor = !ActorName.IsEmpty();
    if (bHasBP == bHasActor) {
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
      if (!BP->SimpleConstructionScript) {
        return FToolResponse::Fail(
            -32000,
            TEXT("Blueprint has no SimpleConstructionScript (not an Actor?)"));
      }
      USCS_Node *Node =
          FindScsNode(BP->SimpleConstructionScript, FName(*CompName));
      if (!Node) {
        return FToolResponse::Fail(
            -32000,
            FString::Printf(TEXT("SCS component '%s' not found on BP "
                                 "(inherited components not supported)"),
                            *CompName));
      }
      Component = Node->ComponentTemplate;
      Source = TEXT("blueprint");
      OwnerName = BP->GetPathName();
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
