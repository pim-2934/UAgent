#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "Editor.h"
#include "LevelEditorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UAgent {
namespace {
class FSaveAssetTool : public IACPTool {
public:
  virtual FString GetMethod() const override { return TEXT("_ue5/save_asset"); }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Save an asset to disk. Pass a '/Game/...' path to save that asset "
        "(force-saves by default). Omit 'path' to save the currently edited "
        "level. Use 'allDirty=true' to save every dirty level in the world.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path": { "type": "string", "description": "Asset path like '/Game/Foo/MyAsset'. Omit to save the current level." },
						"onlyIfDirty": { "type": "boolean", "description": "Skip if the asset is not dirty. Default false." },
						"allDirty": { "type": "boolean", "description": "Save every dirty level in the world. Ignores 'path' and 'onlyIfDirty'. Default false." }
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    FString Path;
    bool bOnlyIfDirty = false;
    bool bAllDirty = false;
    if (Params.IsValid()) {
      Params->TryGetStringField(TEXT("path"), Path);
      Params->TryGetBoolField(TEXT("onlyIfDirty"), bOnlyIfDirty);
      Params->TryGetBoolField(TEXT("allDirty"), bAllDirty);
    }

    if (!GEditor)
      return FToolResponse::Fail(-32000, TEXT("GEditor unavailable"));

    if (bAllDirty) {
      ULevelEditorSubsystem *LE =
          GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
      if (!LE)
        return FToolResponse::Fail(-32000,
                                   TEXT("LevelEditorSubsystem unavailable"));
      if (!LE->SaveAllDirtyLevels()) {
        return FToolResponse::Fail(-32000, TEXT("SaveAllDirtyLevels failed"));
      }
      return FToolResponse::Ok();
    }

    if (Path.IsEmpty()) {
      ULevelEditorSubsystem *LE =
          GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
      if (!LE)
        return FToolResponse::Fail(-32000,
                                   TEXT("LevelEditorSubsystem unavailable"));
      if (!LE->SaveCurrentLevel()) {
        return FToolResponse::Fail(-32000,
                                   TEXT("SaveCurrentLevel failed (no level "
                                        "loaded, or user cancelled)"));
      }
      return FToolResponse::Ok();
    }

    UEditorAssetSubsystem *AS =
        GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
    if (!AS)
      return FToolResponse::Fail(-32000,
                                 TEXT("EditorAssetSubsystem unavailable"));

    FString AssetArg, AssetName, Err;
    if (!Common::SplitContentPath(Path, AssetArg, AssetName, Err))
      return FToolResponse::InvalidParams(Err);

    if (!AS->SaveAsset(AssetArg, bOnlyIfDirty)) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("failed to save asset '%s'"), *AssetArg));
    }
    return FToolResponse::Ok();
  }
};
} // namespace

TSharedRef<IACPTool> CreateSaveAssetTool() {
  return MakeShared<FSaveAssetTool>();
}
} // namespace UAgent
