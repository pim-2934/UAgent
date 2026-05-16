#include "MCPProtocol.h"

#include "../Protocol/ACPToolRegistry.h"
#include "../Protocol/ACPTypes.h"

#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UAgent {
// Only tools whose ACP method name begins with this prefix are exposed over
// MCP; the prefix is stripped for the MCP-facing name. fs/* and session/*
// are ACP-native and stay off the MCP surface.
static const FString MCPMethodPrefix = TEXT("_ue5/");

FMCPProtocol::FMCPProtocol(TSharedPtr<FACPToolRegistry> InRegistry)
    : Registry(MoveTemp(InRegistry)) {}

void FMCPProtocol::Dispatch(const TSharedRef<FJsonObject> &Msg,
                            FResponseCallback OnResponse) {
  const TSharedPtr<FJsonValue> Id = Msg->TryGetField(TEXT("id"));

  // Notifications (no id) carry no response envelope.
  if (!Id.IsValid()) {
    OnResponse(nullptr);
    return;
  }

  FString Method;
  Msg->TryGetStringField(TEXT("method"), Method);

  if (Method == TEXT("initialize")) {
    OnResponse(HandleInitialize(Id));
    return;
  }
  if (Method == TEXT("tools/list")) {
    OnResponse(HandleToolsList(Id));
    return;
  }
  if (Method == TEXT("tools/call")) {
    const TSharedPtr<FJsonObject> *ParamsObj = nullptr;
    Msg->TryGetObjectField(TEXT("params"), ParamsObj);
    HandleToolsCall(Id, ParamsObj ? *ParamsObj : TSharedPtr<FJsonObject>(),
                    MoveTemp(OnResponse));
    return;
  }

  OnResponse(MakeError(Id, -32601,
                       FString::Printf(TEXT("method not found: %s"), *Method)));
}

TSharedRef<FJsonObject>
FMCPProtocol::HandleInitialize(const TSharedPtr<FJsonValue> &Id) {
  TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
  Result->SetStringField(TEXT("protocolVersion"), TEXT("2024-11-05"));

  TSharedRef<FJsonObject> Caps = MakeShared<FJsonObject>();
  Caps->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());
  Result->SetObjectField(TEXT("capabilities"), Caps);

  TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
  Info->SetStringField(TEXT("name"), TEXT("UAgent"));
  FString PluginVersion = TEXT("0.0.0");
  if (const TSharedPtr<IPlugin> Self =
          IPluginManager::Get().FindPlugin(TEXT("UAgent"))) {
    PluginVersion = Self->GetDescriptor().VersionName;
  }
  Info->SetStringField(TEXT("version"), PluginVersion);
  Result->SetObjectField(TEXT("serverInfo"), Info);

  return MakeResult(Id, Result);
}

TSharedRef<FJsonObject>
FMCPProtocol::HandleToolsList(const TSharedPtr<FJsonValue> &Id) {
  TArray<TSharedPtr<FJsonValue>> Tools;
  if (Registry.IsValid()) {
    for (const FString &Method : Registry->GetMethodNames()) {
      if (!Method.StartsWith(MCPMethodPrefix))
        continue;

      const TSharedPtr<IACPTool> Tool = Registry->Find(Method);
      if (!Tool.IsValid())
        continue;

      TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
      T->SetStringField(TEXT("name"), Method.RightChop(MCPMethodPrefix.Len()));
      T->SetStringField(TEXT("description"), Tool->GetDescription());
      if (TSharedPtr<FJsonObject> Schema = Tool->GetInputSchema()) {
        T->SetObjectField(TEXT("inputSchema"), Schema);
      }
      // Spec-compliant tool annotations. claude-agent-acp 0.31.0 ignores
      // these (it always tags MCP tools kind="other"), but emitting them is
      // correct per the MCP spec and lets any other client classify our
      // tools properly without round-tripping through us.
      if (Tool->IsReadOnly()) {
        TSharedRef<FJsonObject> Annotations = MakeShared<FJsonObject>();
        Annotations->SetBoolField(TEXT("readOnlyHint"), true);
        T->SetObjectField(TEXT("annotations"), Annotations);
      }
      Tools.Add(MakeShared<FJsonValueObject>(T));
    }
  }

  TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
  Result->SetArrayField(TEXT("tools"), Tools);
  return MakeResult(Id, Result);
}

void FMCPProtocol::HandleToolsCall(const TSharedPtr<FJsonValue> &Id,
                                   const TSharedPtr<FJsonObject> &Params,
                                   FResponseCallback OnResponse) {
  if (!Params.IsValid()) {
    OnResponse(MakeError(Id, -32602, TEXT("missing params")));
    return;
  }

  FString Name;
  Params->TryGetStringField(TEXT("name"), Name);
  if (Name.IsEmpty()) {
    OnResponse(MakeError(Id, -32602, TEXT("missing name")));
    return;
  }

  const TSharedPtr<FJsonObject> *ArgsObj = nullptr;
  Params->TryGetObjectField(TEXT("arguments"), ArgsObj);
  const TSharedPtr<FJsonObject> Arguments =
      ArgsObj ? *ArgsObj : TSharedPtr<FJsonObject>();

  const FString Method = MCPMethodPrefix + Name;
  const TSharedPtr<IACPTool> Tool =
      Registry.IsValid() ? Registry->Find(Method) : nullptr;
  if (!Tool.IsValid()) {
    OnResponse(MakeError(Id, -32601,
                         FString::Printf(TEXT("tool not found: %s"), *Name)));
    return;
  }

  // ExecuteAsync defaults to forwarding to Execute() and firing the callback
  // synchronously, so synchronous tools take the same dispatch path as
  // deferred ones. Capture this by raw pointer — the protocol object outlives
  // any in-flight request because FMCPServer destroys the protocol only
  // after Stop(), which removes the HTTP route first.
  Tool->ExecuteAsync(Arguments, [this, Id, OnResponse = MoveTemp(OnResponse)](
                                    FToolResponse Response) {
    // MCP signals tool failure via isError + a text content block, NOT
    // via a JSON-RPC error envelope. JSON-RPC errors are reserved for
    // protocol issues (parse, missing method, malformed params).
    TSharedRef<FJsonObject> McpResult = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Content;

    if (Response.Error.IsSet()) {
      TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
      Block->SetStringField(TEXT("type"), TEXT("text"));
      Block->SetStringField(TEXT("text"), Response.Error->Message);
      Content.Add(MakeShared<FJsonValueObject>(Block));
      McpResult->SetBoolField(TEXT("isError"), true);
    } else {
      FString Serialized;
      if (Response.Result.IsValid()) {
        const TSharedRef<TJsonWriter<>> W =
            TJsonWriterFactory<>::Create(&Serialized);
        FJsonSerializer::Serialize(Response.Result.ToSharedRef(), W);
      }
      TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
      Block->SetStringField(TEXT("type"), TEXT("text"));
      Block->SetStringField(TEXT("text"), Serialized);
      Content.Add(MakeShared<FJsonValueObject>(Block));
      McpResult->SetBoolField(TEXT("isError"), false);
    }

    McpResult->SetArrayField(TEXT("content"), Content);
    OnResponse(MakeResult(Id, McpResult));
  });
}

TSharedRef<FJsonObject>
FMCPProtocol::MakeResult(const TSharedPtr<FJsonValue> &Id,
                         const TSharedRef<FJsonObject> &Result) const {
  TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
  Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
  if (Id.IsValid()) {
    Msg->SetField(TEXT("id"), Id);
  }
  Msg->SetObjectField(TEXT("result"), Result);
  return Msg;
}

TSharedRef<FJsonObject>
FMCPProtocol::MakeError(const TSharedPtr<FJsonValue> &Id, int32 Code,
                        const FString &Message) const {
  TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
  Msg->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
  if (Id.IsValid()) {
    Msg->SetField(TEXT("id"), Id);
  }
  TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
  Err->SetNumberField(TEXT("code"), Code);
  Err->SetStringField(TEXT("message"), Message);
  Msg->SetObjectField(TEXT("error"), Err);
  return Msg;
}

TSharedRef<FJsonObject>
FMCPProtocol::MakeParseError(const FString &Message) const {
  return MakeError(nullptr, -32700, Message);
}
} // namespace UAgent
