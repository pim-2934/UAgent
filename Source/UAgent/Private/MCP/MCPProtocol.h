#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

namespace UAgent {
class FACPToolRegistry;

/**
 * Transport-agnostic MCP (Model Context Protocol) handler. Processes a
 * single parsed JSON-RPC message against a shared FACPToolRegistry and
 * invokes a completion callback with the response envelope — or with null
 * for notifications.
 *
 * Dispatch is callback-shaped (not return-shaped) so tools that need to
 * defer (e.g. propose_missing_tool, which waits on a chat-UI proposal
 * card) can complete their tools/call response after the dispatch frame
 * unwinds. Synchronous tools fire the callback inline; the transport
 * doesn't need to care which case applies.
 *
 * Exposes only the subset of registry entries whose method name starts
 * with "_ue5/"; the prefix is stripped for the MCP-facing name. fs/* and
 * session/* stay off the MCP surface because they're ACP-native.
 *
 * Deliberately has no knowledge of HTTP, stdio framing, or sockets — owner
 * code (e.g. FMCPServer) handles that and feeds fully-parsed JSON in.
 */
class FMCPProtocol {
public:
  using FResponseCallback = TFunction<void(TSharedPtr<FJsonObject>)>;

  explicit FMCPProtocol(TSharedPtr<FACPToolRegistry> InRegistry);

  /**
   * Dispatch a single inbound message. Invokes OnResponse with the
   * response JSON for requests (success or JSON-RPC error), or with null
   * for notifications so the transport can reply with an empty-body
   * status (e.g. HTTP 202). May fire OnResponse synchronously (the common
   * case) or asynchronously (deferred tools).
   */
  void Dispatch(const TSharedRef<FJsonObject> &Msg,
                FResponseCallback OnResponse);

  /** Convenience: JSON-RPC "Parse error" (-32700) envelope with null id. */
  TSharedRef<FJsonObject> MakeParseError(const FString &Message) const;

private:
  TSharedRef<FJsonObject> HandleInitialize(const TSharedPtr<FJsonValue> &Id);
  TSharedRef<FJsonObject> HandleToolsList(const TSharedPtr<FJsonValue> &Id);
  void HandleToolsCall(const TSharedPtr<FJsonValue> &Id,
                       const TSharedPtr<FJsonObject> &Params,
                       FResponseCallback OnResponse);

  TSharedRef<FJsonObject>
  MakeResult(const TSharedPtr<FJsonValue> &Id,
             const TSharedRef<FJsonObject> &Result) const;
  TSharedRef<FJsonObject> MakeError(const TSharedPtr<FJsonValue> &Id,
                                    int32 Code, const FString &Message) const;

  TSharedPtr<FACPToolRegistry> Registry;
};
} // namespace UAgent
