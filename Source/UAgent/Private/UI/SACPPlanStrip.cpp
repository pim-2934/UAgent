#include "SACPPlanStrip.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace {

// Status glyphs as text — avoids depending on icon brush names that vary
// across UE versions. The widths are similar enough that rows stay aligned
// without an explicit MinDesiredWidth on the glyph slot.
const TCHAR *StatusGlyph(UAgent::EPlanEntryStatus Status) {
  switch (Status) {
  case UAgent::EPlanEntryStatus::Completed:
    return TEXT("✓"); // ✓
  case UAgent::EPlanEntryStatus::InProgress:
    return TEXT("◐"); // ◐
  case UAgent::EPlanEntryStatus::Pending:
    return TEXT("○"); // ○
  case UAgent::EPlanEntryStatus::Unknown:
  default:
    return TEXT("·"); // · (so a future spec value still renders)
  }
}

// Color the row text by status: completed dims (it's history), in-progress
// pops, pending is neutral. Priority tints the in-progress / pending rows
// only — completed entries shouldn't shift visual weight based on priority
// because the work is done.
FLinearColor RowColor(const UAgent::FPlanEntry &Entry) {
  using UAgent::EPlanEntryPriority;
  using UAgent::EPlanEntryStatus;
  if (Entry.Status == EPlanEntryStatus::Completed) {
    return FLinearColor(0.55f, 0.55f, 0.55f); // dim — done
  }
  if (Entry.Status == EPlanEntryStatus::InProgress) {
    return FLinearColor(0.95f, 0.92f, 0.65f); // warm highlight
  }
  // Pending / unknown — subtle priority shift.
  switch (Entry.Priority) {
  case EPlanEntryPriority::High:
    return FLinearColor(1.00f, 0.85f, 0.65f); // soft warm
  case EPlanEntryPriority::Low:
    return FLinearColor(0.70f, 0.70f, 0.70f); // muted
  case EPlanEntryPriority::Medium:
  case EPlanEntryPriority::Unknown:
  default:
    return FLinearColor(0.88f, 0.88f, 0.88f); // neutral
  }
}

int32 CountCompleted(const TArray<UAgent::FPlanEntry> &Entries) {
  int32 N = 0;
  for (const UAgent::FPlanEntry &E : Entries) {
    if (E.Status == UAgent::EPlanEntryStatus::Completed)
      ++N;
  }
  return N;
}

} // namespace

void SACPPlanStrip::Construct(const FArguments & /*InArgs*/) {
  SAssignNew(BodyContainer, SVerticalBox);

  ChildSlot[SNew(SBorder)
                .BorderImage(
                    FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
                .Visibility(this, &SACPPlanStrip::GetSelfVisibility)
                .Padding(FMargin(0))
                    [SNew(SVerticalBox) +
                     SVerticalBox::Slot().AutoHeight()
                         [SNew(SButton)
                              .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                              .ContentPadding(FMargin(8, 4))
                              .HAlign(HAlign_Fill)
                              .OnClicked(this,
                                         &SACPPlanStrip::OnHeaderClicked)
                                  [SNew(SHorizontalBox) +
                                   SHorizontalBox::Slot()
                                       .AutoWidth()
                                       .VAlign(VAlign_Center)
                                       .Padding(0, 0, 6, 0)
                                           [SNew(SImage)
                                                .Image(this,
                                                       &SACPPlanStrip::
                                                           GetChevronBrush)
                                                .DesiredSizeOverride(
                                                    FVector2D(12, 12))
                                                .ColorAndOpacity(
                                                    FSlateColor::
                                                        UseForeground())] +
                                   SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(this,
                                                      &SACPPlanStrip::
                                                          GetHeaderText)
                                                .ColorAndOpacity(FLinearColor(
                                                    0.85f, 0.85f, 0.85f))]]] +
                     SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 6)
                         [SNew(SBox)
                              .Visibility(this,
                                          &SACPPlanStrip::GetBodyVisibility)
                                  [BodyContainer.ToSharedRef()]]]];
}

void SACPPlanStrip::SetPlan(const TArray<UAgent::FPlanEntry> &InEntries) {
  Entries = InEntries;

  // Auto-expand exactly once per session, the first time a non-empty plan
  // arrives. After that we keep whatever the user has chosen, so a plan
  // they collapsed doesn't keep popping back open on every revision.
  if (!bAutoExpandedOnce && Entries.Num() > 0) {
    bExpanded = true;
    bAutoExpandedOnce = true;
  }

  // Rebuild the body — rows are cheap and the row count is small (~5-15
  // typical), so a wholesale rebuild is simpler than diffing.
  if (!BodyContainer.IsValid())
    return;
  BodyContainer->ClearChildren();
  for (const UAgent::FPlanEntry &Entry : Entries) {
    const FString Glyph = StatusGlyph(Entry.Status);
    const FLinearColor Color = RowColor(Entry);
    BodyContainer->AddSlot().AutoHeight().Padding(0, 1)
        [SNew(SHorizontalBox) +
         SHorizontalBox::Slot()
             .AutoWidth()
             .VAlign(VAlign_Center)
             .Padding(0, 0, 8, 0)
                 [SNew(SBox).MinDesiredWidth(14.0f)
                      [SNew(STextBlock)
                           .Text(FText::FromString(Glyph))
                           .ColorAndOpacity(Color)]] +
         SHorizontalBox::Slot()
             .FillWidth(1.0f)
             .VAlign(VAlign_Center)
                 [SNew(STextBlock)
                      .Text(FText::FromString(Entry.Content))
                      .ColorAndOpacity(Color)
                      .AutoWrapText(true)]];
  }
}

void SACPPlanStrip::Reset() {
  Entries.Reset();
  bExpanded = false;
  bAutoExpandedOnce = false;
  if (BodyContainer.IsValid()) {
    BodyContainer->ClearChildren();
  }
}

FReply SACPPlanStrip::OnHeaderClicked() {
  bExpanded = !bExpanded;
  return FReply::Handled();
}

FText SACPPlanStrip::GetHeaderText() const {
  if (Entries.Num() == 0) {
    return FText::GetEmpty();
  }
  const int32 Done = CountCompleted(Entries);
  return FText::Format(LOCTEXT("PlanHeader", "Plan · {0} of {1} done"),
                       FText::AsNumber(Done),
                       FText::AsNumber(Entries.Num()));
}

EVisibility SACPPlanStrip::GetSelfVisibility() const {
  return Entries.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SACPPlanStrip::GetBodyVisibility() const {
  return (bExpanded && Entries.Num() > 0) ? EVisibility::Visible
                                          : EVisibility::Collapsed;
}

const FSlateBrush *SACPPlanStrip::GetChevronBrush() const {
  // Down chevron when expanded, right when collapsed — matches the standard
  // disclosure-triangle convention used elsewhere in the editor.
  return FAppStyle::Get().GetBrush(bExpanded ? TEXT("Icons.ChevronDown")
                                             : TEXT("Icons.ChevronRight"));
}

#undef LOCTEXT_NAMESPACE
