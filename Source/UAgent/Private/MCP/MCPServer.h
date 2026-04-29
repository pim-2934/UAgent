#pragma once

#include "CoreMinimal.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class IHttpRouter;
struct FHttpServerRequest;

namespace UAgent {
class FACPToolRegistry;
class FMCPProtocol;

/**
 * HTTP adapter for the MCP protocol. Binds a route on 127.0.0.1:<port>/mcp
 * and feeds parsed request bodies into an owned FMCPProtocol; serializes
 * the response back over HTTP. Delegates all semantic decisions to the
 * protocol class — swap this for an IPC adapter and the protocol code is
 * untouched.
 */
class FMCPServer {
public:
  FMCPServer();
  ~FMCPServer();

  bool Start(int32 Port, TSharedPtr<FACPToolRegistry> InRegistry);
  void Stop();

  bool IsRunning() const { return BoundPort > 0; }
  int32 GetPort() const { return BoundPort; }

  /** Returns "http://127.0.0.1:<port>/mcp" or empty if not running. */
  FString GetEndpointUrl() const;

private:
  bool HandleRequest(const FHttpServerRequest &Request,
                     const FHttpResultCallback &OnComplete);

  TUniquePtr<FMCPProtocol> Protocol;
  TSharedPtr<IHttpRouter> Router;
  FHttpRouteHandle RouteHandle;
  int32 BoundPort = 0;
};
} // namespace UAgent
