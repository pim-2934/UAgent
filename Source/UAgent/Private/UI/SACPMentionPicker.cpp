#include "SACPMentionPicker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UAgent"

void SACPMentionPicker::Construct(const FArguments &InArgs) {
  OnAssetPickedDelegate = InArgs._OnAssetPicked;

  ChildSlot[SAssignNew(Anchor, SMenuAnchor)
                .Placement(MenuPlacement_AboveAnchor)
                .MenuContent(BuildPopupContent())[InArgs._Content.Widget]];
}

TSharedRef<SWidget> SACPMentionPicker::BuildPopupContent() {
  SAssignNew(ResultsList, SListView<TSharedPtr<FAssetData>>)
      .ListItemsSource(&Results)
      .OnGenerateRow(this, &SACPMentionPicker::OnGenerateRow)
      .OnSelectionChanged(this, &SACPMentionPicker::OnSelectionChanged)
      .SelectionMode(ESelectionMode::Single);

  return SNew(SBox).MinDesiredWidth(360).MaxDesiredHeight(
      260)[SNew(SVerticalBox) +
           SVerticalBox::Slot().AutoHeight()
               [SAssignNew(SearchBox, SEditableTextBox)
                    .HintText(LOCTEXT("MentionHint", "Search assets…"))
                    .OnTextChanged_Lambda([this](const FText &T) {
                      RefreshResults(T.ToString());
                    })] +
           SVerticalBox::Slot().FillHeight(1.0f)[ResultsList.ToSharedRef()]];
}

TSharedRef<ITableRow>
SACPMentionPicker::OnGenerateRow(TSharedPtr<FAssetData> Item,
                                 const TSharedRef<STableViewBase> &Owner) {
  return SNew(
      STableRow<TSharedPtr<FAssetData>>,
      Owner)[SNew(SHorizontalBox) +
             SHorizontalBox::Slot().FillWidth(1.0f).Padding(
                 4,
                 2)[SNew(STextBlock).Text(FText::FromName(Item->AssetName))] +
             SHorizontalBox::Slot().AutoWidth().Padding(
                 4, 2)[SNew(STextBlock)
                           .Text(FText::FromName(Item->PackagePath))
                           .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))]];
}

void SACPMentionPicker::OnSelectionChanged(TSharedPtr<FAssetData> Item,
                                           ESelectInfo::Type SelectInfo) {
  if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
    return;
  OnAssetPickedDelegate.ExecuteIfBound(*Item);
}

void SACPMentionPicker::RefreshResults(const FString &Query) {
  Results.Reset();

  IAssetRegistry &AR =
      FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")
          .Get();
  TArray<FAssetData> All;
  FARFilter Filter;
  Filter.bRecursivePaths = true;
  Filter.PackagePaths.Add(TEXT("/Game"));
  AR.GetAssets(Filter, All);

  const FString Q = Query.TrimStartAndEnd();
  const int32 Max = 25;
  int32 Added = 0;
  for (const FAssetData &A : All) {
    if (Added >= Max)
      break;
    if (Q.IsEmpty() ||
        A.AssetName.ToString().Contains(Q, ESearchCase::IgnoreCase)) {
      Results.Add(MakeShared<FAssetData>(A));
      ++Added;
    }
  }

  if (ResultsList.IsValid())
    ResultsList->RequestListRefresh();
}

void SACPMentionPicker::Open() {
  if (Anchor.IsValid() && !Anchor->IsOpen()) {
    Anchor->SetIsOpen(true, /*bFocusMenu=*/false);
  }
}

void SACPMentionPicker::Close() {
  if (Anchor.IsValid() && Anchor->IsOpen()) {
    Anchor->SetIsOpen(false);
  }
}

bool SACPMentionPicker::IsOpen() const {
  return Anchor.IsValid() && Anchor->IsOpen();
}

bool SACPMentionPicker::ConfirmTopResult() {
  if (Results.Num() == 0 || !Results[0].IsValid())
    return false;
  OnAssetPickedDelegate.ExecuteIfBound(*Results[0]);
  return true;
}

#undef LOCTEXT_NAMESPACE
