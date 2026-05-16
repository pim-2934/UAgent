#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

namespace UAgent {
/** Outcome the developer picked on the proposal card. */
enum class EProposalOutcome : uint8 {
  /** Sidecar written; agent should halt. */
  Accepted,
  /** Developer waved the proposal off; agent should continue with what it has.
   */
  Skipped,
  /** Developer cancelled the turn; agent should stop. */
  Cancelled,
};

/** Payload the chat window renders on the proposal card and (on Accept)
 * persists into the proposal sidecar JSON. */
struct FProposalRequest {
  /** Stable per-proposal GUID. Used as the row id and the sidecar's `id`. */
  FString Id;
  /** Snake_case tool name the agent suggests adding (e.g.
   * `set_landscape_layer_weight`). */
  FString Name;
  /** One-line description matching the shape of IACPTool::GetDescription. */
  FString Description;
  /** Why the existing tools don't fit and what improvising would look like.
   * The developer reads this to decide whether to implement. */
  FString WhyNeeded;
  /** Proposed JSON Schema for the new tool's arguments — the same shape
   * IACPTool::GetInputSchema would return. */
  TSharedPtr<FJsonObject> InputSchema;
  /** An example arguments object the agent would pass on the next turn. */
  TSharedPtr<FJsonObject> ExampleCall;
  /** Proposed IsReadOnly() classification; the developer can override. */
  bool bIsReadOnly = false;
};

/**
 * Decouples the proposal tool from the async chat-UI Accept/Skip/Cancel flow.
 * The chat window installs a handler in Construct() and clears it on
 * destruction; the propose_missing_tool tool calls Request() and gets a
 * callback when the developer resolves the card.
 *
 * Single-handler model — there's one chat tab at a time, same as
 * FPermissionBroker. If no handler is installed, the broker auto-cancels so
 * a missing UI never silently halts the agent.
 *
 * The broker also enforces the "do not call propose_missing_tool more than
 * once per turn" rule from the standing instruction. The first Request()
 * after BeginTurn() flips bProposedThisTurn; subsequent Requests in the
 * same turn auto-resolve to Cancelled with a log warning. EndTurn() clears
 * the flag (called by the chat window when a prompt completes or starts).
 */
class FProposalBroker {
public:
  using FHandler = TFunction<void(const FProposalRequest &,
                                  TFunction<void(EProposalOutcome)>)>;

  static FProposalBroker &Get();

  /** Install (or replace) the handler. Pass an empty TFunction to clear. */
  void SetHandler(FHandler InHandler);

  /** Send a proposal through the installed handler. If none is installed —
   * or the per-turn guard already fired — the completion fires synchronously
   * with Cancelled. */
  void Request(const FProposalRequest &Req,
               TFunction<void(EProposalOutcome)> Complete);

  /** Reset the per-turn guard. Called by the chat window from OnSendClicked
   * (start of a turn) and OnPromptDone (end of a turn) — either is enough on
   * its own; calling both is defensive and cheap. */
  void ResetTurn();

private:
  FHandler Handler;
  bool bProposedThisTurn = false;
};
} // namespace UAgent
