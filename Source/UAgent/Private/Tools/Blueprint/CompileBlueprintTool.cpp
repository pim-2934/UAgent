#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "BlueprintQueries.h"

#include "Engine/Blueprint.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"

namespace UAgent {
namespace {
const TCHAR *SeverityName(EMessageSeverity::Type S) {
  switch (S) {
  case EMessageSeverity::Error:
    return TEXT("error");
  case EMessageSeverity::Warning:
    return TEXT("warning");
  case EMessageSeverity::PerformanceWarning:
    return TEXT("performance");
  case EMessageSeverity::Info:
    return TEXT("info");
  default:
    return TEXT("info");
  }
}

class FCompileBlueprintTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/compile_blueprint");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Compile a Blueprint and return errors, warnings, and notes. "
                "Use after authoring edits to verify the result.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string", "description": "Path to the Blueprint" }
					},
					"required": ["blueprintPath"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString Path;
    Params->TryGetStringField(TEXT("blueprintPath"), Path);
    if (Path.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing blueprintPath"));

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(Path, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    FCompilerResultsLog Results;
    Results.SetSourcePath(BP->GetPathName());
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None,
                                             &Results);

    TArray<TSharedPtr<FJsonValue>> Messages;
    for (const TSharedRef<FTokenizedMessage> &M : Results.Messages) {
      TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
      J->SetStringField(TEXT("severity"), SeverityName(M->GetSeverity()));
      J->SetStringField(TEXT("text"), M->ToText().ToString());
      Messages.Add(MakeShared<FJsonValueObject>(J));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), BP->GetPathName());
    Result->SetNumberField(TEXT("numErrors"), Results.NumErrors);
    Result->SetNumberField(TEXT("numWarnings"), Results.NumWarnings);
    Result->SetArrayField(TEXT("messages"), Messages);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCompileBlueprintTool() {
  return MakeShared<FCompileBlueprintTool>();
}
} // namespace UAgent
