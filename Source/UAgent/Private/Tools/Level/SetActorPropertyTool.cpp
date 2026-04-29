#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
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

class FSetActorPropertyTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/set_actor_property");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Set a property on a placed actor by name. Value is the "
                "FProperty ImportText form.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"actor": { "type": "string" },
						"propertyName": { "type": "string", "description": "Property name on the actor" },
						"value": { "type": "string", "description": "ImportText form" }
					},
					"required": ["actor", "propertyName", "value"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ActorName, PropName, Value;
    Params->TryGetStringField(TEXT("actor"), ActorName);
    Params->TryGetStringField(TEXT("propertyName"), PropName);
    Params->TryGetStringField(TEXT("value"), Value);
    if (ActorName.IsEmpty() || PropName.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("actor and propertyName required"));
    }

    AActor *A = FindActorByAny(ActorName);
    if (!A)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("actor not found: %s"), *ActorName));

    FProperty *Prop = A->GetClass()->FindPropertyByName(FName(*PropName));
    if (!Prop) {
      const bool bLooksDotted = PropName.Contains(TEXT("."));
      const FString Hint =
          bLooksDotted
              ? TEXT(". Dotted paths aren't supported here — to write a "
                     "component sub-property on a placed actor, use "
                     "set_component_property with actor + componentName + "
                     "propertyName.")
              : FString();
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("property '%s' not found on %s%s"),
                                  *PropName, *A->GetClass()->GetName(), *Hint));
    }

    const FScopedTransaction Tx(
        LOCTEXT("SetActorPropTx", "Set Actor Property"));
    A->Modify();

    void *Addr = Prop->ContainerPtrToValuePtr<void>(A);
    if (!Prop->ImportText_Direct(*Value, Addr, A, PPF_None)) {
      return FToolResponse::Fail(-32000, TEXT("ImportText failed"));
    }

    FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
    A->PostEditChangeProperty(Evt);
    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateSetActorPropertyTool() {
  return MakeShared<FSetActorPropertyTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
