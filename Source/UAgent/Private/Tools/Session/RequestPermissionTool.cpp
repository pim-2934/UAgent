#include "../../Protocol/ACPToolRegistry.h"
#include "../../Protocol/ACPTypes.h"
#include "../BuiltinTools.h"
#include "PermissionBroker.h"
#include "UAgent.h"
#include "UAgentSettings.h"

#include "Modules/ModuleManager.h"

namespace UAgent {
namespace {
// ACP PermissionOption.kind values that grant access (we prefer one-shot).
static FString FindOptionIdByKind(const TArray<TSharedPtr<FJsonValue>> &Options,
                                  const TCHAR *WantedKind) {
  for (const TSharedPtr<FJsonValue> &Val : Options) {
    if (!Val.IsValid())
      continue;
    const TSharedPtr<FJsonObject> *OptObj = nullptr;
    if (!Val->TryGetObject(OptObj) || !OptObj || !OptObj->IsValid())
      continue;
    FString Kind, Id;
    if ((*OptObj)->TryGetStringField(TEXT("kind"), Kind) &&
        (*OptObj)->TryGetStringField(TEXT("optionId"), Id) &&
        Kind.Equals(WantedKind)) {
      return Id;
    }
  }
  return FString();
}

static FString PickAllowId(const TArray<TSharedPtr<FJsonValue>> &Options) {
  FString Id = FindOptionIdByKind(Options, TEXT("allow_once"));
  if (Id.IsEmpty())
    Id = FindOptionIdByKind(Options, TEXT("allow_always"));
  return Id;
}

static FString PickRejectId(const TArray<TSharedPtr<FJsonValue>> &Options) {
  FString Id = FindOptionIdByKind(Options, TEXT("reject_once"));
  if (Id.IsEmpty())
    Id = FindOptionIdByKind(Options, TEXT("reject_always"));
  return Id;
}

// ACP tool kinds that don't create or mutate data.
static bool IsReadOnlyToolKind(const FString &Kind) {
  return Kind == TEXT("read") || Kind == TEXT("search") ||
         Kind == TEXT("think") || Kind == TEXT("fetch");
}

// claude-agent-acp tags every MCP tool with kind="other" (its
// toolInfoFromToolUse only classifies Claude's built-in tools), so kind alone
// can't tell us whether e.g. list_assets is read-only. The toolCall.title for
// MCP tools is "mcp__<server>__<name>" though, which we can parse back into
// our registry method to ask the tool itself via IsReadOnly().
static FString ExtractMcpToolName(const FString &Title) {
  static const FString Prefix = TEXT("mcp__");
  if (!Title.StartsWith(Prefix))
    return FString();
  const int32 Sep = Title.Find(TEXT("__"), ESearchCase::CaseSensitive,
                               ESearchDir::FromStart, Prefix.Len());
  if (Sep == INDEX_NONE)
    return FString();
  return Title.Mid(Sep + 2);
}

// Look up the MCP tool by name in the registry and ask it whether it mutates.
// Returns false (= "treat as mutating") when the lookup fails, biasing toward
// prompting on unknown tools rather than auto-allowing.
static bool IsRegistryToolReadOnly(const FString &McpToolName) {
  if (McpToolName.IsEmpty())
    return false;
  FUAgentModule *Module =
      FModuleManager::GetModulePtr<FUAgentModule>(TEXT("UAgent"));
  if (!Module)
    return false;
  const TSharedPtr<FACPToolRegistry> Reg = Module->GetToolRegistry();
  if (!Reg.IsValid())
    return false;
  const TSharedPtr<IACPTool> Tool =
      Reg->Find(FString(TEXT("_ue5/")) + McpToolName);
  return Tool.IsValid() && Tool->IsReadOnly();
}

static FToolResponse MakeSelectedOutcome(const FString &OptionId) {
  TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
  TSharedRef<FJsonObject> Outcome = MakeShared<FJsonObject>();
  Outcome->SetStringField(TEXT("outcome"), TEXT("selected"));
  Outcome->SetStringField(TEXT("optionId"), OptionId);
  Result->SetObjectField(TEXT("outcome"), Outcome);
  return FToolResponse::Ok(Result);
}

static FToolResponse MakeCancelledOutcome() {
  TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
  TSharedRef<FJsonObject> Outcome = MakeShared<FJsonObject>();
  Outcome->SetStringField(TEXT("outcome"), TEXT("cancelled"));
  Result->SetObjectField(TEXT("outcome"), Outcome);
  return FToolResponse::Ok(Result);
}

// Echoes the first available optionId; used as a structurally-valid fallback
// when the agent doesn't offer the kind we wanted. Returns an empty string if
// the array is malformed.
static FString
PickFirstOptionId(const TArray<TSharedPtr<FJsonValue>> &Options) {
  if (Options.Num() == 0)
    return FString();
  const TSharedPtr<FJsonObject> *First = nullptr;
  if (!Options[0].IsValid() || !Options[0]->TryGetObject(First) || !First ||
      !First->IsValid()) {
    return FString();
  }
  FString Id;
  (*First)->TryGetStringField(TEXT("optionId"), Id);
  return Id;
}

// Build a Selected outcome for ChosenId, falling back to the first option if
// ChosenId is empty (the agent didn't offer the kind we picked). Returns
// Cancelled only when the options array is unusable.
static FToolResponse
ResolveOutcome(FString ChosenId,
               const TArray<TSharedPtr<FJsonValue>> &Options) {
  if (ChosenId.IsEmpty()) {
    ChosenId = PickFirstOptionId(Options);
    if (ChosenId.IsEmpty()) {
      UE_LOG(LogUAgent, Warning,
             TEXT("request_permission: no usable option found — returning "
                  "cancelled"));
      return MakeCancelledOutcome();
    }
  }
  return MakeSelectedOutcome(MoveTemp(ChosenId));
}

/**
 * Routes session/request_permission through the chat window's mode dropdown:
 *   FullAccess — always allow
 *   ReadOnly   — allow only non-mutating tool kinds (read/search/think/fetch)
 *   Default    — auto-allow read-only kinds; for everything else, ask the
 *                user via the chat window's permission card and defer the
 *                response until they click Accept or Cancel
 *
 * The returned optionId MUST be one the agent actually offered in options[];
 * echoing a hardcoded value causes the agent to treat the response as refused.
 */
class FRequestPermissionTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("session/request_permission");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual void ExecuteAsync(const TSharedPtr<FJsonObject> &Params,
                            FToolResponseCallback Complete) override {
    if (!Params.IsValid()) {
      Complete(FToolResponse::InvalidParams(
          TEXT("request_permission: missing params")));
      return;
    }

    const TArray<TSharedPtr<FJsonValue>> *OptionsPtr = nullptr;
    if (!Params->TryGetArrayField(TEXT("options"), OptionsPtr) || !OptionsPtr ||
        OptionsPtr->Num() == 0) {
      Complete(FToolResponse::InvalidParams(
          TEXT("request_permission: missing or empty 'options'")));
      return;
    }
    const TArray<TSharedPtr<FJsonValue>> Options = *OptionsPtr;

    FString ToolKind;
    FString ToolTitle;
    TSharedPtr<FJsonObject> ToolCallCopy;
    const TSharedPtr<FJsonObject> *ToolCallObj = nullptr;
    if (Params->TryGetObjectField(TEXT("toolCall"), ToolCallObj) &&
        ToolCallObj && ToolCallObj->IsValid()) {
      (*ToolCallObj)->TryGetStringField(TEXT("kind"), ToolKind);
      (*ToolCallObj)->TryGetStringField(TEXT("title"), ToolTitle);
      ToolCallCopy = *ToolCallObj;
    }

    // Combined read-only check: trust kind when the agent classified it
    // (Claude's built-ins, future MCP clients that respect annotations) and
    // fall back to asking the actual tool via IACPTool::IsReadOnly() — which
    // we can do because toolCall.title is "mcp__<server>__<name>" and we
    // own the registry. Defense in depth: if a future agent classifies kind
    // correctly we still respect that without changing every tool.
    const FString McpName = ExtractMcpToolName(ToolTitle);
    const bool bIsReadOnly =
        IsReadOnlyToolKind(ToolKind) || IsRegistryToolReadOnly(McpName);

    const EACPPermissionMode Mode = PermissionModeStore::Load();

    switch (Mode) {
    case EACPPermissionMode::ReadOnly: {
      const FString ChosenId =
          bIsReadOnly ? PickAllowId(Options) : PickRejectId(Options);
      UE_LOG(LogUAgent, Verbose,
             TEXT("request_permission: kind='%s' name='%s' ReadOnly→%s "
                  "optionId='%s'"),
             *ToolKind, *McpName, bIsReadOnly ? TEXT("allow") : TEXT("reject"),
             *ChosenId);
      Complete(ResolveOutcome(ChosenId, Options));
      return;
    }

    case EACPPermissionMode::Default: {
      // Read-only tools skip the prompt — Default is about gating mutations,
      // not nagging on every list/read.
      if (bIsReadOnly) {
        const FString ChosenId = PickAllowId(Options);
        UE_LOG(
            LogUAgent, Verbose,
            TEXT("request_permission: kind='%s' name='%s' Default→auto-allow "
                 "(read-only) optionId='%s'"),
            *ToolKind, *McpName, *ChosenId);
        Complete(ResolveOutcome(ChosenId, Options));
        return;
      }

      // Mutating tool — defer to the user. PermissionBroker bridges to the
      // chat window; if no UI is installed it auto-denies, which we map to
      // PickRejectId so the agent gets a valid optionId back.
      FString FallbackTitle = ToolTitle;
      if (FallbackTitle.IsEmpty()) {
        FString MethodHint;
        if (ToolCallCopy.IsValid())
          ToolCallCopy->TryGetStringField(TEXT("title"), MethodHint);
        FallbackTitle = MethodHint.IsEmpty() ? TEXT("(tool)") : MethodHint;
      }

      FPermissionRequest Req;
      if (ToolCallCopy.IsValid())
        ToolCallCopy->TryGetStringField(TEXT("toolCallId"), Req.ToolCallId);
      Req.ToolTitle = FallbackTitle;
      Req.ToolKind = ToolKind;
      Req.RawToolCall = ToolCallCopy;

      // Capture Options + Complete by value into the callback. The broker
      // fires it from the game thread once the user clicks.
      FPermissionBroker::Get().Request(
          Req, [Options, Complete = MoveTemp(Complete),
                ToolKind](EPermissionOutcome Outcome) mutable {
            const bool bAllow = (Outcome == EPermissionOutcome::Allow);
            const FString ChosenId =
                bAllow ? PickAllowId(Options) : PickRejectId(Options);
            UE_LOG(LogUAgent, Verbose,
                   TEXT("request_permission: toolKind='%s' Default→user-%s "
                        "optionId='%s'"),
                   *ToolKind, bAllow ? TEXT("allow") : TEXT("deny"), *ChosenId);
            Complete(ResolveOutcome(ChosenId, Options));
          });
      return;
    }

    case EACPPermissionMode::FullAccess:
    default: {
      const FString ChosenId = PickAllowId(Options);
      UE_LOG(LogUAgent, Verbose,
             TEXT("request_permission: toolKind='%s' FullAccess→allow "
                  "optionId='%s'"),
             *ToolKind, *ChosenId);
      Complete(ResolveOutcome(ChosenId, Options));
      return;
    }
    }
  }
};
} // namespace

TSharedRef<IACPTool> CreateRequestPermissionTool() {
  return MakeShared<FRequestPermissionTool>();
}
} // namespace UAgent
