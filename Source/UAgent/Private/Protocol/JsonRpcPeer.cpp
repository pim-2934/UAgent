#include "JsonRpcPeer.h"

#include "ACPTransport.h"
#include "ACPTypes.h"

namespace UAgent {
FJsonRpcPeer::FJsonRpcPeer() = default;

void FJsonRpcPeer::BindTransport(TSharedPtr<IACPTransport> InTransport) {
  Transport = MoveTemp(InTransport);
  if (Transport.IsValid()) {
    Transport->OnMessage.BindRaw(this, &FJsonRpcPeer::HandleIncoming);
  }
}

void FJsonRpcPeer::Reset() {
  Pending.Reset();
  NextRequestId = 0;
}

void FJsonRpcPeer::SendRequest(const FString &Method,
                               const TSharedRef<FJsonObject> &Params,
                               FResponseContinuation Continuation) {
  if (!Transport.IsValid())
    return;

  const int32 Id = ++NextRequestId;
  if (Continuation) {
    Pending.Add(Id, MoveTemp(Continuation));
  }

  TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
  Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
  Msg->SetNumberField(TEXT("id"), Id);
  Msg->SetStringField(TEXT("method"), Method);
  Msg->SetObjectField(TEXT("params"), Params);

  Transport->Send(Msg);
}

void FJsonRpcPeer::SendNotification(const FString &Method,
                                    const TSharedRef<FJsonObject> &Params) {
  if (!Transport.IsValid())
    return;
  TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
  Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
  Msg->SetStringField(TEXT("method"), Method);
  Msg->SetObjectField(TEXT("params"), Params);
  Transport->Send(Msg);
}

void FJsonRpcPeer::SendResponse(const TSharedPtr<FJsonValue> &Id,
                                const TSharedRef<FJsonObject> &Result) {
  if (!Transport.IsValid() || !Id.IsValid())
    return;
  TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
  Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
  Msg->SetField(TEXT("id"), Id);
  Msg->SetObjectField(TEXT("result"), Result);
  Transport->Send(Msg);
}

void FJsonRpcPeer::SendErrorResponse(const TSharedPtr<FJsonValue> &Id,
                                     int32 Code, const FString &Message) {
  if (!Transport.IsValid() || !Id.IsValid())
    return;
  TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
  Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
  Msg->SetField(TEXT("id"), Id);
  TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
  Err->SetNumberField(TEXT("code"), Code);
  Err->SetStringField(TEXT("message"), Message);
  Msg->SetObjectField(TEXT("error"), Err);
  Transport->Send(Msg);
}

void FJsonRpcPeer::HandleIncoming(TSharedRef<FJsonObject> Message) {
  const bool bHasMethod = Message->HasField(TEXT("method"));
  const bool bHasId = Message->HasField(TEXT("id"));

  if (bHasMethod && bHasId) {
    FString Method;
    Message->TryGetStringField(TEXT("method"), Method);
    const TSharedPtr<FJsonValue> Id = Message->TryGetField(TEXT("id"));
    const TSharedPtr<FJsonObject> *ParamsObj = nullptr;
    Message->TryGetObjectField(TEXT("params"), ParamsObj);
    const TSharedPtr<FJsonObject> Params =
        ParamsObj ? *ParamsObj : TSharedPtr<FJsonObject>();
    if (OnRequest) {
      OnRequest(Method, Params, Id);
    } else {
      SendErrorResponse(
          Id, -32601,
          FString::Printf(TEXT("no handler for method '%s'"), *Method));
    }
    return;
  }

  if (bHasMethod && !bHasId) {
    FString Method;
    Message->TryGetStringField(TEXT("method"), Method);
    const TSharedPtr<FJsonObject> *ParamsObj = nullptr;
    Message->TryGetObjectField(TEXT("params"), ParamsObj);
    if (OnNotification) {
      OnNotification(Method,
                     ParamsObj ? *ParamsObj : TSharedPtr<FJsonObject>());
    }
    return;
  }

  if (bHasId) {
    int32 Id = 0;
    Message->TryGetNumberField(TEXT("id"), Id);
    const TSharedPtr<FJsonObject> *ResultObj = nullptr;
    const TSharedPtr<FJsonObject> *ErrorObj = nullptr;
    Message->TryGetObjectField(TEXT("result"), ResultObj);
    Message->TryGetObjectField(TEXT("error"), ErrorObj);
    HandleResponse(Id, ResultObj ? *ResultObj : nullptr,
                   ErrorObj ? *ErrorObj : nullptr);
    return;
  }

  UE_LOG(LogUAgent, Warning,
         TEXT("JsonRpcPeer: malformed message (no method, no id): dropping"));
}

void FJsonRpcPeer::HandleResponse(int32 Id,
                                  const TSharedPtr<FJsonObject> &Result,
                                  const TSharedPtr<FJsonObject> &Error) {
  FResponseContinuation *It = Pending.Find(Id);
  if (!It) {
    UE_LOG(LogUAgent, Warning, TEXT("JsonRpcPeer: response for unknown id=%d"),
           Id);
    return;
  }
  FResponseContinuation Fn = MoveTemp(*It);
  Pending.Remove(Id);
  Fn(Result, Error);
}
} // namespace UAgent
