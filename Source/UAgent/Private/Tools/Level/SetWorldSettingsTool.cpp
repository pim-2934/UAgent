#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/ClassResolver.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
AWorldSettings *GetCurrentWorldSettings() {
  if (!GEditor)
    return nullptr;
  UWorld *World = GEditor->GetEditorWorldContext().World();
  return World ? World->GetWorldSettings() : nullptr;
}

class FSetWorldSettingsTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/set_world_settings");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Set fields on the currently edited level's AWorldSettings "
                "actor. 'gameMode' is a convenience that resolves a class path "
                "(BP or C++) and writes DefaultGameMode. 'properties' is a "
                "name -> ImportText map for anything else (KillZ, "
                "bEnableWorldBoundsChecks, TimeDilation, …).");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"gameMode": { "type": "string", "description": "Class path or short name of a GameModeBase subclass. Sets DefaultGameMode." },
						"properties": {
							"type": "object",
							"description": "Map of propertyName -> ImportText string, applied to AWorldSettings.",
							"additionalProperties": { "type": "string" }
						}
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString GameMode;
    Params->TryGetStringField(TEXT("gameMode"), GameMode);

    const TSharedPtr<FJsonObject> *PropMap = nullptr;
    const bool bHasProps =
        Params->TryGetObjectField(TEXT("properties"), PropMap) && PropMap &&
        PropMap->IsValid() && (*PropMap)->Values.Num() > 0;

    if (GameMode.IsEmpty() && !bHasProps) {
      return FToolResponse::InvalidParams(
          TEXT("must set at least one of 'gameMode' or 'properties'"));
    }

    AWorldSettings *WS = GetCurrentWorldSettings();
    if (!WS)
      return FToolResponse::Fail(-32000,
                                 TEXT("no WorldSettings — is a level loaded?"));

    const FScopedTransaction Tx(
        LOCTEXT("SetWorldSettingsTx", "Set World Settings"));
    WS->Modify();

    if (!GameMode.IsEmpty()) {
      FString Err;
      UClass *GMClass = Common::ResolveClass(GameMode, Err);
      if (!GMClass)
        return FToolResponse::Fail(-32000, Err);
      if (!GMClass->IsChildOf(AGameModeBase::StaticClass())) {
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("%s is not a GameModeBase subclass"),
                                    *GMClass->GetName()));
      }

      FProperty *DGM = AWorldSettings::StaticClass()->FindPropertyByName(
          TEXT("DefaultGameMode"));
      if (!DGM)
        return FToolResponse::Fail(
            -32000, TEXT("DefaultGameMode property missing on AWorldSettings"));

      WS->DefaultGameMode = GMClass;
      FPropertyChangedEvent Evt(DGM, EPropertyChangeType::ValueSet);
      WS->PostEditChangeProperty(Evt);
    }

    TArray<TSharedPtr<FJsonValue>> Failed;
    if (bHasProps) {
      for (const auto &Pair : (*PropMap)->Values) {
        const FString &PropName = Pair.Key;
        FString PropValue;
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(PropValue)) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"),
                            TEXT("value must be a string (ImportText form)"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FProperty *Prop = WS->GetClass()->FindPropertyByName(FName(*PropName));
        if (!Prop) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"),
                            FString::Printf(TEXT("not found on %s"),
                                            *WS->GetClass()->GetName()));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        void *Addr = Prop->ContainerPtrToValuePtr<void>(WS);
        if (!Prop->ImportText_Direct(*PropValue, Addr, WS, PPF_None)) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"), TEXT("ImportText failed"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
        WS->PostEditChangeProperty(Evt);
      }
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("worldSettings"), WS->GetPathName());
    if (Failed.Num() > 0)
      Result->SetArrayField(TEXT("failedProperties"), Failed);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateSetWorldSettingsTool() {
  return MakeShared<FSetWorldSettingsTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
