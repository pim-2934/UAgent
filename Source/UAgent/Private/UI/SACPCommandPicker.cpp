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

  // Pin the popup width to the anchored input box so long descriptions
  // wrap inside the row instead of pushing the popup wider than the chat.
  // Lambda evaluates at layout time — Anchor is fully constructed by then.
  return SNew(SBox)
      .WidthOverride_Lambda([this]() -> FOptionalSize {
        if (Anchor.IsValid()) {
          const float W = Anchor->GetCachedGeometry().GetLocalSize().X;
          if (W > 0.0f)
            return W;
        }
        return 360.0f;
      })
      .MaxDesiredHeight(260)[ResultsList.ToSharedRef()];
}

TSharedRef<ITableRow>
SACPCommandPicker::OnGenerateRow(TSharedPtr<UAgent::FAvailableCommand> Item,
                                 const TSharedRef<STableViewBase> &Owner) {
  // Mirror SACPMentionPicker's row shape: primary text fills the left,
  // secondary text auto-widths on the right in dim gray. The hint (if any)
  // gets appended to the primary label as a command-signature suffix —
  // `/test <pattern>` reads naturally without needing a separate column.
  const FString Primary =
      Item.IsValid() ? (Item->InputHint.IsEmpty()
                            ? FString::Printf(TEXT("/%s"), *Item->Name)
                            : FString::Printf(TEXT("/%s %s"), *Item->Name,
                                              *Item->InputHint))
                     : FString();
  const FString Secondary = Item.IsValid() ? Item->Description : FString();

  // Primary on the left at its natural width; description fills the rest
  // and wraps. Without FillWidth + AutoWrapText, a long description would
  // expand the row's intrinsic width and push the popup beyond the input.
  return SNew(STableRow<TSharedPtr<UAgent::FAvailableCommand>>, Owner)
      [SNew(SHorizontalBox) +
       SHorizontalBox::Slot()
           .AutoWidth()
           .VAlign(VAlign_Top)
           .Padding(4, 2)[SNew(STextBlock).Text(FText::FromString(Primary))] +
       SHorizontalBox::Slot()
           .FillWidth(1.0f)
           .VAlign(VAlign_Top)
           .Padding(4, 2)[SNew(STextBlock)
                              .Text(FText::FromString(Secondary))
                              .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f))
                              .AutoWrapText(true)]];
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
