#pragma once

#include "CoreMinimal.h"
#include "Protocol/ACPTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SEditableTextBox;
class SMenuAnchor;

/**
 * Slash-command popup: wraps an SMenuAnchor + filtered list of commands
 * advertised by the backing agent (per ACP `availableCommands` /
 * `available_commands_update`). Mirrors SACPMentionPicker's shape — owns no
 * chat state, emits OnCommandPicked, and lets the host rewrite the input
 * box. The anchor hangs below whatever widget is passed via `[ ... ]`.
 */
class SACPCommandPicker : public SCompoundWidget {
public:
  DECLARE_DELEGATE_OneParam(FOnCommandPicked,
                            const UAgent::FAvailableCommand & /*Command*/);

  SLATE_BEGIN_ARGS(SACPCommandPicker) {}
  SLATE_EVENT(FOnCommandPicked, OnCommandPicked)
  SLATE_DEFAULT_SLOT(FArguments, Content)
  SLATE_END_ARGS()

  void Construct(const FArguments &InArgs);

  /** Replace the available-commands snapshot. Re-applies the current filter
   * (preserves user's typed query while the agent re-advertises). */
  void SetCommands(const TArray<UAgent::FAvailableCommand> &InCommands);

  /** Refresh the result list with commands whose name starts with Query
   * (case-insensitive; substring match as a fallback). Empty Query lists
   * everything. */
  void RefreshResults(const FString &Query);

  void Open();
  void Close();
  bool IsOpen() const;

  /** Fire OnCommandPicked with the first result. Returns false if the list is
   * empty. */
  bool ConfirmTopResult();

private:
  TSharedRef<SWidget> BuildPopupContent();
  TSharedRef<ITableRow>
  OnGenerateRow(TSharedPtr<UAgent::FAvailableCommand> Item,
                const TSharedRef<STableViewBase> &Owner);
  void OnSelectionChanged(TSharedPtr<UAgent::FAvailableCommand> Item,
                          ESelectInfo::Type SelectInfo);

  FOnCommandPicked OnCommandPickedDelegate;

  TSharedPtr<SMenuAnchor> Anchor;
  TSharedPtr<SListView<TSharedPtr<UAgent::FAvailableCommand>>> ResultsList;

  // Full advertised set, kept so re-filtering doesn't require the host to
  // re-push the snapshot on every keystroke.
  TArray<UAgent::FAvailableCommand> AllCommands;
  // Filtered view bound to the list widget (Slate retains the pointer).
  TArray<TSharedPtr<UAgent::FAvailableCommand>> Results;
  // Last query supplied to RefreshResults; reused by SetCommands so an
  // in-flight agent update doesn't clobber the user's typed filter.
  FString CurrentQuery;
};
