#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SHorizontalBox;

/**
 * Horizontal strip showing context chips above the chat input, plus a "+"
 * button. Renders two visual flavors:
 *  - "auto" chips (currently-open assets, green tint, no remove button) come
 *    from SetAutoChips and are informational — the host decides what the
 *    auto-set is.
 *  - "manual" chips (user-added via @ or +) come from SetManualChips and show
 *    a × remove button that fires OnChipRemoved.
 * The strip has no opinions about how context is built into prompt blocks; it
 * just renders whatever it's told.
 */
class SACPContextStrip : public SCompoundWidget {
public:
  DECLARE_DELEGATE(FOnAddClicked);
  DECLARE_DELEGATE_OneParam(FOnChipRemoved, const FAssetData & /*Asset*/);

  SLATE_BEGIN_ARGS(SACPContextStrip) {}
  SLATE_EVENT(FOnAddClicked, OnAddClicked)
  SLATE_EVENT(FOnChipRemoved, OnChipRemoved)
  SLATE_END_ARGS()

  void Construct(const FArguments &InArgs);

  void SetAutoChips(const TArray<FAssetData> &InChips);
  void SetManualChips(const TArray<FAssetData> &InChips);

private:
  void Rebuild();

  FOnAddClicked OnAddClickedDelegate;
  FOnChipRemoved OnChipRemovedDelegate;

  TSharedPtr<SHorizontalBox> ChipBox;
  TArray<FAssetData> AutoChips;
  TArray<FAssetData> ManualChips;
};
