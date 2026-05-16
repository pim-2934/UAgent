#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/PieActorLookup.h"
#include "../Common/PropertyToJson.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
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
    return TEXT(
        "Dump a single actor's properties as JSON. Default target is the "
        "editor world (resolve by internal name or display label). Pass "
        "pie=true to read from the running Play-In-Editor world instead — "
        "leave `actor` empty and set `controllerIndex` (default 0) to grab "
        "the live player pawn without needing its runtime name, or set "
        "`actor` to look up a named actor in the PIE world. Useful for "
        "observing runtime state (current health, ability tags, transient "
        "flags) without modifying the BP.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
						"type": "object",
						"properties": {
							"actor": { "type": "string", "description": "Actor name or label. Required for editor target; optional with pie=true (omit to use controllerIndex)." },
							"pie": { "type": "boolean", "description": "Target the active PIE world instead of the editor world. Default false." },
							"controllerIndex": { "type": "integer", "description": "PIE only — index of the local player controller whose pawn to read. Default 0. Ignored when `actor` is set.", "minimum": 0 }
						}
					})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ActorName;
    bool bPie = false;
    int32 ControllerIndex = 0;
    Params->TryGetStringField(TEXT("actor"), ActorName);
    Params->TryGetBoolField(TEXT("pie"), bPie);
    Params->TryGetNumberField(TEXT("controllerIndex"), ControllerIndex);

    AActor *A = nullptr;
    FString SourceLabel;
    if (bPie) {
      UWorld *PieWorld = Common::GetActivePIEWorld();
      if (!PieWorld) {
        return FToolResponse::Fail(
            -32000, TEXT("pie=true but no Play-In-Editor session is running"));
      }
      if (!ActorName.IsEmpty()) {
        A = Common::FindActorInWorld(PieWorld, ActorName);
        if (!A) {
          return FToolResponse::Fail(
              -32000, FString::Printf(TEXT("actor not found in PIE world: %s"),
                                      *ActorName));
        }
        SourceLabel = TEXT("pie_named");
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
        SourceLabel = TEXT("pie_player");
      }
    } else {
      if (ActorName.IsEmpty())
        return FToolResponse::InvalidParams(
            TEXT("'actor' is required when pie is false"));
      A = FindActor(ActorName);
      if (!A) {
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("actor not found: %s"), *ActorName));
      }
      SourceLabel = TEXT("editor");
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("source"), SourceLabel);
    Root->SetStringField(TEXT("name"), A->GetName());
#if WITH_EDITOR
    Root->SetStringField(TEXT("label"), A->GetActorLabel());
#endif
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
