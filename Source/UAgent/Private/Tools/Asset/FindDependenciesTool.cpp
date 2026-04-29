#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

namespace UAgent {
namespace {
class FFindDependenciesTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/find_dependencies");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("List packages the given asset depends on. Use to understand "
                "what a Blueprint pulls in.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"assetPath": { "type": "string", "description": "Asset path" }
					},
					"required": ["assetPath"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString AssetPath;
    Params->TryGetStringField(TEXT("assetPath"), AssetPath);
    if (AssetPath.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing assetPath"));

    FString Package, AssetName, Err;
    if (!Common::SplitContentPath(AssetPath, Package, AssetName, Err))
      return FToolResponse::InvalidParams(Err);

    IAssetRegistry &Registry = FAssetRegistryModule::GetRegistry();
    TArray<FName> Dependencies;
    Registry.GetDependencies(FName(*Package), Dependencies,
                             UE::AssetRegistry::EDependencyCategory::Package);

    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FName &N : Dependencies) {
      Out.Add(MakeShared<FJsonValueString>(N.ToString()));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("package"), Package);
    Result->SetArrayField(TEXT("dependencies"), Out);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateFindDependenciesTool() {
  return MakeShared<FFindDependenciesTool>();
}
} // namespace UAgent
