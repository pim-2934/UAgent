#include "MCPServer.h"

#include "MCPProtocol.h"

#include "../Protocol/ACPTypes.h"

#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "IPAddress.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UAgent {
FMCPServer::FMCPServer() = default;

FMCPServer::~FMCPServer() { Stop(); }

bool FMCPServer::Start(int32 Port, TSharedPtr<FACPToolRegistry> InRegistry) {
  if (BoundPort > 0) {
    UE_LOG(LogUAgent, Warning,
           TEXT("FMCPServer::Start called while already listening on :%d"),
           BoundPort);
    return true;
  }
  if (!InRegistry.IsValid()) {
    UE_LOG(LogUAgent, Error, TEXT("FMCPServer::Start: no registry"));
    return false;
  }

  FHttpServerModule &HttpModule = FHttpServerModule::Get();
  Router = HttpModule.GetHttpRouter(Port, /*bFailOnBindFailure=*/true);
  if (!Router.IsValid()) {
    UE_LOG(LogUAgent, Error,
           TEXT("FMCPServer::Start: GetHttpRouter failed on port %d (port in "
                "use?)"),
           Port);
    return false;
  }

  Protocol = MakeUnique<FMCPProtocol>(InRegistry);

  RouteHandle = Router->BindRoute(
      FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_POST,
      FHttpRequestHandler::CreateRaw(this, &FMCPServer::HandleRequest));

  HttpModule.StartAllListeners();

  BoundPort = Port;
  UE_LOG(LogUAgent, Log,
         TEXT("MCP server listening on http://127.0.0.1:%d/mcp"), BoundPort);
  return true;
}

void FMCPServer::Stop() {
  if (Router.IsValid() && RouteHandle.IsValid()) {
    Router->UnbindRoute(RouteHandle);
  }
  RouteHandle.Reset();
  Router.Reset();
  Protocol.Reset();
  BoundPort = 0;
}

FString FMCPServer::GetEndpointUrl() const {
  return BoundPort > 0
             ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), BoundPort)
             : FString();
}

// UE's FHttpRouter binds 0.0.0.0, so reject every non-loopback peer here to
// keep the tool surface — fs writes, asset deletes, console exec — off the LAN.
static bool IsLoopbackPeer(const TSharedPtr<FInternetAddr> &Addr) {
  if (!Addr.IsValid())
    return false;
  TArray<uint8> Raw = Addr->GetRawIp();
  if (Raw.Num() == 4) {
    return Raw[0] == 127;
  }
  if (Raw.Num() == 16) {
    // ::1
    bool bIsV6Loopback = (Raw[15] == 1);
    for (int32 i = 0; i < 15 && bIsV6Loopback; ++i)
      bIsV6Loopback = (Raw[i] == 0);
    if (bIsV6Loopback)
      return true;
    // ::ffff:127.x.x.x — IPv4-mapped-in-IPv6 loopback
    const bool bMappedV4 = Raw[10] == 0xff && Raw[11] == 0xff && Raw[0] == 0 &&
                           Raw[1] == 0 && Raw[2] == 0 && Raw[3] == 0 &&
                           Raw[4] == 0 && Raw[5] == 0 && Raw[6] == 0 &&
                           Raw[7] == 0 && Raw[8] == 0 && Raw[9] == 0;
    return bMappedV4 && Raw[12] == 127;
  }
  return false;
}

bool FMCPServer::HandleRequest(const FHttpServerRequest &Request,
                               const FHttpResultCallback &OnComplete) {
  auto SendJson = [&](const TSharedRef<FJsonObject> &Envelope) {
    FString Serialized;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
            &Serialized);
    FJsonSerializer::Serialize(Envelope, W);
    OnComplete(
        FHttpServerResponse::Create(Serialized, TEXT("application/json")));
  };

  if (!IsLoopbackPeer(Request.PeerAddress)) {
    const FString PeerStr = Request.PeerAddress.IsValid()
                                ? Request.PeerAddress->ToString(false)
                                : FString(TEXT("?"));
    UE_LOG(LogUAgent, Warning, TEXT("MCP rejected non-loopback peer: %s"),
           *PeerStr);
    TUniquePtr<FHttpServerResponse> Resp =
        FHttpServerResponse::Create(FString(), TEXT("text/plain"));
    Resp->Code = EHttpServerResponseCodes::Denied;
    OnComplete(MoveTemp(Resp));
    return true;
  }

  FString BodyStr;
  if (Request.Body.Num() > 0) {
    FFileHelper::BufferToString(BodyStr, Request.Body.GetData(),
                                Request.Body.Num());
  }

  TSharedPtr<FJsonObject> Msg;
  const TSharedRef<TJsonReader<>> Reader =
      TJsonReaderFactory<>::Create(BodyStr);
  if (!FJsonSerializer::Deserialize(Reader, Msg) || !Msg.IsValid()) {
    SendJson(Protocol->MakeParseError(TEXT("Parse error")));
    return true;
  }

  const TSharedPtr<FJsonObject> Response =
      Protocol->Dispatch(Msg.ToSharedRef());
  if (!Response.IsValid()) {
    // Notification — no response body, 202 Accepted.
    TUniquePtr<FHttpServerResponse> Resp =
        FHttpServerResponse::Create(FString(), TEXT("application/json"));
    Resp->Code = EHttpServerResponseCodes::Accepted;
    OnComplete(MoveTemp(Resp));
    return true;
  }

  SendJson(Response.ToSharedRef());
  return true;
}
} // namespace UAgent
