#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SEditableTextBox;
class SMenuAnchor;

/**
 * @-mention popup: wraps the SMenuAnchor + search box + asset list. Owns no
 * chat state — emits OnAssetPicked and lets the host decide what to do (add
 * chip, strip @query from input, refocus). The anchor widget hangs below the
 * content you pass in via `[ ... ]`.
 */
class SACPMentionPicker : public SCompoundWidget {
public:
  DECLARE_DELEGATE_OneParam(FOnAssetPicked, const FAssetData & /*Asset*/);

  SLATE_BEGIN_ARGS(SACPMentionPicker) {}
  SLATE_EVENT(FOnAssetPicked, OnAssetPicked)
  SLATE_DEFAULT_SLOT(FArguments, Content)
  SLATE_END_ARGS()

  void Construct(const FArguments &InArgs);

  /** Refresh the result list with assets whose name contains Query
   * (case-insensitive, empty = all). */
  void RefreshResults(const FString &Query);

  void Open();
  void Close();
  bool IsOpen() const;

  /** Fire OnAssetPicked with the first result. Returns false if the list is
   * empty. */
  bool ConfirmTopResult();

private:
  TSharedRef<SWidget> BuildPopupContent();
  TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FAssetData> Item,
                                      const TSharedRef<STableViewBase> &Owner);
  void OnSelectionChanged(TSharedPtr<FAssetData> Item,
                          ESelectInfo::Type SelectInfo);

  FOnAssetPicked OnAssetPickedDelegate;

  TSharedPtr<SMenuAnchor> Anchor;
  TSharedPtr<SEditableTextBox> SearchBox;
  TSharedPtr<SListView<TSharedPtr<FAssetData>>> ResultsList;
  TArray<TSharedPtr<FAssetData>> Results;
};
