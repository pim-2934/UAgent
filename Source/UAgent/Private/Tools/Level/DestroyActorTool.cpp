#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorActorSubsystem.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
AActor *FindActor(UEditorActorSubsystem *Sub, const FString &NameOrLabel) {
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

class FDestroyActorTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/destroy_actor");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Destroy one or more placed actors in the edited level. "
                "Resolve each by internal name or display label. Pass 'actor' "
                "for a single target or 'actors' for a batch; failures are "
                "reported per actor and do not stop remaining destroys. The "
                "whole call is one undo transaction.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
						"type": "object",
						"properties": {
							"actor":  { "type": "string", "description": "Single actor name or label." },
							"actors": {
								"type": "array",
								"items": { "type": "string" },
								"description": "Batch of actor names or labels."
							}
						}
					})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    TArray<FString> Targets;
    FString Single;
    if (Params->TryGetStringField(TEXT("actor"), Single) && !Single.IsEmpty()) {
      Targets.Add(Single);
    }
    const TArray<TSharedPtr<FJsonValue>> *Arr = nullptr;
    if (Params->TryGetArrayField(TEXT("actors"), Arr) && Arr) {
      for (const TSharedPtr<FJsonValue> &V : *Arr) {
        FString S;
        if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
          Targets.Add(S);
      }
    }
    if (Targets.Num() == 0) {
      return FToolResponse::InvalidParams(
          TEXT("provide 'actor' or a non-empty 'actors' array"));
    }

    UEditorActorSubsystem *Sub =
        GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>()
                : nullptr;
    if (!Sub)
      return FToolResponse::Fail(-32000,
                                 TEXT("EditorActorSubsystem unavailable"));

    const FScopedTransaction Tx(LOCTEXT("DestroyActorTx", "Destroy Actors"));

    TArray<TSharedPtr<FJsonValue>> Destroyed;
    TArray<TSharedPtr<FJsonValue>> Failed;
    for (const FString &Name : Targets) {
      AActor *A = FindActor(Sub, Name);
      if (!A) {
        TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
        F->SetStringField(TEXT("actor"), Name);
        F->SetStringField(TEXT("error"), TEXT("actor not found"));
        Failed.Add(MakeShared<FJsonValueObject>(F));
        continue;
      }
      const FString ResolvedName = A->GetName();
      const FString ResolvedLabel = A->GetActorLabel();
      if (!Sub->DestroyActor(A)) {
        TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
        F->SetStringField(TEXT("actor"), Name);
        F->SetStringField(TEXT("error"), TEXT("DestroyActor returned false"));
        Failed.Add(MakeShared<FJsonValueObject>(F));
        continue;
      }
      TSharedRef<FJsonObject> D = MakeShared<FJsonObject>();
      D->SetStringField(TEXT("name"), ResolvedName);
      D->SetStringField(TEXT("label"), ResolvedLabel);
      Destroyed.Add(MakeShared<FJsonValueObject>(D));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("destroyed"), Destroyed);
    if (Failed.Num() > 0) {
      Result->SetArrayField(TEXT("failed"), Failed);
    }
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateDestroyActorTool() {
  return MakeShared<FDestroyActorTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
