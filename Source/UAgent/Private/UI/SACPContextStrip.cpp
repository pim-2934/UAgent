#include "SACPContextStrip.h"

#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UAgent"

void SACPContextStrip::Construct(const FArguments &InArgs) {
  OnAddClickedDelegate = InArgs._OnAddClicked;
  OnChipRemovedDelegate = InArgs._OnChipRemoved;

  ChildSlot[SNew(SBorder)
                .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(
                    4))[SNew(SHorizontalBox) +
                        SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            .Padding(0, 0, 4, 0)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("ContextLabel", "Context:"))
                                     .ColorAndOpacity(FLinearColor(
                                         0.65f, 0.65f, 0.65f, 1.0f))] +
                        SHorizontalBox::Slot().FillWidth(
                            1.0f)[SAssignNew(ChipBox, SHorizontalBox)] +
                        SHorizontalBox::Slot().AutoWidth()
                            [SNew(SButton)
                                 .Text(LOCTEXT("AddContext", "+"))
                                 .ToolTipText(LOCTEXT("AddContextTooltip",
                                                      "Add a file to context"))
                                 .OnClicked_Lambda([this]() {
                                   OnAddClickedDelegate.ExecuteIfBound();
                                   return FReply::Handled();
                                 })]]];

  Rebuild();
}

void SACPContextStrip::SetAutoChips(const TArray<FAssetData> &InChips) {
  AutoChips = InChips;
  Rebuild();
}

void SACPContextStrip::SetManualChips(const TArray<FAssetData> &InChips) {
  ManualChips = InChips;
  Rebuild();
}

void SACPContextStrip::Rebuild() {
  if (!ChipBox.IsValid())
    return;
  ChipBox->ClearChildren();

  for (const FAssetData &A : AutoChips) {
    ChipBox->AddSlot().AutoWidth().Padding(
        2,
        0)[SNew(SBorder)
               .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
               .Padding(FMargin(
                   4,
                   2))[SNew(STextBlock)
                           .Text(FText::FromName(A.AssetName))
                           .ColorAndOpacity(FLinearColor(0.5f, 0.8f, 0.5f))
                           .ToolTipText(LOCTEXT(
                               "AutoChipTip", "Open asset (auto-included)"))]];
  }

  for (const FAssetData &Asset : ManualChips) {
    ChipBox->AddSlot().AutoWidth().Padding(
        2,
        0)[SNew(SBorder)
               .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
               .Padding(FMargin(4, 2))
                   [SNew(SHorizontalBox) +
                    SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock)
                             .Text(FText::FromName(Asset.AssetName))
                             .ColorAndOpacity(FLinearColor(0.6f, 0.8f, 1.0f))] +
                    SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        .Padding(4, 0, 0,
                                 0)[SNew(SButton)
                                        .Text(LOCTEXT("ChipClose", "×"))
                                        .OnClicked_Lambda([this, Asset]() {
                                          OnChipRemovedDelegate.ExecuteIfBound(
                                              Asset);
                                          return FReply::Handled();
                                        })]]];
  }
}

#undef LOCTEXT_NAMESPACE
