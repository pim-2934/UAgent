#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"

namespace UAgent {
namespace {
class FFocusInContentBrowserTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/focus_in_content_browser");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Highlight an asset in the Content Browser so the user can see "
                "where it lives.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"assetPath": { "type": "string", "description": "Path to the asset to focus" }
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

    FContentBrowserModule &Module =
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(
            TEXT("ContentBrowser"));
    TArray<FAssetData> Selection;
    Selection.Emplace(Asset);
    Module.Get().SyncBrowserToAssets(Selection, /*bAllowLockedBrowsers=*/false,
                                     /*bFocusContentBrowser=*/true);

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Asset->GetPathName());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateFocusInContentBrowserTool() {
  return MakeShared<FFocusInContentBrowserTool>();
}
} // namespace UAgent
