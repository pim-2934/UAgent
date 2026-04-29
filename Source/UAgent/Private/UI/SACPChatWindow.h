#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

namespace UAgent {
class FACPClient;
struct FSessionUpdate;
struct FPermissionRequest;
enum class EClientState : uint8;
enum class EStopReason : uint8;
enum class EPermissionOutcome : uint8;
struct FContentBlock;
} // namespace UAgent

class FChatMessageLog;
class SACPContextStrip;
class SACPMentionPicker;
class SACPMessageList;
class SHorizontalBox;
class SMultiLineEditableTextBox;
class STextBlock;

/**
 * Thin shell for the UAgent chat tab: composes SACPMessageList,
 * SACPContextStrip, SACPMentionPicker, and an input row. Owns the FACPClient
 * and the FChatMessageLog; translates inbound session updates and client state
 * transitions into log mutations.
 */
class SACPChatWindow : public SCompoundWidget {
public:
  SLATE_BEGIN_ARGS(SACPChatWindow) {}
  SLATE_END_ARGS()

  void Construct(const FArguments &InArgs);
  virtual ~SACPChatWindow() override;

  void StartSession();

private:
  TSharedRef<SWidget> BuildHeader();
  TSharedRef<SWidget> BuildInputRow();
  /** The bottom strip — Permission Mode dropdown on the left, agent's
   * advertised model dropdown (when present) inline to its right. */
  TSharedRef<SWidget> BuildPermissionModeRow();
  /** Repopulates AgentSettingsContainer from the client's latest snapshot.
   * Called on session/new advertisement and config_option_update; safe to
   * call when nothing's advertised (results in an empty, collapsed slot). */
  void RefreshAgentSettings();

  FReply OnSendClicked();
  FReply OnNewSessionClicked();
  FReply OnExportClicked();
  FReply OnInputKey(const FGeometry &, const FKeyEvent &Key);
  void OnInputTextChanged(const FText &NewText);
  FText GetStatusText() const;
  bool IsSendEnabled() const;

  void OnClientStateChanged(UAgent::EClientState NewState);
  void OnSessionUpdateReceived(const UAgent::FSessionUpdate &Update);
  void OnPromptDone(UAgent::EStopReason Reason, FString ErrorOrEmpty);
  void OnClientError(const FString &Message);

  void OnMentionPicked(const FAssetData &Asset);
  void AddContextChip(const FAssetData &Asset);
  void RemoveContextChip(const FAssetData &Asset);
  void RebuildContextStrip();
  TArray<UAgent::FContentBlock> BuildContextBlocks();
  TArray<FAssetData> CollectOpenAssets() const;

  /** PermissionBroker handler — pushes a Permission row to the log and
   * remembers the completion callback against its index. */
  void
  OnPermissionRequested(const UAgent::FPermissionRequest &Req,
                        TFunction<void(UAgent::EPermissionOutcome)> Complete);

  /** Routed from SACPMessageList when the user clicks Accept/Cancel. */
  void OnPermissionRowDecided(const FString &PermissionId, bool bAllow);

private:
  TSharedPtr<UAgent::FACPClient> Client;
  TSharedPtr<FChatMessageLog> MessageLog;

  TSharedPtr<SACPMessageList> MessageList;
  TSharedPtr<SACPContextStrip> ContextStrip;
  TSharedPtr<SMultiLineEditableTextBox> InputBox;
  TSharedPtr<STextBlock> StatusLabel;
  TSharedPtr<SACPMentionPicker> MentionPicker;

  // Pending permission callbacks keyed by the per-row UUID assigned in
  // FChatMessageLog::AppendPermission. Stable across log mutations, unlike
  // a positional index. Cleared as each row is resolved (button click) or
  // when the chat window tears down (we deny any leftovers so the agent
  // isn't left waiting).
  TMap<FString, TFunction<void(UAgent::EPermissionOutcome)>> PendingPermissions;

  TSharedPtr<SHorizontalBox> AgentSettingsContainer;

  TMap<FName, FAssetData> ContextChips;
  // Subset of ContextChips keys that were added by an @[AssetName] token. These
  // get auto-removed from ContextChips when the token disappears from the
  // input. Chips added via the + button are not tracked here and persist
  // regardless of text.
  TSet<FName> TokenBackedChipPackages;
  int32 PendingAtPosition = INDEX_NONE;
};
