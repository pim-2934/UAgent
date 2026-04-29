#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

namespace UAgent {
/** Outcome the user picked in the chat permission card. */
enum class EPermissionOutcome : uint8 {
  Allow,
  Deny,
};

/** Information shown to the user when asking whether to run a tool. */
struct FPermissionRequest {
  /** ACP tool-call id (used to correlate the prompt card with a tool row). */
  FString ToolCallId;
  /** Human-readable title for the tool call. */
  FString ToolTitle;
  /** ACP tool kind (read/write/edit/...). Empty if the agent didn't supply
   * one. */
  FString ToolKind;
  /** Raw arguments the agent wants to pass — surfaced in the card so the user
   * can see what's about to happen. */
  TSharedPtr<FJsonObject> RawToolCall;
};

/**
 * Decouples the synchronous request_permission tool from the async chat-UI
 * Accept/Cancel flow. The chat window installs a handler in Construct() and
 * clears it on destruction; the request_permission tool calls Request() and
 * gets a callback when the user resolves the prompt.
 *
 * Single-handler model — there's one chat tab at a time and stacking multiple
 * UI surfaces would just confuse who answers what. If no handler is installed
 * (chat tab closed), the broker auto-denies so a missing UI never silently
 * approves a mutation.
 */
class FPermissionBroker {
public:
  using FHandler = TFunction<void(const FPermissionRequest &,
                                  TFunction<void(EPermissionOutcome)>)>;

  static FPermissionBroker &Get();

  /** Install (or replace) the handler. Pass an empty TFunction to clear. */
  void SetHandler(FHandler InHandler);

  /** Send a request through the installed handler. If none is installed, the
   * completion fires synchronously with Deny. */
  void Request(const FPermissionRequest &Req,
               TFunction<void(EPermissionOutcome)> Complete);

private:
  FHandler Handler;
};
} // namespace UAgent
