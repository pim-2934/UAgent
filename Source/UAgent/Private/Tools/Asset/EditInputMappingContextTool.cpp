#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/ClassResolver.h"

#include "Editor.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UAgent {
namespace {

template <typename TBase>
TBase *MakeInstancedSubobject(UInputMappingContext *Outer,
                              const FString &ClassPath, FString &OutError) {
  UClass *Cls = Common::ResolveClass(ClassPath, OutError);
  if (!Cls)
    return nullptr;
  if (!Cls->IsChildOf(TBase::StaticClass())) {
    OutError = FString::Printf(TEXT("'%s' is not a %s subclass"), *ClassPath,
                               *TBase::StaticClass()->GetName());
    return nullptr;
  }
  if (Cls->HasAnyClassFlags(CLASS_Abstract)) {
    OutError = FString::Printf(
        TEXT("'%s' is abstract — pick a concrete subclass"), *ClassPath);
    return nullptr;
  }
  return NewObject<TBase>(Outer, Cls, NAME_None, RF_Transactional);
}

class FEditInputMappingContextTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/edit_input_mapping_context");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Edit an existing UInputMappingContext: remove mappings (by "
        "action+key, or all bindings for an action) and/or add new "
        "mappings. Each 'add' entry may declare 'triggers' and 'modifiers' "
        "as arrays of UInputTrigger / UInputModifier subclass paths "
        "(e.g. '/Script/EnhancedInput.InputTriggerHold'); those objects are "
        "constructed with default property values. Removes are applied "
        "before adds. Saves the asset.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path": { "type": "string", "description": "Existing UInputMappingContext asset path, e.g. '/Game/Input/IMC_Default' or a plugin IMC like '/ACFSource/Input/ACF_DefaultMapping'." },
						"remove": {
							"type": "array",
							"description": "Mappings to remove. Each entry removes every mapping matching action+key (UInputMappingContext::UnmapKey). Applied before adds.",
							"items": {
								"type": "object",
								"properties": {
									"action": { "type": "string", "description": "UInputAction asset path." },
									"key":    { "type": "string", "description": "FKey name, e.g. 'Gamepad_LeftShoulder'." }
								},
								"required": ["action", "key"]
							}
						},
						"removeAllForAction": {
							"type": "array",
							"description": "UInputAction asset paths whose every binding should be removed (UnmapAllKeysFromAction).",
							"items": { "type": "string" }
						},
						"add": {
							"type": "array",
							"description": "Mappings to add. Triggers/modifiers are instantiated with default property values; tune them in the asset editor afterward if needed.",
							"items": {
								"type": "object",
								"properties": {
									"action":    { "type": "string", "description": "UInputAction asset path." },
									"key":       { "type": "string", "description": "FKey name." },
									"triggers":  { "type": "array", "items": { "type": "string", "description": "UInputTrigger subclass path, e.g. '/Script/EnhancedInput.InputTriggerHold'." } },
									"modifiers": { "type": "array", "items": { "type": "string", "description": "UInputModifier subclass path, e.g. '/Script/EnhancedInput.InputModifierNegate'." } }
								},
								"required": ["action", "key"]
							}
						}
					},
					"required": ["path"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Path;
    Params->TryGetStringField(TEXT("path"), Path);
    if (Path.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("'path' is required"));

    FString LoadErr;
    UObject *Loaded = Common::LoadAssetByPath(
        Path, UInputMappingContext::StaticClass(), LoadErr);
    UInputMappingContext *IMC = Cast<UInputMappingContext>(Loaded);
    if (!IMC) {
      return FToolResponse::Fail(
          -32000, LoadErr.IsEmpty()
                      ? FString::Printf(TEXT("IMC not found at '%s'"), *Path)
                      : LoadErr);
    }

    IMC->Modify();

    TArray<TSharedPtr<FJsonValue>> Failed;
    int32 Removed = 0;
    int32 Added = 0;

    // --- removes by (action, key) -----------------------------------------
    const TArray<TSharedPtr<FJsonValue>> *Removes = nullptr;
    if (Params->TryGetArrayField(TEXT("remove"), Removes) && Removes) {
      for (const TSharedPtr<FJsonValue> &Entry : *Removes) {
        if (!Entry.IsValid() || Entry->Type != EJson::Object) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("remove"));
          F->SetStringField(TEXT("error"), TEXT("entry must be an object"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }
        const TSharedPtr<FJsonObject> &Obj = Entry->AsObject();
        FString ActionPath, KeyName;
        Obj->TryGetStringField(TEXT("action"), ActionPath);
        Obj->TryGetStringField(TEXT("key"), KeyName);
        if (ActionPath.IsEmpty() || KeyName.IsEmpty()) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("remove"));
          F->SetStringField(TEXT("error"),
                            TEXT("entry requires 'action' and 'key'"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FString ActionErr;
        UObject *ActionObj = Common::LoadAssetByPath(
            ActionPath, UInputAction::StaticClass(), ActionErr);
        UInputAction *Action = Cast<UInputAction>(ActionObj);
        if (!Action) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("remove"));
          F->SetStringField(TEXT("action"), ActionPath);
          F->SetStringField(TEXT("error"), ActionErr.IsEmpty()
                                               ? TEXT("action not found")
                                               : ActionErr);
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        const FKey Key(*KeyName);
        if (!Key.IsValid()) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("remove"));
          F->SetStringField(TEXT("key"), KeyName);
          F->SetStringField(TEXT("error"), TEXT("unknown FKey name"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        const int32 Before = IMC->GetMappings().Num();
        IMC->UnmapKey(Action, Key);
        const int32 After = IMC->GetMappings().Num();
        Removed += FMath::Max(0, Before - After);
      }
    }

    // --- removeAllForAction ----------------------------------------------
    const TArray<TSharedPtr<FJsonValue>> *RemoveAll = nullptr;
    if (Params->TryGetArrayField(TEXT("removeAllForAction"), RemoveAll) &&
        RemoveAll) {
      for (const TSharedPtr<FJsonValue> &Entry : *RemoveAll) {
        if (!Entry.IsValid() || Entry->Type != EJson::String) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("removeAllForAction"));
          F->SetStringField(TEXT("error"),
                            TEXT("entry must be a UInputAction asset path"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }
        const FString ActionPath = Entry->AsString();
        FString ActionErr;
        UObject *ActionObj = Common::LoadAssetByPath(
            ActionPath, UInputAction::StaticClass(), ActionErr);
        UInputAction *Action = Cast<UInputAction>(ActionObj);
        if (!Action) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("removeAllForAction"));
          F->SetStringField(TEXT("action"), ActionPath);
          F->SetStringField(TEXT("error"), ActionErr.IsEmpty()
                                               ? TEXT("action not found")
                                               : ActionErr);
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        const int32 Before = IMC->GetMappings().Num();
        IMC->UnmapAllKeysFromAction(Action);
        const int32 After = IMC->GetMappings().Num();
        Removed += FMath::Max(0, Before - After);
      }
    }

    // --- adds -------------------------------------------------------------
    const TArray<TSharedPtr<FJsonValue>> *Adds = nullptr;
    if (Params->TryGetArrayField(TEXT("add"), Adds) && Adds) {
      for (const TSharedPtr<FJsonValue> &Entry : *Adds) {
        if (!Entry.IsValid() || Entry->Type != EJson::Object) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("add"));
          F->SetStringField(TEXT("error"), TEXT("entry must be an object"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }
        const TSharedPtr<FJsonObject> &Obj = Entry->AsObject();
        FString ActionPath, KeyName;
        Obj->TryGetStringField(TEXT("action"), ActionPath);
        Obj->TryGetStringField(TEXT("key"), KeyName);
        if (ActionPath.IsEmpty() || KeyName.IsEmpty()) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("add"));
          F->SetStringField(TEXT("error"),
                            TEXT("entry requires 'action' and 'key'"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FString ActionErr;
        UObject *ActionObj = Common::LoadAssetByPath(
            ActionPath, UInputAction::StaticClass(), ActionErr);
        UInputAction *Action = Cast<UInputAction>(ActionObj);
        if (!Action) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("add"));
          F->SetStringField(TEXT("action"), ActionPath);
          F->SetStringField(TEXT("error"), ActionErr.IsEmpty()
                                               ? TEXT("action not found")
                                               : ActionErr);
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        const FKey Key(*KeyName);
        if (!Key.IsValid()) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("op"), TEXT("add"));
          F->SetStringField(TEXT("key"), KeyName);
          F->SetStringField(TEXT("error"), TEXT("unknown FKey name"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        // Resolve triggers/modifiers up front so we can fail the entry
        // cleanly without leaving a half-configured mapping behind.
        TArray<UInputTrigger *> Triggers;
        TArray<UInputModifier *> Modifiers;
        bool bSubobjectErr = false;

        const TArray<TSharedPtr<FJsonValue>> *Trigs = nullptr;
        if (Obj->TryGetArrayField(TEXT("triggers"), Trigs) && Trigs) {
          for (const TSharedPtr<FJsonValue> &T : *Trigs) {
            if (!T.IsValid() || T->Type != EJson::String) {
              TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
              F->SetStringField(TEXT("op"), TEXT("add"));
              F->SetStringField(TEXT("action"), ActionPath);
              F->SetStringField(TEXT("key"), KeyName);
              F->SetStringField(TEXT("error"),
                                TEXT("trigger entry must be a class path"));
              Failed.Add(MakeShared<FJsonValueObject>(F));
              bSubobjectErr = true;
              break;
            }
            FString TErr;
            UInputTrigger *Trigger =
                MakeInstancedSubobject<UInputTrigger>(IMC, T->AsString(), TErr);
            if (!Trigger) {
              TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
              F->SetStringField(TEXT("op"), TEXT("add"));
              F->SetStringField(TEXT("action"), ActionPath);
              F->SetStringField(TEXT("key"), KeyName);
              F->SetStringField(TEXT("trigger"), T->AsString());
              F->SetStringField(TEXT("error"), TErr);
              Failed.Add(MakeShared<FJsonValueObject>(F));
              bSubobjectErr = true;
              break;
            }
            Triggers.Add(Trigger);
          }
        }
        if (bSubobjectErr)
          continue;

        const TArray<TSharedPtr<FJsonValue>> *Mods = nullptr;
        if (Obj->TryGetArrayField(TEXT("modifiers"), Mods) && Mods) {
          for (const TSharedPtr<FJsonValue> &M : *Mods) {
            if (!M.IsValid() || M->Type != EJson::String) {
              TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
              F->SetStringField(TEXT("op"), TEXT("add"));
              F->SetStringField(TEXT("action"), ActionPath);
              F->SetStringField(TEXT("key"), KeyName);
              F->SetStringField(TEXT("error"),
                                TEXT("modifier entry must be a class path"));
              Failed.Add(MakeShared<FJsonValueObject>(F));
              bSubobjectErr = true;
              break;
            }
            FString MErr;
            UInputModifier *Modifier = MakeInstancedSubobject<UInputModifier>(
                IMC, M->AsString(), MErr);
            if (!Modifier) {
              TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
              F->SetStringField(TEXT("op"), TEXT("add"));
              F->SetStringField(TEXT("action"), ActionPath);
              F->SetStringField(TEXT("key"), KeyName);
              F->SetStringField(TEXT("modifier"), M->AsString());
              F->SetStringField(TEXT("error"), MErr);
              Failed.Add(MakeShared<FJsonValueObject>(F));
              bSubobjectErr = true;
              break;
            }
            Modifiers.Add(Modifier);
          }
        }
        if (bSubobjectErr)
          continue;

        FEnhancedActionKeyMapping &Mapping = IMC->MapKey(Action, Key);
        for (UInputTrigger *T : Triggers)
          Mapping.Triggers.Add(T);
        for (UInputModifier *M : Modifiers)
          Mapping.Modifiers.Add(M);
        ++Added;
      }
    }

    IMC->MarkPackageDirty();

    if (UEditorAssetSubsystem *AS =
            GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                    : nullptr) {
      AS->SaveLoadedAsset(IMC, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), IMC->GetPathName());
    Result->SetNumberField(TEXT("mappingsRemoved"), Removed);
    Result->SetNumberField(TEXT("mappingsAdded"), Added);
    Result->SetNumberField(TEXT("totalMappings"), IMC->GetMappings().Num());
    if (Failed.Num() > 0)
      Result->SetArrayField(TEXT("failed"), Failed);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateEditInputMappingContextTool() {
  return MakeShared<FEditInputMappingContextTool>();
}
} // namespace UAgent
