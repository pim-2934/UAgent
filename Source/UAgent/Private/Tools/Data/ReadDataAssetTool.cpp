#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/PropertyToJson.h"

#include "Engine/DataAsset.h"

namespace UAgent {
namespace {
class FReadDataAssetTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/read_data_asset");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Read a UDataAsset or UPrimaryDataAsset as JSON. Works for any "
                "UDataAsset-derived configuration asset.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"assetPath": { "type": "string", "description": "Path to the data asset" }
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

    FString Err;
    UObject *Asset = Common::LoadAssetByPath(AssetPath, nullptr, Err);
    if (!Asset)
      return FToolResponse::Fail(-32000, Err);
    if (!Asset->IsA<UDataAsset>()) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("asset at '%s' is %s, not a UDataAsset"),
                                  *AssetPath, *Asset->GetClass()->GetName()));
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("path"), Asset->GetPathName());
    Root->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Root->SetObjectField(TEXT("properties"), Common::PropertiesToJsonObject(
                                                 Asset->GetClass(), Asset));
    return FToolResponse::Ok(Root);
  }
};
} // namespace

TSharedRef<IACPTool> CreateReadDataAssetTool() {
  return MakeShared<FReadDataAssetTool>();
}
} // namespace UAgent
