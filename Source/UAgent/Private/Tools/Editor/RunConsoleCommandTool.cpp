#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Misc/OutputDeviceRedirector.h"

namespace UAgent {
namespace {
class FCaptureOutput : public FOutputDevice {
public:
  FString Buffer;
  virtual void Serialize(const TCHAR *V, ELogVerbosity::Type,
                         const FName &) override {
    if (!Buffer.IsEmpty())
      Buffer.AppendChar(TEXT('\n'));
    Buffer.Append(V);
  }
  virtual bool CanBeUsedOnAnyThread() const override { return true; }
};

class FRunConsoleCommandTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/run_console_command");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Execute an editor console command (e.g. 'stat fps', 'stat "
                "unit', cvars). Captures immediate output; for ongoing stats, "
                "poll read_editor_log. Requires an editor world.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"command": { "type": "string" }
					},
					"required": ["command"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString Cmd;
    Params->TryGetStringField(TEXT("command"), Cmd);
    if (Cmd.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing command"));

    UWorld *World =
        GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
      return FToolResponse::Fail(-32000, TEXT("no editor world"));

    FCaptureOutput Capture;
    const bool bOk = GEditor->Exec(World, *Cmd, Capture);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("command"), Cmd);
    Result->SetBoolField(TEXT("ok"), bOk);
    Result->SetStringField(TEXT("output"), Capture.Buffer);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateRunConsoleCommandTool() {
  return MakeShared<FRunConsoleCommandTool>();
}
} // namespace UAgent
