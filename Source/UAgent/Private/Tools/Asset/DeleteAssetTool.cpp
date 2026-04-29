#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UAgent {
namespace {
class FDeleteAssetTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/delete_asset");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Delete an asset by content-browser path. This is destructive and does "
        "not check incoming references — if other assets reference this one, "
        "they will break. Use find_references first if you're not sure.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path": { "type": "string", "description": "Asset path to delete, e.g. '/Game/Foo/BP_Bar'" }
					},
					"required": ["path"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString Path;
    Params->TryGetStringField(TEXT("path"), Path);
    if (Path.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("'path' is required"));

    UEditorAssetSubsystem *AS =
        GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                : nullptr;
    if (!AS)
      return FToolResponse::Fail(-32000,
                                 TEXT("EditorAssetSubsystem unavailable"));

    FString Normalized, AssetName, Err;
    if (!Common::SplitContentPath(Path, Normalized, AssetName, Err))
      return FToolResponse::InvalidParams(Err);

    if (!AS->DoesAssetExist(Normalized)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("no asset at '%s'"), *Path));
    }
    if (!AS->DeleteAsset(Normalized)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("DeleteAsset failed for '%s'"), *Path));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("deleted"), Normalized);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateDeleteAssetTool() {
  return MakeShared<FDeleteAssetTool>();
}
} // namespace UAgent
