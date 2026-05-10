#pragma once

#include "ChatMessageLog.h"
#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * Fires when the user clicks Accept or Cancel on a Permission row. The chat
 * window translates this into a PermissionBroker callback resolution. The
 * id is the per-row UUID assigned in FChatMessageLog::AppendPermission and
 * is stable across log mutations.
 */
DECLARE_DELEGATE_TwoParams(FOnPermissionDecided,
                           const FString & /*PermissionId*/, bool /*bAllow*/);

/** Outcome the user picked on a propose_missing_tool card. Three buttons map
 * onto Accepted (sidecar + halt), Skipped (let agent continue), Cancelled
 * (stop turn without sidecar). Mirrors UAgent::EProposalOutcome but kept
 * widget-side to avoid pulling the Tools/Developer header into UI code. */
enum class EProposalRowDecision : uint8 {
  Accepted,
  Skipped,
  Cancelled,
};

/**
 * Fires when the user clicks Accept / Skip / Cancel on a Proposal row.
 * The id is the per-row UUID stamped in the FProposalRequest and the sidecar.
 */
DECLARE_DELEGATE_TwoParams(FOnProposalDecided,
                           const FString & /*ProposalId*/,
                           EProposalRowDecision /*Decision*/);

/**
 * Fires when the user clicks Retry / Dismiss on a ProposalReplay banner.
 * bRetry true → chat window replays the saved prompt; false → mark sidecar
 * discarded.
 */
DECLARE_DELEGATE_TwoParams(FOnProposalReplayDecided,
                           const FString & /*ProposalId*/, bool /*bRetry*/);

/**
 * Virtualized list view over an FChatMessageLog. Subscribes to the log's
 * OnChanged delegate and reissues `RequestListRefresh` + scroll-into-view on
 * any mutation. Row text is lambda-bound to the underlying FACPChatMessageItem
 * so streaming agent chunks update the existing row in place instead of
 * regenerating it.
 */
class SACPMessageList : public SCompoundWidget {
public:
  SLATE_BEGIN_ARGS(SACPMessageList) {}
  SLATE_EVENT(FOnPermissionDecided, OnPermissionDecided)
  SLATE_EVENT(FOnProposalDecided, OnProposalDecided)
  SLATE_EVENT(FOnProposalReplayDecided, OnProposalReplayDecided)
  SLATE_END_ARGS()

  void Construct(const FArguments &InArgs, TSharedRef<FChatMessageLog> InLog);

private:
  TSharedRef<ITableRow> OnGenerateRow(FACPChatMessageItemRef Item,
                                      const TSharedRef<STableViewBase> &Owner);
  TSharedRef<SWidget> MakeMessageRow(const FACPChatMessageItemRef &Item);
  TSharedRef<SWidget> MakePermissionRow(const FACPChatMessageItemRef &Item);
  TSharedRef<SWidget> MakeProposalRow(const FACPChatMessageItemRef &Item);
  TSharedRef<SWidget>
  MakeProposalReplayRow(const FACPChatMessageItemRef &Item);
  void HandleLogChanged();
  void HandleAgentTurnEnded();
  bool IsToolExpanded(const FString &ToolCallId) const;

  // Sticky-bottom auto-scroll. The chat pins to the last row while the user
  // stays near the bottom; as soon as they scroll up to read earlier messages
  // we stop yanking them back. ScheduleScrollCatchup re-issues ScrollToBottom
  // across several ticks because the markdown widget tree produced at turn-end
  // (see HandleAgentTurnEnded) finalizes its measured height after the first
  // post-rebuild tick, and a single scroll call would undershoot.
  EActiveTimerReturnType TickScrollCatchup(double InCurrentTime,
                                           float InDeltaTime);
  void ScheduleScrollCatchup();
  void OnListScrolled(double InScrollOffsetInItems);

  TSharedPtr<FChatMessageLog> Log;
  TSharedPtr<SListView<FACPChatMessageItemRef>> ListView;
  TMap<FString, bool> ToolExpandedById;
  TSharedPtr<FActiveTimerHandle> ScrollCatchupHandle;
  int32 ScrollCatchupFramesLeft = 0;
  bool bPinToBottom = true;
  FOnPermissionDecided OnPermissionDecided;
  FOnProposalDecided OnProposalDecided;
  FOnProposalReplayDecided OnProposalReplayDecided;
};
