#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

namespace UAgent {
struct FSessionUpdate;
}

/** Single entry in the chat transcript. */
struct FACPChatMessageItem {
  enum class ERole : uint8 { User, Agent, Tool, System, Permission };

  // Lifecycle of a Permission row:
  //   Pending — buttons are live; user hasn't clicked yet.
  //   Allowed/Denied — terminal; buttons hide and the row shows the outcome.
  enum class EPermissionState : uint8 { Pending, Allowed, Denied };

  ERole Role = ERole::User;
  FString Text;
  TArray<FAssetData> Contexts; // user-message chips
  FString ToolCallId;
  FString ToolCallTitle;
  FString ToolCallStatus = TEXT("pending");
  FLinearColor Tint = FLinearColor::White;

  // Permission-row payload. Only meaningful when Role == Permission.
  // PermissionId is a stable per-row UUID used by the chat window to key
  // pending broker callbacks and by SACPMessageList to fire the correct
  // resolution; it survives any future log compaction (which array indexes
  // wouldn't).
  FString PermissionId;
  FString PermissionToolKind;
  FString PermissionArgsPreview;
  EPermissionState PermissionState = EPermissionState::Pending;

  // True once the message is settled (user/tool/system always start true; an
  // agent message stays false while its turn is streaming and flips true when
  // another role appends or EndAgentTurn fires). Views render streaming
  // agents as plain text and completed agents as parsed markdown.
  bool bTurnComplete = true;
};

using FACPChatMessageItemRef = TSharedRef<FACPChatMessageItem>;

DECLARE_MULTICAST_DELEGATE(FOnChatLogChanged);
DECLARE_MULTICAST_DELEGATE(FOnAgentTurnEnded);

/**
 * Pure model for the chat transcript. Holds the append-only message array and
 * two small markers: which item is the in-flight agent reply (so chunks can
 * coalesce) and which tool-call id maps to which message (so status updates
 * land in the right place).
 *
 * Views subscribe to OnChanged and refresh themselves; the model has no Slate
 * dependency and could back a non-UI surface (tests, a log export, a second
 * renderer) without changes.
 */
class FChatMessageLog {
public:
  /** User-authored message. Implicitly ends any in-flight agent turn. */
  void AppendUser(const FString &Text, const TArray<FAssetData> &Contexts);

  /** Agent text chunk. Coalesces with the current agent message if one is open.
   */
  void AppendAgentChunk(const FString &Text);

  /** Adds a tool-call row. Ends the agent turn so subsequent chunks start
   * fresh. */
  void AppendTool(const UAgent::FSessionUpdate &Update);

  /** Updates an existing tool-call row in place; falls back to AppendTool if
   * unseen. */
  void UpdateTool(const UAgent::FSessionUpdate &Update);

  /** Transient system/status line. Does NOT end the agent turn. */
  void AppendSystem(const FString &Text, const FLinearColor &Tint);

  /**
   * Permission prompt row. Carries Accept/Cancel buttons until the user
   * resolves it. Returns a stable per-row id (UUID) the caller (chat window)
   * uses to key the pending broker callback and to flip state via
   * SetPermissionState() later. The id survives log mutations in a way
   * positional indexes can't.
   */
  FString AppendPermission(const FString &ToolTitle, const FString &ToolKind,
                           const FString &ArgsPreview);

  /** Flip a Permission row to Allowed/Denied and broadcast OnChanged. No-op
   * if no row matches the id. */
  void SetPermissionState(const FString &PermissionId,
                          FACPChatMessageItem::EPermissionState NewState);

  /**
   * Routes an inbound session update to AppendAgentChunk / AppendTool /
   * UpdateTool based on its Kind. Keeps the chat window out of the switch —
   * it hands the update in and lets the log decide.
   */
  void ApplySessionUpdate(const UAgent::FSessionUpdate &Update);

  /** Mark the current agent reply as done; next AppendAgentChunk starts a new
   * row. */
  void EndAgentTurn();

  /** Drop all messages and markers. Broadcasts OnChanged so views repaint
   * empty. */
  void Reset();

  const TArray<FACPChatMessageItemRef> &GetMessages() const { return Messages; }

  FOnChatLogChanged OnChanged;
  FOnAgentTurnEnded OnAgentTurnEnded;

private:
  /** Close any open agent turn: flips bTurnComplete and fires OnAgentTurnEnded.
   */
  void CloseAgentTurnIfOpen();

  TArray<FACPChatMessageItemRef> Messages;
  int32 CurrentAgentMessageIndex = INDEX_NONE;
  TMap<FString, int32> ToolCallIndexById;
};
