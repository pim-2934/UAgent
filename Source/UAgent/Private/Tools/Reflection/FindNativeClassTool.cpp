#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/ClassResolver.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/Package.h"

namespace UAgent {
namespace {
FString ResolveModuleBaseDir(const FString &ModuleName) {
  TSharedPtr<IPlugin> Plugin = nullptr;
  for (const TSharedRef<IPlugin> &P :
       IPluginManager::Get().GetEnabledPlugins()) {
    for (const FModuleDescriptor &M : P->GetDescriptor().Modules) {
      if (M.Name.ToString() == ModuleName) {
        return FPaths::Combine(P->GetBaseDir(), TEXT("Source"), ModuleName);
      }
    }
  }
  return FString();
}

class FFindNativeClassTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/find_native_class");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Resolve a class name to its declaring header file. Returns the class "
        "path, module name, and absolute path to the header if resolvable. Use "
        "before read_header or fs/read_text_file on native source.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"class": { "type": "string", "description": "Class name or path" }
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

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), C->GetPathName());
    Result->SetStringField(TEXT("name"), C->GetName());

    UPackage *Pkg = C->GetOutermost();
    const FString PkgName = Pkg ? Pkg->GetName() : FString();
    FString ModuleName;
    if (PkgName.StartsWith(TEXT("/Script/"))) {
      ModuleName = PkgName.RightChop(FString(TEXT("/Script/")).Len());
      Result->SetStringField(TEXT("module"), ModuleName);
    }

    const FString Relative = C->GetMetaData(TEXT("ModuleRelativePath"));
    Result->SetStringField(TEXT("moduleRelativePath"), Relative);

    if (!Relative.IsEmpty() && !ModuleName.IsEmpty()) {
      const FString Base = ResolveModuleBaseDir(ModuleName);
      if (!Base.IsEmpty()) {
        Result->SetStringField(TEXT("headerPath"),
                               FPaths::Combine(Base, Relative));
      }
    }

    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateFindNativeClassTool() {
  return MakeShared<FFindNativeClassTool>();
}
} // namespace UAgent
