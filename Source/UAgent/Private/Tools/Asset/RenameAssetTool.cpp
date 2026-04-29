#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UAgent {
namespace {
class FRenameAssetTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/rename_asset");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Rename or move an asset. Destination can be in the same "
                "folder (rename) or any other '/Game/...' folder (move). "
                "Incoming references are updated to the new path. Fails if the "
                "destination already exists.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"sourcePath":      { "type": "string", "description": "Current asset path, e.g. '/Game/Foo/BP_Bar'" },
						"destinationPath": { "type": "string", "description": "New asset path, e.g. '/Game/Foo/BP_Baz' (rename) or '/Game/Other/BP_Bar' (move)" }
					},
					"required": ["sourcePath", "destinationPath"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString Source, Dest;
    Params->TryGetStringField(TEXT("sourcePath"), Source);
    Params->TryGetStringField(TEXT("destinationPath"), Dest);
    if (Source.IsEmpty() || Dest.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("'sourcePath' and 'destinationPath' are required"));
    }

    FString SrcNorm, DstNorm, IgnoredName, Err;
    if (!Common::SplitContentPath(Source, SrcNorm, IgnoredName, Err))
      return FToolResponse::InvalidParams(
          FString::Printf(TEXT("sourcePath: %s"), *Err));
    if (!Common::SplitContentPath(Dest, DstNorm, IgnoredName, Err))
      return FToolResponse::InvalidParams(
          FString::Printf(TEXT("destinationPath: %s"), *Err));

    UEditorAssetSubsystem *AS =
        GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                : nullptr;
    if (!AS)
      return FToolResponse::Fail(-32000,
                                 TEXT("EditorAssetSubsystem unavailable"));

    if (!AS->DoesAssetExist(SrcNorm)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("no asset at source '%s'"), *Source));
    }
    if (AS->DoesAssetExist(DstNorm)) {
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("destination already exists: '%s'"), *Dest));
    }

    if (!AS->RenameAsset(SrcNorm, DstNorm)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("RenameAsset failed: '%s' -> '%s'"),
                                  *Source, *Dest));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("from"), SrcNorm);
    Result->SetStringField(TEXT("to"), DstNorm);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateRenameAssetTool() {
  return MakeShared<FRenameAssetTool>();
}
} // namespace UAgent
