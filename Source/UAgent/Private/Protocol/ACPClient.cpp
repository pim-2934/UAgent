#include "ACPClient.h"
#include "ACPToolRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

namespace UAgent {
FACPClient::FACPClient()
    : TransportFactory([]() -> TSharedRef<IACPTransport> {
        return MakeShared<FACPTransport>();
      }) {
  Peer.SetRequestHandler([this](const FString &Method,
                                const TSharedPtr<FJsonObject> &Params,
                                const TSharedPtr<FJsonValue> &Id) {
    HandleRequest(Method, Params, Id);
  });
  Peer.SetNotificationHandler(
      [this](const FString &Method, const TSharedPtr<FJsonObject> &Params) {
        HandleNotification(Method, Params);
      });
}

FACPClient::~FACPClient() { Stop(); }

void FACPClient::Start(const FString &AgentCommand,
                       const TArray<FString> &AgentArgs,
                       const FString &WorkingDir) {
  Stop();

  Transport = TransportFactory();
  Transport->OnExit.BindRaw(this, &FACPClient::OnTransportExit);
  Peer.BindTransport(Transport);

  SetState(EClientState::Starting);
  if (!Transport->Start(AgentCommand, AgentArgs, WorkingDir)) {
    ReportError(TEXT("Start"),
                FString::Printf(TEXT("failed to launch '%s'"), *AgentCommand));
    Transport.Reset();
    Peer.BindTransport(nullptr);
    return;
  }

  SendInitialize();
}

void FACPClient::Stop() {
  bStopping = true;
  if (Transport) {
    Transport->Shutdown();
    Transport.Reset();
  }
  Peer.Reset();
  SessionId.Reset();
  ConfigOptions.Reset();
  OnAgentSettingsChanged.Broadcast();
  SetState(EClientState::Disconnected);
  bStopping = false;
}

void FACPClient::OnTransportExit(int32 ExitCode, FString StderrDump) {
  UE_LOG(LogUAgent, Warning, TEXT("Agent exited with code %d. Stderr tail: %s"),
         ExitCode, *StderrDump.Right(1024));
  if (bStopping || State == EClientState::Disconnected) {
    return;
  }
  const FString Tail = StderrDump.Right(512).TrimStartAndEnd();
  const FString Msg =
      Tail.IsEmpty() ? FString::Printf(TEXT("agent exited (code %d)"), ExitCode)
                     : FString::Printf(TEXT("agent exited (code %d): %s"),
                                       ExitCode, *Tail);
  ReportError(TEXT("AgentExit"), Msg);
}

void FACPClient::SetState(EClientState NewState) {
  if (State == NewState)
    return;
  State = NewState;
  OnStateChanged.Broadcast(NewState);
}

void FACPClient::ReportError(const FString &Where, const FString &Message) {
  LastError = FString::Printf(TEXT("[%s] %s"), *Where, *Message);
  UE_LOG(LogUAgent, Error, TEXT("%s"), *LastError);
  SetState(EClientState::Error);
  OnError.Broadcast(LastError);
}

void FACPClient::SendInitialize() {
  SetState(EClientState::Initializing);

  TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
  Params->SetNumberField(TEXT("protocolVersion"), 1);

  // Derive capabilities from what's registered. Un-registering the fs
  // tools disables the corresponding capability automatically.
  TSharedRef<FJsonObject> Caps = MakeShared<FJsonObject>();
  TSharedRef<FJsonObject> Fs = MakeShared<FJsonObject>();
  const bool bHasRead = ToolRegistry.IsValid() &&
                        ToolRegistry->Contains(TEXT("fs/read_text_file"));
  const bool bHasWrite = ToolRegistry.IsValid() &&
                         ToolRegistry->Contains(TEXT("fs/write_text_file"));
  Fs->SetBoolField(TEXT("readTextFile"), bHasRead);
  Fs->SetBoolField(TEXT("writeTextFile"), bHasWrite);
  Caps->SetObjectField(TEXT("fs"), Fs);
  Caps->SetBoolField(TEXT("terminal"), false);
  Params->SetObjectField(TEXT("clientCapabilities"), Caps);

  TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
  Info->SetStringField(TEXT("name"), TEXT("UAgent"));
  Info->SetStringField(TEXT("title"), TEXT("Unreal Engine 5 Agent Bridge"));
  FString PluginVersion = TEXT("0.0.0");
  if (const TSharedPtr<IPlugin> Self =
          IPluginManager::Get().FindPlugin(TEXT("UAgent"))) {
    PluginVersion = Self->GetDescriptor().VersionName;
  }
  Info->SetStringField(TEXT("version"), PluginVersion);
  Params->SetObjectField(TEXT("clientInfo"), Info);

  Peer.SendRequest(
      TEXT("initialize"), Params,
      [this](const TSharedPtr<FJsonObject> &Result,
             const TSharedPtr<FJsonObject> &Error) {
        if (Error.IsValid()) {
          FString Msg;
          Error->TryGetStringField(TEXT("message"), Msg);
          ReportError(TEXT("initialize"), Msg);
          return;
        }

        bAgentSupportsHttpMcp = false;
        if (Result.IsValid()) {
          const TSharedPtr<FJsonObject> *AgentCaps = nullptr;
          if (Result->TryGetObjectField(TEXT("agentCapabilities"), AgentCaps) &&
              AgentCaps && AgentCaps->IsValid()) {
            const TSharedPtr<FJsonObject> *McpCaps = nullptr;
            if ((*AgentCaps)
                    ->TryGetObjectField(TEXT("mcpCapabilities"), McpCaps) &&
                McpCaps && McpCaps->IsValid()) {
              (*McpCaps)->TryGetBoolField(TEXT("http"), bAgentSupportsHttpMcp);
            }
          }
        }
        SendNewSession();
      });
}

void FACPClient::SendNewSession() {
  SetState(EClientState::CreatingSession);

  TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
  Params->SetStringField(
      TEXT("cwd"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));

  TArray<TSharedPtr<FJsonValue>> McpServers;
  if (!McpServerUrl.IsEmpty()) {
    if (bAgentSupportsHttpMcp) {
      TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
      Entry->SetStringField(TEXT("type"), TEXT("http"));
      Entry->SetStringField(TEXT("name"), TEXT("ue5"));
      Entry->SetStringField(TEXT("url"), McpServerUrl);
      Entry->SetArrayField(TEXT("headers"), TArray<TSharedPtr<FJsonValue>>{});
      McpServers.Add(MakeShared<FJsonValueObject>(Entry));
    } else {
      UE_LOG(LogUAgent, Warning,
             TEXT("Agent did not advertise mcp_capabilities.http; UE5 MCP "
                  "tools won't be auto-registered for this session."));
    }
  }
  Params->SetArrayField(TEXT("mcpServers"), McpServers);

  Peer.SendRequest(
      TEXT("session/new"), Params,
      [this](const TSharedPtr<FJsonObject> &Result,
             const TSharedPtr<FJsonObject> &Error) {
        if (Error.IsValid()) {
          FString Msg;
          Error->TryGetStringField(TEXT("message"), Msg);
          ReportError(TEXT("session/new"), Msg);
          return;
        }
        if (!Result.IsValid() ||
            !Result->TryGetStringField(TEXT("sessionId"), SessionId)) {
          ReportError(TEXT("session/new"), TEXT("response missing sessionId"));
          return;
        }

        // Pull configOptions if the agent advertised any — it's an optional,
        // agent-specific field. Agents that don't expose it just won't see
        // the corresponding UI surface.
        ConfigOptions.Reset();
        const TArray<TSharedPtr<FJsonValue>> *ConfigArr = nullptr;
        if (Result->TryGetArrayField(TEXT("configOptions"), ConfigArr) &&
            ConfigArr) {
          ParseConfigOptions(*ConfigArr, ConfigOptions);
        }
        OnAgentSettingsChanged.Broadcast();

        UE_LOG(LogUAgent, Log, TEXT("Session ready: %s (configOptions=%d)"),
               *SessionId, ConfigOptions.Num());
        SetState(EClientState::Ready);
      });
}

bool FACPClient::SendPrompt(const TArray<FContentBlock> &Blocks) {
  if (State != EClientState::Ready) {
    UE_LOG(LogUAgent, Warning, TEXT("SendPrompt while state=%d (need Ready)"),
           (int32)State);
    return false;
  }

  TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
  Params->SetStringField(TEXT("sessionId"), SessionId);
  Params->SetArrayField(TEXT("prompt"), ContentBlocksToJson(Blocks));

  SetState(EClientState::Prompting);

  Peer.SendRequest(TEXT("session/prompt"), Params,
                   [this](const TSharedPtr<FJsonObject> &Result,
                          const TSharedPtr<FJsonObject> &Error) {
                     if (Error.IsValid()) {
                       FString Msg;
                       Error->TryGetStringField(TEXT("message"), Msg);
                       OnPromptCompleted.Broadcast(EStopReason::Unknown, Msg);
                       SetState(EClientState::Ready);
                       return;
                     }
                     FString ReasonStr;
                     if (Result.IsValid())
                       Result->TryGetStringField(TEXT("stopReason"), ReasonStr);
                     OnPromptCompleted.Broadcast(ParseStopReason(ReasonStr),
                                                 FString());
                     SetState(EClientState::Ready);
                   });
  return true;
}

void FACPClient::CancelPrompt() {
  if (State != EClientState::Prompting || SessionId.IsEmpty())
    return;
  TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
  Params->SetStringField(TEXT("sessionId"), SessionId);
  Peer.SendNotification(TEXT("session/cancel"), Params);
}

void FACPClient::HandleNotification(const FString &Method,
                                    const TSharedPtr<FJsonObject> &Params) {
  if (Method == TEXT("session/update") && Params.IsValid()) {
    FSessionUpdate Update;
    if (FSessionUpdate::FromJson(Params.ToSharedRef(), Update)) {
      // Mirror agent-driven config-option changes into our local snapshot
      // before fanning the update out, so subscribers querying
      // GetConfigOptions() during their handler see the post-change state.
      const bool bSettingsChanged =
          Update.Kind == FSessionUpdate::EKind::ConfigOptionUpdate;
      if (bSettingsChanged) {
        ConfigOptions = Update.ConfigOptions;
      }

      OnSessionUpdate.Broadcast(Update);
      if (bSettingsChanged) {
        OnAgentSettingsChanged.Broadcast();
      }
    }
    return;
  }
  UE_LOG(LogUAgent, Verbose, TEXT("Unhandled notification: %s"), *Method);
}

void FACPClient::SetConfigOption(const FString &ConfigId,
                                 const FString &Value) {
  if (SessionId.IsEmpty() || ConfigId.IsEmpty())
    return;

  bool bChanged = false;
  for (FConfigOption &Opt : ConfigOptions) {
    if (Opt.Id == ConfigId && Opt.CurrentValue != Value) {
      Opt.CurrentValue = Value;
      bChanged = true;
      break;
    }
  }
  if (!bChanged)
    return;
  OnAgentSettingsChanged.Broadcast();

  TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
  Params->SetStringField(TEXT("sessionId"), SessionId);
  Params->SetStringField(TEXT("configId"), ConfigId);
  Params->SetStringField(TEXT("value"), Value);
  Peer.SendRequest(TEXT("session/set_config_option"), Params,
                   [](const TSharedPtr<FJsonObject> &,
                      const TSharedPtr<FJsonObject> &Error) {
                     if (Error.IsValid()) {
                       FString Msg;
                       Error->TryGetStringField(TEXT("message"), Msg);
                       UE_LOG(LogUAgent, Warning,
                              TEXT("session/set_config_option failed: %s"),
                              *Msg);
                     }
                   });
}

void FACPClient::HandleRequest(const FString &Method,
                               const TSharedPtr<FJsonObject> &Params,
                               const TSharedPtr<FJsonValue> &Id) {
  TSharedPtr<IACPTool> Tool =
      ToolRegistry.IsValid() ? ToolRegistry->Find(Method) : nullptr;
  if (!Tool.IsValid()) {
    Peer.SendErrorResponse(
        Id, -32601, FString::Printf(TEXT("method not found: %s"), *Method));
    return;
  }

  // Capture Tool by shared ptr so it stays alive across an async deferral.
  // The callback may fire on a later game-thread tick (after a UI button
  // click); FACPClient outlives the chat session so capturing `this` is OK.
  Tool->ExecuteAsync(Params, [this, Id, Tool](FToolResponse Response) {
    if (Response.Error.IsSet()) {
      Peer.SendErrorResponse(Id, Response.Error->Code, Response.Error->Message);
      return;
    }
    Peer.SendResponse(Id, Response.Result.IsValid()
                              ? Response.Result.ToSharedRef()
                              : MakeShared<FJsonObject>());
  });
}
} // namespace UAgent
