#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "EditorLogSink.h"

namespace UAgent {
namespace {
ELogVerbosity::Type ParseVerbosity(const FString &S) {
  if (S.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase))
    return ELogVerbosity::Fatal;
  if (S.Equals(TEXT("Error"), ESearchCase::IgnoreCase))
    return ELogVerbosity::Error;
  if (S.Equals(TEXT("Warning"), ESearchCase::IgnoreCase))
    return ELogVerbosity::Warning;
  if (S.Equals(TEXT("Display"), ESearchCase::IgnoreCase))
    return ELogVerbosity::Display;
  if (S.Equals(TEXT("Log"), ESearchCase::IgnoreCase))
    return ELogVerbosity::Log;
  if (S.Equals(TEXT("Verbose"), ESearchCase::IgnoreCase))
    return ELogVerbosity::Verbose;
  return ELogVerbosity::Log;
}

const TCHAR *VerbosityName(ELogVerbosity::Type V) {
  switch (V) {
  case ELogVerbosity::Fatal:
    return TEXT("Fatal");
  case ELogVerbosity::Error:
    return TEXT("Error");
  case ELogVerbosity::Warning:
    return TEXT("Warning");
  case ELogVerbosity::Display:
    return TEXT("Display");
  case ELogVerbosity::Log:
    return TEXT("Log");
  case ELogVerbosity::Verbose:
    return TEXT("Verbose");
  case ELogVerbosity::VeryVerbose:
    return TEXT("VeryVerbose");
  default:
    return TEXT("Log");
  }
}

class FReadEditorLogTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/read_editor_log");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Tail the editor's Output Log. Filter by category substring "
                "and/or minimum severity. Pass sinceId (from a previous call's "
                "nextId) to read only new lines.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"categoryContains": { "type": "string", "description": "Only lines whose category contains this substring (case-insensitive), e.g. 'UAgent' or 'LogBlueprint'" },
						"minVerbosity": { "type": "string", "description": "Minimum severity: Fatal|Error|Warning|Display|Log|Verbose. Default Log." },
						"limit": { "type": "integer", "description": "Max lines to return (default 200)", "minimum": 1 },
						"sinceId": { "type": "integer", "description": "Only return lines with id > this. Use nextId from a previous call to tail." }
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    FString CategoryContains;
    FString MinVerbStr;
    int32 Limit = 200;
    double SinceIdDouble = 0;

    if (Params.IsValid()) {
      Params->TryGetStringField(TEXT("categoryContains"), CategoryContains);
      Params->TryGetStringField(TEXT("minVerbosity"), MinVerbStr);
      Params->TryGetNumberField(TEXT("limit"), Limit);
      Params->TryGetNumberField(TEXT("sinceId"), SinceIdDouble);
    }
    if (Limit <= 0)
      Limit = 200;
    const ELogVerbosity::Type MinV =
        MinVerbStr.IsEmpty() ? ELogVerbosity::Log : ParseVerbosity(MinVerbStr);

    TArray<FEditorLogLine> Lines;
    int64 NextId = 0;
    FEditorLogSink::Get().GetLines(
        static_cast<int64>(SinceIdDouble), Limit,
        CategoryContains.IsEmpty() ? FName() : FName(*CategoryContains), MinV,
        Lines, NextId);

    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FEditorLogLine &L : Lines) {
      TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
      J->SetNumberField(TEXT("id"), static_cast<double>(L.Id));
      J->SetNumberField(TEXT("time"), L.Time);
      J->SetStringField(TEXT("verbosity"), VerbosityName(L.Verbosity));
      J->SetStringField(TEXT("category"), L.Category.ToString());
      J->SetStringField(TEXT("message"), L.Message);
      Out.Add(MakeShared<FJsonValueObject>(J));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("lines"), Out);
    Result->SetNumberField(TEXT("nextId"), static_cast<double>(NextId));
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateReadEditorLogTool() {
  return MakeShared<FReadEditorLogTool>();
}
} // namespace UAgent
