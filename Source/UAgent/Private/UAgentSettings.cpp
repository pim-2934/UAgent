#include "UAgentSettings.h"

#include "Misc/ConfigCacheIni.h"

UUAgentSettings::UUAgentSettings() {
  CategoryName = TEXT("Plugins");
  SectionName = TEXT("UAgent");
}

namespace UAgent {
namespace PermissionModeStore {
// Stored as a string under EditorPerProjectUserSettings.ini so it persists
// per-user without polluting the project's shared Engine.ini.
static const TCHAR *Section = TEXT("/Script/UAgent.ChatWindow");
static const TCHAR *Key = TEXT("PermissionMode");

static const TCHAR *ToString(EACPPermissionMode Mode) {
  switch (Mode) {
  case EACPPermissionMode::ReadOnly:
    return TEXT("ReadOnly");
  case EACPPermissionMode::Default:
    return TEXT("Default");
  case EACPPermissionMode::FullAccess:
  default:
    return TEXT("FullAccess");
  }
}

static EACPPermissionMode FromString(const FString &S) {
  if (S.Equals(TEXT("ReadOnly"), ESearchCase::IgnoreCase))
    return EACPPermissionMode::ReadOnly;
  if (S.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
    return EACPPermissionMode::Default;
  return EACPPermissionMode::FullAccess;
}

EACPPermissionMode Load() {
  FString Value;
  if (GConfig &&
      GConfig->GetString(Section, Key, Value, GEditorPerProjectIni) &&
      !Value.IsEmpty()) {
    return FromString(Value);
  }
  return EACPPermissionMode::FullAccess;
}

void Save(EACPPermissionMode Mode) {
  if (!GConfig)
    return;
  GConfig->SetString(Section, Key, ToString(Mode), GEditorPerProjectIni);
  GConfig->Flush(false, GEditorPerProjectIni);
}
} // namespace PermissionModeStore

namespace ModelStore {
static const TCHAR *Section = TEXT("/Script/UAgent.ChatWindow");
static const TCHAR *Key = TEXT("Model");

FString Load() {
  FString Value;
  if (GConfig)
    GConfig->GetString(Section, Key, Value, GEditorPerProjectIni);
  return Value;
}

void Save(const FString &Value) {
  if (!GConfig)
    return;
  GConfig->SetString(Section, Key, *Value, GEditorPerProjectIni);
  GConfig->Flush(false, GEditorPerProjectIni);
}
} // namespace ModelStore
} // namespace UAgent
