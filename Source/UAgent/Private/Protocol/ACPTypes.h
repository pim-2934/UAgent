#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UAgent {
DECLARE_LOG_CATEGORY_EXTERN(LogUAgent, Log, All);

/** ACP ContentBlock variant — text / resource / resource_link / image / audio.
 */
struct FContentBlock {
  enum class EKind : uint8 {
    Text,
    Resource,
    ResourceLink,
    Image,
    Audio,
  };

  EKind Kind = EKind::Text;

  // Text
  FString Text;

  // Resource (embedded)
  FString ResourceUri;
  FString ResourceMimeType;
  FString ResourceText; // text variant
  FString ResourceBlob; // base64 variant (unused in phase 1 sends)

  // ResourceLink
  FString LinkUri;
  FString LinkName;
  FString LinkMimeType;
  int64 LinkSize = -1;

  // Image / Audio
  FString MediaMimeType;
  FString MediaDataBase64;

  static FContentBlock MakeText(const FString &InText);
  static FContentBlock MakeResourceLink(const FString &InUri,
                                        const FString &InName,
                                        const FString &InMime = FString(),
                                        int64 InSize = -1);
  static FContentBlock MakeResource(const FString &InUri, const FString &InMime,
                                    const FString &InText);

  TSharedRef<FJsonObject> ToJson() const;
  static bool FromJson(const TSharedRef<FJsonObject> &Obj, FContentBlock &Out);
};

/** One choice inside a session config option's `options` array. */
struct FConfigOptionChoice {
  FString Value;
  FString Name;
};

/**
 * One ACP "session config option" — a typed selector advertised by the agent.
 * Category is conventional and signals UX intent: "model", "mode",
 * "thought_level" (reasoning effort), or empty/custom.
 */
struct FConfigOption {
  FString Id;
  FString Category;
  FString CurrentValue;
  TArray<FConfigOptionChoice> Options;

  static bool FromJson(const TSharedRef<FJsonObject> &Obj, FConfigOption &Out);
};

/** Parses a JSON array of ConfigOption objects. Resets Out before populating;
 * malformed entries are skipped. */
void ParseConfigOptions(const TArray<TSharedPtr<FJsonValue>> &In,
                        TArray<FConfigOption> &Out);

/** Stop reasons returned by session/prompt. */
enum class EStopReason : uint8 {
  EndTurn,
  MaxTokens,
  MaxTurnRequests,
  Refusal,
  Cancelled,
  Unknown,
};

EStopReason ParseStopReason(const FString &In);
const TCHAR *StopReasonToString(EStopReason In);

/** Session update variants (subset — unknown kinds fall through as Raw). */
struct FSessionUpdate {
  enum class EKind : uint8 {
    UserMessageChunk,
    AgentMessageChunk,
    AgentThoughtChunk,
    ToolCall,
    ToolCallUpdate,
    Plan,
    AvailableCommandsUpdate,
    CurrentModeUpdate,
    ConfigOptionUpdate,
    Raw,
  };

  EKind Kind = EKind::Raw;
  FString SessionId;

  // Chunk payloads use a single content block.
  FContentBlock Content;

  // Tool-call bookkeeping (populated for ToolCall / ToolCallUpdate).
  FString ToolCallId;
  FString ToolCallTitle;
  FString ToolCallKind;
  FString ToolCallStatus;
  TArray<FContentBlock> ToolCallContent;

  // ConfigOptionUpdate: full advertised set, with currentValue reflecting the
  // agent's post-change state.
  TArray<FConfigOption> ConfigOptions;

  // Fallback — raw object for kinds we don't model yet.
  TSharedPtr<FJsonObject> RawObject;

  static bool FromJson(const TSharedRef<FJsonObject> &Params,
                       FSessionUpdate &Out);
};

/** Utility — serialize ContentBlock array to a JSON array. */
TArray<TSharedPtr<FJsonValue>>
ContentBlocksToJson(const TArray<FContentBlock> &Blocks);
} // namespace UAgent
