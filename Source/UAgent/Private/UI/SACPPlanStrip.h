#pragma once

#include "CoreMinimal.h"
#include "Protocol/ACPTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;

/**
 * Sticky strip that surfaces the backing agent's current plan
 * (`session/update` kind=plan) without inserting rows into the chat log.
 * Header is a click-to-toggle button showing "Plan · X of Y done". Body
 * renders one row per FPlanEntry with a status glyph (○/◐/✓) and the
 * entry text. Auto-expands once per session the first time a non-empty
 * plan arrives; afterwards the user controls expansion.
 *
 * Visibility is internally managed — empty plan collapses the whole
 * widget so the host can slot it unconditionally and forget about it.
 */
class SACPPlanStrip : public SCompoundWidget {
public:
  SLATE_BEGIN_ARGS(SACPPlanStrip) {}
  SLATE_END_ARGS()

  void Construct(const FArguments &InArgs);

  /**
   * Replace the plan in full (per spec, agents send the full list each
   * update). An empty array hides the strip; first non-empty plan
   * auto-expands. Subsequent updates keep the user's expand/collapse choice.
   */
  void SetPlan(const TArray<UAgent::FPlanEntry> &InEntries);

  /** Forget the plan and the once-per-session auto-expand state. Call on
   * fresh session start / loaded session to avoid carrying over a flag
   * from the previous one. */
  void Reset();

private:
  TSharedRef<SWidget> BuildBody();
  FReply OnHeaderClicked();
  FText GetHeaderText() const;
  EVisibility GetSelfVisibility() const;
  EVisibility GetBodyVisibility() const;
  const FSlateBrush *GetChevronBrush() const;

  TArray<UAgent::FPlanEntry> Entries;
  TSharedPtr<SVerticalBox> BodyContainer;

  bool bExpanded = false;
  bool bAutoExpandedOnce = false;
};
