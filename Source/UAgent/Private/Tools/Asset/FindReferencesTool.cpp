#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

namespace UAgent {
namespace {
class FFindReferencesTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/find_references");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("List packages that reference the given asset (Reference "
                "Viewer data). Use to answer 'who uses X?'.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"assetPath": { "type": "string", "description": "Asset path. Package form (/Game/X/Y) is accepted." }
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
    TArray<FName> Referencers;
    Registry.GetReferencers(FName(*Package), Referencers,
                            UE::AssetRegistry::EDependencyCategory::Package);

    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FName &N : Referencers) {
      Out.Add(MakeShared<FJsonValueString>(N.ToString()));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("package"), Package);
    Result->SetArrayField(TEXT("referencers"), Out);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateFindReferencesTool() {
  return MakeShared<FFindReferencesTool>();
}
} // namespace UAgent
