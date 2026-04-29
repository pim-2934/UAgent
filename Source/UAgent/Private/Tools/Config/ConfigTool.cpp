#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Fs/FsPathUtils.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace UAgent {
namespace {
bool ResolveConfigPath(const FString &InPath, FString &OutAbsolute,
                       FString &OutError) {
  // Accept either "DefaultEngine.ini" (resolved under Config/),
  // "Config/DefaultX.ini", or an absolute path.
  FString Path = InPath;
  if (!FPaths::IsRelative(Path) || Path.StartsWith(TEXT("/"))) {
    OutAbsolute = Path;
  } else if (Path.StartsWith(TEXT("Config/"))) {
    OutAbsolute = FPaths::Combine(FPaths::ProjectDir(), Path);
  } else {
    OutAbsolute = FPaths::Combine(FPaths::ProjectDir(), TEXT("Config"), Path);
  }
  OutAbsolute = FPaths::ConvertRelativePathToFull(OutAbsolute);
  if (!FsPathUtils::IsInsideProject(OutAbsolute)) {
    OutError = TEXT("config path must be inside the project");
    return false;
  }
  return true;
}

class FReadConfigTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/read_config");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Read a config entry via GConfig. File can be "
                "'DefaultEngine.ini', 'Config/DefaultGame.ini', or absolute.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"file": { "type": "string" },
						"section": { "type": "string" },
						"key": { "type": "string" }
					},
					"required": ["file", "section", "key"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString File, Section, Key;
    Params->TryGetStringField(TEXT("file"), File);
    Params->TryGetStringField(TEXT("section"), Section);
    Params->TryGetStringField(TEXT("key"), Key);
    if (File.IsEmpty() || Section.IsEmpty() || Key.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("file, section, key required"));
    }

    FString Abs, Err;
    if (!ResolveConfigPath(File, Abs, Err))
      return FToolResponse::Fail(-32000, Err);

    FString Value;
    const bool bFound = GConfig->GetString(*Section, *Key, Value, Abs);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("file"), Abs);
    Result->SetBoolField(TEXT("found"), bFound);
    Result->SetStringField(TEXT("value"), Value);
    return FToolResponse::Ok(Result);
  }
};

class FWriteConfigTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/write_config");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Write a config entry via GConfig and flush. Restricted to "
                "files under the project directory.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"file": { "type": "string" },
						"section": { "type": "string" },
						"key": { "type": "string" },
						"value": { "type": "string" }
					},
					"required": ["file", "section", "key", "value"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString File, Section, Key, Value;
    Params->TryGetStringField(TEXT("file"), File);
    Params->TryGetStringField(TEXT("section"), Section);
    Params->TryGetStringField(TEXT("key"), Key);
    Params->TryGetStringField(TEXT("value"), Value);
    if (File.IsEmpty() || Section.IsEmpty() || Key.IsEmpty()) {
      return FToolResponse::InvalidParams(TEXT("file, section, key required"));
    }

    FString Abs, Err;
    if (!ResolveConfigPath(File, Abs, Err))
      return FToolResponse::Fail(-32000, Err);

    GConfig->SetString(*Section, *Key, *Value, Abs);
    GConfig->Flush(false, Abs);
    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateReadConfigTool() {
  return MakeShared<FReadConfigTool>();
}
TSharedRef<IACPTool> CreateWriteConfigTool() {
  return MakeShared<FWriteConfigTool>();
}
} // namespace UAgent
