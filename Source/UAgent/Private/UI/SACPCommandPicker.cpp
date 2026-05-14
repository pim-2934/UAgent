#include "SACPCommandPicker.h"

#include "Widgets/Input/SMenuAnchor.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UAgent"

void SACPCommandPicker::Construct(const FArguments &InArgs) {
  OnCommandPickedDelegate = InArgs._OnCommandPicked;

  ChildSlot[SAssignNew(Anchor, SMenuAnchor)
                .Placement(MenuPlacement_AboveAnchor)
                .MenuContent(BuildPopupContent())[InArgs._Content.Widget]];
}

TSharedRef<SWidget> SACPCommandPicker::BuildPopupContent() {
  SAssignNew(ResultsList, SListView<TSharedPtr<UAgent::FAvailableCommand>>)
      .ListItemsSource(&Results)
      .OnGenerateRow(this, &SACPCommandPicker::OnGenerateRow)
      .OnSelectionChanged(this, &SACPCommandPicker::OnSelectionChanged)
      .SelectionMode(ESelectionMode::Single);

  return SNew(SBox).MinDesiredWidth(360).MaxDesiredHeight(
      260)[ResultsList.ToSharedRef()];
}

TSharedRef<ITableRow>
SACPCommandPicker::OnGenerateRow(TSharedPtr<UAgent::FAvailableCommand> Item,
                                 const TSharedRef<STableViewBase> &Owner) {
  // Row layout:  /name [hint]   description
  // Hint sits right next to the name (dim) so the user sees expected args
  // without scanning; the description fills the remaining width.
  const FString NameLabel =
      Item.IsValid() ? FString::Printf(TEXT("/%s"), *Item->Name) : FString();
  const FString HintLabel =
      (Item.IsValid() && !Item->InputHint.IsEmpty())
          ? FString::Printf(TEXT(" %s"), *Item->InputHint)
          : FString();
  const FString Description = Item.IsValid() ? Item->Description : FString();

  return SNew(STableRow<TSharedPtr<UAgent::FAvailableCommand>>, Owner)
      [SNew(SHorizontalBox) +
       SHorizontalBox::Slot()
           .AutoWidth()
           .Padding(4, 2)[SNew(STextBlock).Text(FText::FromString(NameLabel))] +
       SHorizontalBox::Slot().AutoWidth().Padding(
           0, 2)[SNew(STextBlock)
                     .Text(FText::FromString(HintLabel))
                     .ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f))] +
       SHorizontalBox::Slot().FillWidth(1.0f).Padding(
           8, 2)[SNew(STextBlock)
                     .Text(FText::FromString(Description))
                     .ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))]];
}

void SACPCommandPicker::OnSelectionChanged(
    TSharedPtr<UAgent::FAvailableCommand> Item, ESelectInfo::Type SelectInfo) {
  if (!Item.IsValid() || SelectInfo == ESelectInfo::Direct)
    return;
  OnCommandPickedDelegate.ExecuteIfBound(*Item);
}

void SACPCommandPicker::SetCommands(
    const TArray<UAgent::FAvailableCommand> &InCommands) {
  AllCommands = InCommands;
  RefreshResults(CurrentQuery);
}

void SACPCommandPicker::RefreshResults(const FString &Query) {
  CurrentQuery = Query;
  const FString Q = Query.TrimStartAndEnd();
  Results.Reset();

  // Two-pass match: prefix matches first, then substring matches, so that
  // typing "init" surfaces "init" above "session-init". Both passes
  // case-insensitive; empty query lists everything in advertised order.
  if (Q.IsEmpty()) {
    Results.Reserve(AllCommands.Num());
    for (const UAgent::FAvailableCommand &C : AllCommands) {
      Results.Add(MakeShared<UAgent::FAvailableCommand>(C));
    }
  } else {
    TArray<const UAgent::FAvailableCommand *> Prefix;
    TArray<const UAgent::FAvailableCommand *> Sub;
    for (const UAgent::FAvailableCommand &C : AllCommands) {
      if (C.Name.StartsWith(Q, ESearchCase::IgnoreCase)) {
        Prefix.Add(&C);
      } else if (C.Name.Contains(Q, ESearchCase::IgnoreCase)) {
        Sub.Add(&C);
      }
    }
    Results.Reserve(Prefix.Num() + Sub.Num());
    for (const UAgent::FAvailableCommand *C : Prefix)
      Results.Add(MakeShared<UAgent::FAvailableCommand>(*C));
    for (const UAgent::FAvailableCommand *C : Sub)
      Results.Add(MakeShared<UAgent::FAvailableCommand>(*C));
  }

  if (ResultsList.IsValid())
    ResultsList->RequestListRefresh();
}

void SACPCommandPicker::Open() {
  if (Anchor.IsValid() && !Anchor->IsOpen()) {
    Anchor->SetIsOpen(true, /*bFocusMenu=*/false);
  }
}

void SACPCommandPicker::Close() {
  if (Anchor.IsValid() && Anchor->IsOpen()) {
    Anchor->SetIsOpen(false);
  }
}

bool SACPCommandPicker::IsOpen() const {
  return Anchor.IsValid() && Anchor->IsOpen();
}

bool SACPCommandPicker::ConfirmTopResult() {
  if (Results.Num() == 0 || !Results[0].IsValid())
    return false;
  OnCommandPickedDelegate.ExecuteIfBound(*Results[0]);
  return true;
}

#undef LOCTEXT_NAMESPACE
