#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"

namespace UAgent {
class FACPToolRegistry;

/**
 * Transport-agnostic MCP (Model Context Protocol) handler. Processes a
 * single parsed JSON-RPC message against a shared FACPToolRegistry and
 * returns the response envelope — or null for notifications.
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
  explicit FMCPProtocol(TSharedPtr<FACPToolRegistry> InRegistry);

  /**
   * Dispatch a single inbound message. Returns the response JSON for
   * requests (success or JSON-RPC error); returns null for notifications
   * so the transport can reply with an empty-body status (e.g. HTTP 202).
   */
  TSharedPtr<FJsonObject> Dispatch(const TSharedRef<FJsonObject> &Msg);

  /** Convenience: JSON-RPC "Parse error" (-32700) envelope with null id. */
  TSharedRef<FJsonObject> MakeParseError(const FString &Message) const;

private:
  TSharedRef<FJsonObject> HandleInitialize(const TSharedPtr<FJsonValue> &Id);
  TSharedRef<FJsonObject> HandleToolsList(const TSharedPtr<FJsonValue> &Id);
  TSharedRef<FJsonObject>
  HandleToolsCall(const TSharedPtr<FJsonValue> &Id,
                  const TSharedPtr<FJsonObject> &Params);

  TSharedRef<FJsonObject>
  MakeResult(const TSharedPtr<FJsonValue> &Id,
             const TSharedRef<FJsonObject> &Result) const;
  TSharedRef<FJsonObject> MakeError(const TSharedPtr<FJsonValue> &Id,
                                    int32 Code, const FString &Message) const;

  TSharedPtr<FACPToolRegistry> Registry;
};
} // namespace UAgent
