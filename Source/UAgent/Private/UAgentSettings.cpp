#include "UAgentSettings.h"

#include "Misc/ConfigCacheIni.h"

UUAgentSettings::UUAgentSettings() {
  CategoryName = TEXT("Plugins");
  SectionName = TEXT("UAgent");
}

namespace UAgent {
namespace SessionModeStore {
// Stored as the agent's mode id string under EditorPerProjectUserSettings.ini
// so it persists per-user without polluting the project's shared Engine.ini.
// Agent-specific — when the saved id isn't in the current agent's advertised
// set, callers ignore it and fall back to the agent's current_mode_id.
static const TCHAR *Section = TEXT("/Script/UAgent.ChatWindow");
static const TCHAR *Key = TEXT("SessionMode");

FString Load() {
  FString Value;
  if (GConfig)
    GConfig->GetString(Section, Key, Value, GEditorPerProjectIni);
  return Value;
}

void Save(const FString &ModeId) {
  if (!GConfig)
    return;
  GConfig->SetString(Section, Key, *ModeId, GEditorPerProjectIni);
  GConfig->Flush(false, GEditorPerProjectIni);
}
} // namespace SessionModeStore

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
