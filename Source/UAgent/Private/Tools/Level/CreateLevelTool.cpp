#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "Editor.h"
#include "LevelEditorSubsystem.h"

namespace UAgent {
namespace {
class FCreateLevelTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_level");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Create a new level asset and open it. Path is a /Game/... "
                "content path (no extension). If 'template' is set, the new "
                "level is seeded from that existing level asset.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path": { "type": "string", "description": "Target asset path, e.g. '/Game/Maps/L_MyLevel'" },
						"template": { "type": "string", "description": "Optional template level asset path to copy from" },
						"partitioned": { "type": "boolean", "description": "Create as a World Partition world. Ignored when 'template' is set. Default false." }
					},
					"required": ["path"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString Path, Template;
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("template"), Template);
    if (Path.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("path required"));
    }
    if (!Path.StartsWith(TEXT("/"))) {
      return FToolResponse::InvalidParams(TEXT(
          "path must be a content-browser path like '/Game/Maps/L_MyLevel'"));
    }
    bool bPartitioned = false;
    Params->TryGetBoolField(TEXT("partitioned"), bPartitioned);

    ULevelEditorSubsystem *Sub =
        GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>()
                : nullptr;
    if (!Sub)
      return FToolResponse::Fail(-32000,
                                 TEXT("LevelEditorSubsystem unavailable"));

    const bool bOk = Template.IsEmpty()
                         ? Sub->NewLevel(Path, bPartitioned)
                         : Sub->NewLevelFromTemplate(Path, Template);

    if (!bOk) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("failed to create level at '%s'"), *Path));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Path);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateLevelTool() {
  return MakeShared<FCreateLevelTool>();
}
} // namespace UAgent
