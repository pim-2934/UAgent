#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

namespace UAgent {
class IACPTransport;

/**
 * Generic JSON-RPC 2.0 peer over any IACPTransport. Owns request-id
 * allocation, the continuation map for in-flight requests, and the
 * classification of inbound messages (request vs response vs notification).
 *
 * The ACP state machine — initialize / session/new / session/prompt — lives
 * one layer up in FACPClient; this class is protocol-method-agnostic and
 * could just as easily be reused for any other stdio JSON-RPC peer.
 */
class FJsonRpcPeer {
public:
  using FResponseContinuation =
      TFunction<void(const TSharedPtr<FJsonObject> & /*Result*/,
                     const TSharedPtr<FJsonObject> & /*Error*/)>;
  using FRequestHandler = TFunction<void(
      const FString & /*Method*/, const TSharedPtr<FJsonObject> & /*Params*/,
      const TSharedPtr<FJsonValue> & /*Id*/)>;
  using FNotificationHandler = TFunction<void(
      const FString & /*Method*/, const TSharedPtr<FJsonObject> & /*Params*/)>;

  FJsonRpcPeer();

  /**
   * Bind the peer to a transport. The peer hooks OnMessage on the
   * transport; it does not take ownership. Safe to call with nullptr to
   * unbind.
   */
  void BindTransport(TSharedPtr<IACPTransport> InTransport);

  /** Clears pending continuations and the next-id counter. */
  void Reset();

  void SetRequestHandler(FRequestHandler InHandler) {
    OnRequest = MoveTemp(InHandler);
  }
  void SetNotificationHandler(FNotificationHandler InHandler) {
    OnNotification = MoveTemp(InHandler);
  }

  /** Send a request; the continuation (if set) fires when a response arrives.
   */
  void SendRequest(const FString &Method, const TSharedRef<FJsonObject> &Params,
                   FResponseContinuation Continuation = {});

  void SendNotification(const FString &Method,
                        const TSharedRef<FJsonObject> &Params);

  /** Id may be numeric or string — pass through whatever the inbound request
   * used. */
  void SendResponse(const TSharedPtr<FJsonValue> &Id,
                    const TSharedRef<FJsonObject> &Result);
  void SendErrorResponse(const TSharedPtr<FJsonValue> &Id, int32 Code,
                         const FString &Message);

private:
  void HandleIncoming(TSharedRef<FJsonObject> Message);
  void HandleResponse(int32 Id, const TSharedPtr<FJsonObject> &Result,
                      const TSharedPtr<FJsonObject> &Error);

  TSharedPtr<IACPTransport> Transport;
  FRequestHandler OnRequest;
  FNotificationHandler OnNotification;
  int32 NextRequestId = 0;
  TMap<int32, FResponseContinuation> Pending;
};
} // namespace UAgent
