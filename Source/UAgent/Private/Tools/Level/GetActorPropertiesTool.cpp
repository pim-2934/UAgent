#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/PropertyToJson.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"

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

class FGetActorPropertiesTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/get_actor_properties");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Dump a single placed actor's properties as JSON. Resolve the "
                "actor by internal name or its display label.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"actor": { "type": "string", "description": "Actor name or label" }
					},
					"required": ["actor"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ActorName;
    Params->TryGetStringField(TEXT("actor"), ActorName);
    if (ActorName.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing actor"));

    AActor *A = FindActor(ActorName);
    if (!A)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("actor not found: %s"), *ActorName));

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("name"), A->GetName());
    Root->SetStringField(TEXT("label"), A->GetActorLabel());
    Root->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
    Root->SetObjectField(TEXT("properties"),
                         Common::PropertiesToJsonObject(A->GetClass(), A));
    return FToolResponse::Ok(Root);
  }
};
} // namespace

TSharedRef<IACPTool> CreateGetActorPropertiesTool() {
  return MakeShared<FGetActorPropertiesTool>();
}
} // namespace UAgent
