#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/ClassResolver.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/Package.h"

namespace UAgent {
namespace {
FString ResolveModuleBaseDir(const FString &ModuleName) {
  for (const TSharedRef<IPlugin> &P :
       IPluginManager::Get().GetEnabledPlugins()) {
    for (const FModuleDescriptor &M : P->GetDescriptor().Modules) {
      if (M.Name.ToString() == ModuleName) {
        return FPaths::Combine(P->GetBaseDir(), TEXT("Source"), ModuleName);
      }
    }
  }
  // Project-owned native modules live under <Project>/Source/<ModuleName>/
  // and aren't enumerated by IPluginManager.
  const FString ProjectCandidate =
      FPaths::Combine(FPaths::GameSourceDir(), ModuleName);
  if (FPaths::DirectoryExists(ProjectCandidate)) {
    return ProjectCandidate;
  }
  return FString();
}

class FReadHeaderTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/read_header");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Resolve a class name to its declaring header and return the "
                "file contents. Shortcut for find_native_class + "
                "fs/read_text_file that works without pasting a full path.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"class": { "type": "string" }
					},
					"required": ["class"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ClassName;
    Params->TryGetStringField(TEXT("class"), ClassName);
    if (ClassName.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing 'class'"));

    FString Err;
    UClass *C = Common::ResolveClass(ClassName, Err);
    if (!C)
      return FToolResponse::Fail(-32000, Err);

    UPackage *Pkg = C->GetOutermost();
    const FString PkgName = Pkg ? Pkg->GetName() : FString();
    if (!PkgName.StartsWith(TEXT("/Script/"))) {
      return FToolResponse::Fail(-32000, TEXT("class is not native"));
    }
    const FString ModuleName =
        PkgName.RightChop(FString(TEXT("/Script/")).Len());
    const FString Relative = C->GetMetaData(TEXT("ModuleRelativePath"));
    if (Relative.IsEmpty()) {
      return FToolResponse::Fail(-32000,
                                 TEXT("no ModuleRelativePath metadata"));
    }
    const FString Base = ResolveModuleBaseDir(ModuleName);
    if (Base.IsEmpty()) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("module '%s' not found in enabled plugins"),
                          *ModuleName));
    }

    const FString HeaderPath = FPaths::Combine(Base, Relative);
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *HeaderPath)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("could not read %s"), *HeaderPath));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("class"), C->GetPathName());
    Result->SetStringField(TEXT("headerPath"), HeaderPath);
    Result->SetStringField(TEXT("content"), Content);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateReadHeaderTool() {
  return MakeShared<FReadHeaderTool>();
}
} // namespace UAgent
