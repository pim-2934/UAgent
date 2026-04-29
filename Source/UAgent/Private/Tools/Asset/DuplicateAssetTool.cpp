#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "EditorAssetLibrary.h"

namespace UAgent {
namespace {
class FDuplicateAssetTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/duplicate_asset");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Duplicate an asset to a new path. Canonical workflow for 'copy this "
        "sample and tweak it'. Saves the new asset by default.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"sourcePath": { "type": "string", "description": "Source asset path" },
						"destinationPath": { "type": "string", "description": "Destination asset path, e.g. /Game/MyGame/BP_MyChar" },
						"saveAsset": { "type": "boolean", "description": "Save the new asset to disk (default true)" }
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
          TEXT("sourcePath and destinationPath required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    const FString SrcNorm = Common::NormalizeAssetPath(Source);
    const FString DstNorm =
        Dest.Contains(TEXT(".")) ? Dest.Left(Dest.Find(TEXT("."))) : Dest;

    UObject *New = UEditorAssetLibrary::DuplicateAsset(SrcNorm, DstNorm);
    if (!New) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("DuplicateAsset failed: %s -> %s"),
                                  *Source, *Dest));
    }
    if (bSave) {
      UEditorAssetLibrary::SaveAsset(DstNorm, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), New->GetPathName());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateDuplicateAssetTool() {
  return MakeShared<FDuplicateAssetTool>();
}
} // namespace UAgent
