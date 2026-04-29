#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "ImageUtils.h"
#include "LevelEditorViewport.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace UAgent {
namespace {
FViewport *GetTargetViewport(FString &OutKind) {
  // Prefer the most-recently-active level editor viewport, then the active
  // editor viewport.
  if (GCurrentLevelEditingViewportClient &&
      GCurrentLevelEditingViewportClient->Viewport) {
    OutKind = TEXT("levelEditor");
    return GCurrentLevelEditingViewportClient->Viewport;
  }
  if (GEditor) {
    if (FViewport *Active = GEditor->GetActiveViewport()) {
      OutKind = TEXT("activeEditor");
      return Active;
    }
  }
  return nullptr;
}

class FCaptureViewportTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/capture_viewport");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Capture the active editor level viewport to a PNG under "
        "<ProjectSaved>/UAgent/Screenshots. Returns the absolute and "
        "project-relative paths. With includeBase64=true, also embeds the PNG "
        "as a base64 string in the response (beware: large payload).");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"filename":       { "type": "string", "description": "Filename (without extension) under Saved/UAgent/Screenshots. Defaults to 'viewport_<timestamp>'." },
						"includeBase64":  { "type": "boolean", "description": "When true, the response includes the PNG bytes base64-encoded under 'base64'. Default false." }
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    FString Filename;
    bool bIncludeBase64 = false;
    if (Params.IsValid()) {
      Params->TryGetStringField(TEXT("filename"), Filename);
      Params->TryGetBoolField(TEXT("includeBase64"), bIncludeBase64);
    }

    FString ViewportKind;
    FViewport *Viewport = GetTargetViewport(ViewportKind);
    if (!Viewport)
      return FToolResponse::Fail(
          -32000,
          TEXT("no active level editor viewport — open one in the editor"));

    const FIntPoint Size = Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0) {
      return FToolResponse::Fail(-32000, TEXT("viewport has zero size"));
    }

    Viewport->Draw();

    TArray<FColor> Pixels;
    FReadSurfaceDataFlags ReadFlags;
    ReadFlags.SetLinearToGamma(false);
    if (!Viewport->ReadPixels(Pixels, ReadFlags,
                              FIntRect(0, 0, Size.X, Size.Y)) ||
        Pixels.Num() == 0) {
      return FToolResponse::Fail(-32000,
                                 TEXT("Viewport::ReadPixels returned no data"));
    }
    // ReadPixels leaves alpha at whatever the surface had — force opaque so the
    // PNG isn't mostly transparent when the viewport clears to a zero-alpha
    // colour.
    for (FColor &C : Pixels)
      C.A = 255;

    TArray64<uint8> PngBytes;
    FImageUtils::PNGCompressImageArray(
        Size.X, Size.Y,
        TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), PngBytes);
    if (PngBytes.Num() == 0) {
      return FToolResponse::Fail(-32000,
                                 TEXT("PNG encoding produced no bytes"));
    }

    if (Filename.IsEmpty()) {
      Filename =
          FString::Printf(TEXT("viewport_%s"),
                          *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    }
    // Reject path separators and traversal — the output dir is fixed and must
    // stay fixed.
    if (Filename.Contains(TEXT("/")) || Filename.Contains(TEXT("\\")) ||
        Filename.Contains(TEXT("..")) || Filename.Contains(TEXT(":"))) {
      return FToolResponse::InvalidParams(
          TEXT("filename must be a bare name — no path separators, '..', or "
               "drive letters"));
    }
    const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(),
                                        TEXT("UAgent"), TEXT("Screenshots"));
    IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
    const FString AbsPath = FPaths::Combine(Dir, Filename + TEXT(".png"));

    if (!FFileHelper::SaveArrayToFile(PngBytes, *AbsPath)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("failed to write '%s'"), *AbsPath));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), AbsPath);
    Result->SetStringField(TEXT("projectRelative"),
                           FPaths::ConvertRelativePathToFull(AbsPath));
    Result->SetNumberField(TEXT("width"), Size.X);
    Result->SetNumberField(TEXT("height"), Size.Y);
    Result->SetNumberField(TEXT("bytes"), PngBytes.Num());
    Result->SetStringField(TEXT("viewport"), ViewportKind);

    if (bIncludeBase64) {
      // FBase64 handles TArray<uint8>; copy down from the 64-bit variant.
      TArray<uint8> Small;
      Small.Append(PngBytes.GetData(), PngBytes.Num());
      Result->SetStringField(TEXT("base64"), FBase64::Encode(Small));
    }

    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCaptureViewportTool() {
  return MakeShared<FCaptureViewportTool>();
}
} // namespace UAgent
