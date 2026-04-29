#pragma once

#include "ACPTransport.h"
#include "ACPTypes.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "JsonRpcPeer.h"

namespace UAgent {
class FACPToolRegistry;

enum class EClientState : uint8 {
  Disconnected,
  Starting,
  Initializing,
  CreatingSession,
  Ready,
  Prompting,
  Error,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnClientStateChanged,
                                    EClientState /*NewState*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSessionUpdateDelegate,
                                    const FSessionUpdate & /*Update*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPromptCompleted,
                                     EStopReason /*StopReason*/,
                                     FString /*ErrorOrEmpty*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnErrorDelegate,
                                    const FString & /*Message*/);
DECLARE_MULTICAST_DELEGATE(FOnAgentSettingsChanged);

/**
 * High-level ACP client. Drives the ACP state machine:
 *   initialize → session/new → session/prompt
 * and routes agent→client requests through an FACPToolRegistry. Generic
 * JSON-RPC framing (request-id allocation, pending-continuations map,
 * inbound-message classification) lives on an FJsonRpcPeer the client
 * owns; the transport is an IACPTransport supplied by a factory.
 */
class FACPClient {
public:
  using FTransportFactory = TFunction<TSharedRef<IACPTransport>()>;

  FACPClient();
  ~FACPClient();

  /**
   * Set the registry used to dispatch agent→client requests. Must be
   * set before Start() for the initial capabilities advertisement to
   * reflect registered tools.
   */
  void SetToolRegistry(TSharedPtr<FACPToolRegistry> InRegistry) {
    ToolRegistry = MoveTemp(InRegistry);
  }

  /**
   * Override the transport factory (must be called before Start()). The
   * default factory spawns a real subprocess via FACPTransport; tests
   * inject a mock here.
   */
  void SetTransportFactory(FTransportFactory InFactory) {
    TransportFactory = MoveTemp(InFactory);
  }

  /**
   * URL of the in-editor MCP server to advertise to the agent in
   * session/new. Empty string disables advertisement. Must be set before
   * Start(); only takes effect if the agent reports mcp_capabilities.http.
   */
  void SetMcpServerUrl(FString InUrl) { McpServerUrl = MoveTemp(InUrl); }

  /** Starts the agent process and kicks off initialize + session/new. */
  void Start(const FString &AgentCommand, const TArray<FString> &AgentArgs,
             const FString &WorkingDir);

  /** Kills the agent and resets state. */
  void Stop();

  /** Sends a user prompt. Requires state == Ready. Returns false if state check
   * fails. */
  bool SendPrompt(const TArray<FContentBlock> &Blocks);

  /** Sends a session/cancel notification. */
  void CancelPrompt();

  /**
   * Sets one of the agent's advertised config options (model, …). No-op when
   * SessionId is empty. Optimistically updates the local snapshot; the
   * agent's `config_option_update` notification overrides if needed.
   */
  void SetConfigOption(const FString &ConfigId, const FString &Value);

  EClientState GetState() const { return State; }
  const FString &GetSessionId() const { return SessionId; }
  const FString &GetLastError() const { return LastError; }

  /** Latest snapshot of what the agent has advertised. Empty when nothing was
   * advertised (or session not yet created). */
  const TArray<FConfigOption> &GetConfigOptions() const {
    return ConfigOptions;
  }

  FOnClientStateChanged OnStateChanged;
  FOnSessionUpdateDelegate OnSessionUpdate;
  FOnPromptCompleted OnPromptCompleted;
  FOnErrorDelegate OnError;

  /** Fires whenever GetConfigOptions() may have changed — on session/new
   * advertisement, config_option_update, or local optimistic writes via
   * SetConfigOption. */
  FOnAgentSettingsChanged OnAgentSettingsChanged;

private:
  void HandleRequest(const FString &Method,
                     const TSharedPtr<FJsonObject> &Params,
                     const TSharedPtr<FJsonValue> &Id);
  void HandleNotification(const FString &Method,
                          const TSharedPtr<FJsonObject> &Params);

  void OnTransportExit(int32 ExitCode, FString StderrDump);

  void SetState(EClientState NewState);
  void ReportError(const FString &Where, const FString &Message);

  void SendInitialize();
  void SendNewSession();

private:
  TSharedPtr<IACPTransport> Transport;
  TSharedPtr<FACPToolRegistry> ToolRegistry;
  FTransportFactory TransportFactory;
  FJsonRpcPeer Peer;

  EClientState State = EClientState::Disconnected;
  FString SessionId;
  FString LastError;

  FString McpServerUrl;
  bool bAgentSupportsHttpMcp = false;

  TArray<FConfigOption> ConfigOptions;

  // Guard against Transport::Shutdown's synchronous OnExit being mistaken
  // for an unexpected agent crash during an intentional Stop().
  bool bStopping = false;
};
} // namespace UAgent
