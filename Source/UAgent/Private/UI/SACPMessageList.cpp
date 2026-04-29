#include "SACPMessageList.h"

#include "ChatMarkdown.h"

#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "UAgent"

// Bodies of Agent and Tool rows mutate over the course of a turn (streaming
// chunks; tool status transitions pending→in_progress→completed). The row is
// generated once by SListView and kept alive by the list's virtualization;
// lambdas capturing `Item` let STextBlock re-read the latest values each paint.
TSharedRef<SWidget>
SACPMessageList::MakeMessageRow(const FACPChatMessageItemRef &Item) {
  // Permission prompts have a fully separate layout — bail out to the
  // dedicated builder so this method stays focused on the
  // User/Agent/Tool/System shape that share the label-and-body skeleton.
  if (Item->Role == FACPChatMessageItem::ERole::Permission) {
    return MakePermissionRow(Item);
  }

  FLinearColor BorderColor;
  switch (Item->Role) {
  case FACPChatMessageItem::ERole::User:
    BorderColor = FLinearColor(0.12f, 0.22f, 0.40f, 1.0f);
    break;
  case FACPChatMessageItem::ERole::Agent:
    BorderColor = FLinearColor(0.10f, 0.10f, 0.13f, 1.0f);
    break;
  case FACPChatMessageItem::ERole::Tool:
    BorderColor = FLinearColor(0.20f, 0.14f, 0.04f, 1.0f);
    break;
  case FACPChatMessageItem::ERole::System:
    BorderColor = FLinearColor(0.30f, 0.10f, 0.10f, 1.0f);
    break;
  case FACPChatMessageItem::ERole::Permission:
    // unreachable — handled by early return above.
    BorderColor = FLinearColor::Black;
    break;
  }

  auto LabelLambda = [Item]() {
    switch (Item->Role) {
    case FACPChatMessageItem::ERole::User:
      return LOCTEXT("UserLabel", "you");
    case FACPChatMessageItem::ERole::Agent:
      return LOCTEXT("AgentLabel", "agent");
    case FACPChatMessageItem::ERole::Tool:
      return FText::Format(LOCTEXT("ToolLabel", "tool · {0}"),
                           FText::FromString(Item->ToolCallTitle.IsEmpty()
                                                 ? Item->ToolCallId
                                                 : Item->ToolCallTitle));
    case FACPChatMessageItem::ERole::System:
      return LOCTEXT("SystemLabel", "system");
    }
    return FText::GetEmpty();
  };

  TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

  if (Item->Role == FACPChatMessageItem::ERole::Tool) {
    const FString CallId = Item->ToolCallId;

    TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);

    Header->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
        [SNew(STextBlock)
             .Text_Lambda(LabelLambda)
             .ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
             .Font(FAppStyle::Get().GetFontStyle(TEXT("BoldFont")))
             .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
             .AutoWrapText_Lambda(
                 [this, CallId]() { return IsToolExpanded(CallId); })];

    Header->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(FMargin(
            4, 0))[SNew(SImage)
                       .DesiredSizeOverride(FVector2D(16, 16))
                       .Image_Lambda([Item]() -> const FSlateBrush * {
                         const FString &S = Item->ToolCallStatus;
                         if (S == TEXT("completed"))
                           return FAppStyle::Get().GetBrush(
                               "Icons.SuccessWithColor");
                         if (S == TEXT("failed"))
                           return FAppStyle::Get().GetBrush(
                               "Icons.ErrorWithColor");
                         return FAppStyle::Get().GetBrush("Icons.Warning");
                       })];

    Header->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [SNew(SButton)
             .ButtonStyle(FAppStyle::Get(), "NoBorder")
             .ContentPadding(FMargin(2))
             .OnClicked_Lambda([this, CallId]() -> FReply {
               bool &Expanded = ToolExpandedById.FindOrAdd(CallId, false);
               Expanded = !Expanded;
               if (ListView.IsValid())
                 ListView->RequestListRefresh();
               return FReply::Handled();
             })[SNew(SImage)
                    .DesiredSizeOverride(FVector2D(16, 16))
                    .Image_Lambda([this, CallId]() -> const FSlateBrush * {
                      return IsToolExpanded(CallId) ? FAppStyle::Get().GetBrush(
                                                          "Icons.ChevronDown")
                                                    : FAppStyle::Get().GetBrush(
                                                          "Icons.ChevronRight");
                    })]];

    Body->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[Header];

    Body->AddSlot().AutoHeight()[SNew(STextBlock)
                                     .Text_Lambda([Item]() {
                                       return FText::Format(
                                           LOCTEXT("ToolStatus", "[{0}]"),
                                           FText::FromString(
                                               Item->ToolCallStatus));
                                     })
                                     .ColorAndOpacity(FLinearColor(0.85f, 0.75f,
                                                                   0.45f, 1.0f))
                                     .Visibility_Lambda([this, CallId]() {
                                       return IsToolExpanded(CallId)
                                                  ? EVisibility::Visible
                                                  : EVisibility::Collapsed;
                                     })];
  } else {
    Body->AddSlot().AutoHeight().Padding(
        0, 0, 0, 4)[SNew(STextBlock)
                        .Text_Lambda(LabelLambda)
                        .ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
                        .Font(FAppStyle::Get().GetFontStyle(TEXT("BoldFont")))];
  }

  FTextBlockStyle BodyTextStyle =
      FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
  if (Item->Tint != FLinearColor::White) {
    BodyTextStyle.SetColorAndOpacity(FSlateColor(Item->Tint));
  }

  TSharedRef<SWidget> BodyWidget = SNullWidget::NullWidget;
  if (Item->Role == FACPChatMessageItem::ERole::Agent && Item->bTurnComplete) {
    // Turn is settled — parse markdown once into a block tree.
    BodyWidget = UAgent::ChatMarkdown::BuildWidget(Item->Text);
  } else {
    // Streaming agent, or user/tool/system: keep the cheap lambda-bound
    // text so live updates don't regenerate the widget tree.
    TSharedRef<SMultiLineEditableText> EditText =
        SNew(SMultiLineEditableText)
            .IsReadOnly(true)
            .AutoWrapText(true)
            .TextStyle(&BodyTextStyle)
            .Text_Lambda([Item]() { return FText::FromString(Item->Text); });

    if (Item->Role == FACPChatMessageItem::ERole::Tool) {
      const FString CallId = Item->ToolCallId;
      EditText->SetVisibility(
          TAttribute<EVisibility>::CreateLambda([this, CallId]() {
            return IsToolExpanded(CallId) ? EVisibility::Visible
                                          : EVisibility::Collapsed;
          }));
    }
    BodyWidget = EditText;
  }

  Body->AddSlot().AutoHeight()[BodyWidget];

  if (Item->Role == FACPChatMessageItem::ERole::User &&
      Item->Contexts.Num() > 0) {
    TSharedRef<SWrapBox> ContextWrap = SNew(SWrapBox).UseAllottedSize(true);
    for (const FAssetData &A : Item->Contexts) {
      ContextWrap->AddSlot().Padding(
          2,
          2)[SNew(SBorder)
                 .BorderImage(
                     FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
                 .Padding(FMargin(4, 2))[SNew(STextBlock)
                                             .Text(FText::FromName(A.AssetName))
                                             .ColorAndOpacity(FLinearColor(
                                                 0.6f, 0.8f, 1.0f, 1.0f))]];
    }
    Body->AddSlot().AutoHeight().Padding(0, 4, 0, 0)[ContextWrap];
  }

  return SNew(SBorder)
      .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
      .BorderBackgroundColor(BorderColor)
      .Padding(FMargin(8))[Body];
}

// Permission card: header label, tool title, JSON args preview, and a
// resolved-state line that swaps in for the Accept/Cancel buttons once the
// user clicks. The PermissionId captured into the OnClicked lambdas is the
// stable per-row UUID assigned in FChatMessageLog::AppendPermission — it
// keeps button clicks correlated with the right pending broker callback
// even if the log is later compacted or re-ordered.
TSharedRef<SWidget>
SACPMessageList::MakePermissionRow(const FACPChatMessageItemRef &Item) {
  const FString PermissionId = Item->PermissionId;
  const FLinearColor BorderColor(0.30f, 0.20f, 0.05f, 1.0f);

  TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

  Body->AddSlot().AutoHeight().Padding(
      0, 0, 0,
      4)[SNew(STextBlock)
             .Text(NSLOCTEXT("UAgent", "PermLabel", "permission requested"))
             .ColorAndOpacity(FLinearColor(0.85f, 0.75f, 0.45f, 1.0f))
             .Font(FAppStyle::Get().GetFontStyle(TEXT("BoldFont")))];

  Body->AddSlot().AutoHeight().Padding(
      0, 0, 0, 2)[SNew(STextBlock)
                      .Text_Lambda([Item]() {
                        return FText::FromString(Item->ToolCallTitle.IsEmpty()
                                                     ? TEXT("(unknown tool)")
                                                     : Item->ToolCallTitle);
                      })
                      .Font(FAppStyle::Get().GetFontStyle(TEXT("BoldFont")))];

  Body->AddSlot().AutoHeight().Padding(
      0, 0, 0, 4)[SNew(STextBlock)
                      .Text_Lambda([Item]() {
                        if (Item->PermissionArgsPreview.IsEmpty())
                          return FText::GetEmpty();
                        return FText::FromString(Item->PermissionArgsPreview);
                      })
                      .ColorAndOpacity(FLinearColor(0.65f, 0.65f, 0.65f, 1.0f))
                      .AutoWrapText(true)];

  // Buttons row, hidden once the user resolves the prompt.
  TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
  Buttons->AddSlot().AutoWidth().Padding(
      0, 0, 6, 0)[SNew(SButton)
                      .Text(NSLOCTEXT("UAgent", "PermAccept", "Accept"))
                      .OnClicked_Lambda([this, PermissionId]() -> FReply {
                        OnPermissionDecided.ExecuteIfBound(PermissionId,
                                                           /*bAllow=*/true);
                        return FReply::Handled();
                      })];
  Buttons->AddSlot()
      .AutoWidth()[SNew(SButton)
                       .Text(NSLOCTEXT("UAgent", "PermCancel", "Cancel"))
                       .OnClicked_Lambda([this, PermissionId]() -> FReply {
                         OnPermissionDecided.ExecuteIfBound(PermissionId,
                                                            /*bAllow=*/false);
                         return FReply::Handled();
                       })];

  Body->AddSlot().AutoHeight()[SNew(SBox).Visibility_Lambda([Item]() {
    return Item->PermissionState ==
                   FACPChatMessageItem::EPermissionState::Pending
               ? EVisibility::Visible
               : EVisibility::Collapsed;
  })[Buttons]];

  Body->AddSlot().AutoHeight()
      [SNew(STextBlock)
           .Text_Lambda([Item]() {
             switch (Item->PermissionState) {
             case FACPChatMessageItem::EPermissionState::Allowed:
               return NSLOCTEXT("UAgent", "PermAllowed", "✓ Allowed by user");
             case FACPChatMessageItem::EPermissionState::Denied:
               return NSLOCTEXT("UAgent", "PermDenied", "✗ Cancelled by user");
             default:
               return FText::GetEmpty();
             }
           })
           .ColorAndOpacity_Lambda([Item]() {
             return Item->PermissionState ==
                            FACPChatMessageItem::EPermissionState::Allowed
                        ? FLinearColor(0.5f, 0.85f, 0.5f, 1.0f)
                        : FLinearColor(0.9f, 0.6f, 0.4f, 1.0f);
           })
           .Visibility_Lambda([Item]() {
             return Item->PermissionState ==
                            FACPChatMessageItem::EPermissionState::Pending
                        ? EVisibility::Collapsed
                        : EVisibility::Visible;
           })];

  return SNew(SBorder)
      .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
      .BorderBackgroundColor(BorderColor)
      .Padding(FMargin(8))[Body];
}

void SACPMessageList::Construct(const FArguments &InArgs,
                                TSharedRef<FChatMessageLog> InLog) {
  Log = InLog;
  OnPermissionDecided = InArgs._OnPermissionDecided;

  SAssignNew(ListView, SListView<FACPChatMessageItemRef>)
      .ListItemsSource(&Log->GetMessages())
      .OnGenerateRow(this, &SACPMessageList::OnGenerateRow)
      .OnListViewScrolled(this, &SACPMessageList::OnListScrolled)
      .SelectionMode(ESelectionMode::None);

  ChildSlot[SNew(SBorder)
                .BorderImage(
                    FAppStyle::Get().GetBrush("ToolPanel.DarkGroupBorder"))
                .Padding(FMargin(4))[ListView.ToSharedRef()]];

  // AddSP auto-unbinds when this widget dies, so no manual teardown.
  Log->OnChanged.AddSP(this, &SACPMessageList::HandleLogChanged);
  Log->OnAgentTurnEnded.AddSP(this, &SACPMessageList::HandleAgentTurnEnded);
}

TSharedRef<ITableRow>
SACPMessageList::OnGenerateRow(FACPChatMessageItemRef Item,
                               const TSharedRef<STableViewBase> &Owner) {
  return SNew(STableRow<FACPChatMessageItemRef>, Owner)
      .ShowSelection(false)
      .Padding(FMargin(0, 4))[MakeMessageRow(Item)];
}

bool SACPMessageList::IsToolExpanded(const FString &ToolCallId) const {
  const bool *V = ToolExpandedById.Find(ToolCallId);
  return V ? *V : false;
}

void SACPMessageList::HandleAgentTurnEnded() {
  // The agent body widget tree changes shape on turn end (streaming text ->
  // parsed markdown blocks), and SListView reuses already-generated rows, so
  // nudge it to regenerate them. The swapped-in markdown tree is typically
  // taller than the plain editable text it replaces, so re-pin to the bottom
  // after geometry settles.
  if (ListView.IsValid()) {
    ListView->RebuildList();
    ScheduleScrollCatchup();
  }
}

void SACPMessageList::HandleLogChanged() {
  if (!ListView.IsValid() || !Log.IsValid())
    return;
  const TArray<FACPChatMessageItemRef> &Items = Log->GetMessages();
  if (Items.Num() == 0) {
    ToolExpandedById.Reset();
  }
  ListView->RequestListRefresh();
  if (Items.Num() > 0) {
    ScheduleScrollCatchup();
  }
}

void SACPMessageList::OnListScrolled(double /*InScrollOffsetInItems*/) {
  if (!ListView.IsValid())
    return;
  // DistanceFromBottom is normalized 0..1; 0 = at bottom. Stay pinned while
  // the user is essentially at the bottom (covers our own programmatic
  // ScrollToBottom calls), and unpin as soon as they move away.
  const FVector2D Remaining = ListView->GetScrollDistanceRemaining();
  bPinToBottom = Remaining.Y <= 0.001f;
}

void SACPMessageList::ScheduleScrollCatchup() {
  if (!bPinToBottom)
    return;
  // Re-command the scroll across several frames: on the first post-rebuild
  // tick, SListView may only have placeholder heights for regenerated rows,
  // so a single ScrollToBottom can leave us above the real bottom once the
  // markdown block tree finishes measuring.
  ScrollCatchupFramesLeft = 4;
  if (!ScrollCatchupHandle.IsValid()) {
    ScrollCatchupHandle = RegisterActiveTimer(
        0.0f, FWidgetActiveTimerDelegate::CreateSP(
                  this, &SACPMessageList::TickScrollCatchup));
  }
}

EActiveTimerReturnType SACPMessageList::TickScrollCatchup(double, float) {
  if (ListView.IsValid() && bPinToBottom) {
    ListView->ScrollToBottom();
  }
  if (--ScrollCatchupFramesLeft <= 0) {
    ScrollCatchupHandle.Reset();
    return EActiveTimerReturnType::Stop;
  }
  return EActiveTimerReturnType::Continue;
}

#undef LOCTEXT_NAMESPACE
