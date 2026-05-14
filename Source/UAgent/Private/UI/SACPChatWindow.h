#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Protocol/ACPTypes.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;

namespace UAgent {
class FACPClient;
struct FPermissionRequest;
struct FProposalRequest;
enum class EClientState : uint8;
enum class EStopReason : uint8;
enum class EPermissionOutcome : uint8;
enum class EProposalOutcome : uint8;
} // namespace UAgent

enum class EProposalRowDecision : uint8;

class FChatMessageLog;
class SACPCommandPicker;
class SACPContextStrip;
class SACPMentionPicker;
class SACPMessageList;
class SACPPlanStrip;
class SHorizontalBox;
class SMenuAnchor;
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
  FReply OnHistoryClicked();
  /** Built lazily by the SMenuAnchor each time it opens — reads from
   * RecentSessions which OnHistoryClicked populates before opening. */
  TSharedRef<SWidget> BuildHistoryMenuContent();
  /** Called when the user picks a session from the history menu — clears the
   * chat log so replayed session/update notifications can rebuild it from
   * scratch, then asks the client to swap to the chosen session. */
  void OnHistoryEntryPicked(const FString &SessionIdToLoad);
  FReply OnInputKey(const FGeometry &, const FKeyEvent &Key);
  void OnInputTextChanged(const FText &NewText);
  FText GetStatusText() const;
  bool IsSendEnabled() const;

  void OnClientStateChanged(UAgent::EClientState NewState);
  void OnSessionUpdateReceived(const UAgent::FSessionUpdate &Update);
  void OnPromptDone(UAgent::EStopReason Reason, FString ErrorOrEmpty);
  void OnClientError(const FString &Message);

  void OnMentionPicked(const FAssetData &Asset);
  void OnCommandPicked(const UAgent::FAvailableCommand &Command);
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

  /** ProposalBroker handler — pushes a Proposal row to the log and remembers
   * the completion callback against the proposal id. */
  void OnProposalRequested(const UAgent::FProposalRequest &Req,
                           TFunction<void(UAgent::EProposalOutcome)> Complete);

  /** Routed from SACPMessageList when the developer clicks
   * Accept/Skip/Cancel on a Proposal row. Writes the sidecar JSON on Accept
   * before resolving the broker callback so a successful response means the
   * file is on disk. */
  void OnProposalRowDecided(const FString &ProposalId,
                            EProposalRowDecision Decision);

  /** Routed from SACPMessageList when the developer clicks Retry/Dismiss
   * on a ProposalReplay banner. */
  void OnProposalReplayDecided(const FString &ProposalId, bool bRetry);

  /** Scan Saved/UAgent/Proposals on session start and append a banner per
   * pending sidecar. No-op when the developer gate is closed. */
  void ScanForPendingProposals();

private:
  TSharedPtr<UAgent::FACPClient> Client;
  TSharedPtr<FChatMessageLog> MessageLog;

  TSharedPtr<SACPMessageList> MessageList;
  TSharedPtr<SACPPlanStrip> PlanStrip;
  TSharedPtr<SACPContextStrip> ContextStrip;
  TSharedPtr<SMultiLineEditableTextBox> InputBox;
  TSharedPtr<STextBlock> StatusLabel;
  TSharedPtr<SACPMentionPicker> MentionPicker;
  TSharedPtr<SACPCommandPicker> CommandPicker;
  TSharedPtr<SMenuAnchor> HistoryAnchor;

  // Most-recent ListSessions result. Refilled on every history-button click;
  // BuildHistoryMenuContent reads this when the SMenuAnchor opens (which
  // happens *after* the request resolves, so the menu is never empty when
  // built from a successful response).
  TArray<UAgent::FSessionInfo> RecentSessions;
  // Set while a session/list request is in flight to block double-clicks and
  // drive the disabled state on the history button.
  bool bHistoryRequestInFlight = false;
  // Non-empty if the last ListSessions invocation returned an error; rendered
  // inside the menu so the user sees why the list is empty.
  FString LastHistoryError;

  // Pending permission callbacks keyed by the per-row UUID assigned in
  // FChatMessageLog::AppendPermission. Stable across log mutations, unlike
  // a positional index. Cleared as each row is resolved (button click) or
  // when the chat window tears down (we deny any leftovers so the agent
  // isn't left waiting).
  TMap<FString, TFunction<void(UAgent::EPermissionOutcome)>> PendingPermissions;

  // Pending proposal callbacks keyed by FProposalRequest::Id (also the row
  // id and the sidecar's id). Same survival/termination story as
  // PendingPermissions. We hold both the callback AND the original request —
  // the callback resolves the agent, the request carries InputSchema /
  // ExampleCall fields the chat row doesn't itself store, which the sidecar
  // JSON needs in full so the developer can implement the proposed tool.
  struct FPendingProposal {
    TFunction<void(UAgent::EProposalOutcome)> Complete;
    TSharedPtr<FJsonObject> InputSchema;
    TSharedPtr<FJsonObject> ExampleCall;
    bool bIsReadOnly = false;
  };
  TMap<FString, FPendingProposal> PendingProposals;

  // Captured at the start of OnSendClicked so a halt-during-this-turn
  // proposal can stamp the saved-prompt text into the sidecar JSON for
  // next-session replay.
  FString LastUserPromptText;

  // Originating-prompt UUID stamped on every proposal sidecar from the same
  // turn. Lets multi-proposal turns be marked replayed/discarded together
  // when the developer resolves any one of them.
  FString CurrentTurnId;

  TSharedPtr<SHorizontalBox> AgentSettingsContainer;

  // Once-per-session gate for the AGENTS.md project context block. We read
  // <ProjectDir>/AGENTS.md on the first user prompt and prepend its content
  // as a framing context block; subsequent prompts skip it because the agent
  // already has it. Reset to false in StartSession so each new session
  // re-loads (catches edits to AGENTS.md between sessions).
  bool bProjectContextSent = false;

  TMap<FName, FAssetData> ContextChips;
  // Subset of ContextChips keys that were added by an @[AssetName] token. These
  // get auto-removed from ContextChips when the token disappears from the
  // input. Chips added via the + button are not tracked here and persist
  // regardless of text.
  TSet<FName> TokenBackedChipPackages;
  int32 PendingAtPosition = INDEX_NONE;
};
