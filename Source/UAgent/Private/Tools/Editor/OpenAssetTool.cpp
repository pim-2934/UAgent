#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

namespace UAgent {
namespace {
class FOpenAssetTool : public IACPTool {
public:
  virtual FString GetMethod() const override { return TEXT("_ue5/open_asset"); }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Open an asset in its default editor (Blueprint editor, DataTable "
        "editor, etc.). Use this to show the user what you are discussing.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"assetPath": { "type": "string", "description": "Path to the asset, e.g. /Game/Maps/MyMap" }
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

    UAssetEditorSubsystem *Sub =
        GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
                : nullptr;
    if (!Sub)
      return FToolResponse::Fail(-32000,
                                 TEXT("asset editor subsystem unavailable"));

    const bool bOk = Sub->OpenEditorForAsset(Asset);
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("opened"), bOk);
    Result->SetStringField(TEXT("path"), Asset->GetPathName());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateOpenAssetTool() {
  return MakeShared<FOpenAssetTool>();
}
} // namespace UAgent
