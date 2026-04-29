#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Texture.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UAgent {
namespace {
bool ParseLinearColor(const FString &In, FLinearColor &Out) {
  if (In.IsEmpty())
    return false;
  TArray<FString> Parts;
  In.ParseIntoArray(Parts, TEXT(","));
  if (Parts.Num() < 3)
    return false;
  Out.R = FCString::Atof(*Parts[0].TrimStartAndEnd());
  Out.G = FCString::Atof(*Parts[1].TrimStartAndEnd());
  Out.B = FCString::Atof(*Parts[2].TrimStartAndEnd());
  Out.A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3].TrimStartAndEnd()) : 1.0f;
  return true;
}

class FCreateMaterialInstanceTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_material_instance");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Create a UMaterialInstanceConstant asset parented to an "
                "existing material (or other instance). Scalar/vector/texture "
                "parameter overrides are applied via UMaterialEditingLibrary. "
                "Vectors are 'R,G,B' or 'R,G,B,A' strings; textures are "
                "'/Game/...' asset paths. Saves the asset.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path":   { "type": "string", "description": "Target asset path, e.g. '/Game/Materials/MI_ArenaFloor'" },
						"parent": { "type": "string", "description": "Parent UMaterialInterface asset path" },
						"scalarParameters":  { "type": "object", "additionalProperties": { "type": "number" } },
						"vectorParameters":  { "type": "object", "additionalProperties": { "type": "string" } },
						"textureParameters": { "type": "object", "additionalProperties": { "type": "string" } }
					},
					"required": ["path", "parent"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Path, Parent;
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("parent"), Parent);
    if (Path.IsEmpty() || Parent.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("'path' and 'parent' are required"));
    }

    FString PackagePath, AssetName, Err;
    if (!Common::SplitContentPath(Path, PackagePath, AssetName, Err)) {
      return FToolResponse::InvalidParams(Err);
    }
    // AssetTools::CreateAsset wants the parent directory, not the full
    // package path.
    const FString ParentDir = FPaths::GetPath(PackagePath);

    UObject *ParentObj =
        Common::LoadAssetByPath(Parent, UMaterialInterface::StaticClass(), Err);
    UMaterialInterface *ParentMat = Cast<UMaterialInterface>(ParentObj);
    if (!ParentMat)
      return FToolResponse::Fail(
          -32000,
          Err.IsEmpty()
              ? FString::Printf(TEXT("could not load parent material '%s'"),
                                *Parent)
              : Err);

    FAssetToolsModule &AssetToolsModule =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(
            TEXT("AssetTools"));
    UMaterialInstanceConstantFactoryNew *Factory =
        NewObject<UMaterialInstanceConstantFactoryNew>();
    Factory->InitialParent = ParentMat;

    UObject *Created = AssetToolsModule.Get().CreateAsset(
        AssetName, ParentDir, UMaterialInstanceConstant::StaticClass(),
        Factory);
    UMaterialInstanceConstant *MIC = Cast<UMaterialInstanceConstant>(Created);
    if (!MIC)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("CreateAsset failed for '%s'"), *Path));

    TArray<TSharedPtr<FJsonValue>> Failed;
    auto RecordFail = [&](const FString &Kind, const FString &Name,
                          const FString &Msg) {
      TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
      F->SetStringField(TEXT("kind"), Kind);
      F->SetStringField(TEXT("parameter"), Name);
      F->SetStringField(TEXT("error"), Msg);
      Failed.Add(MakeShared<FJsonValueObject>(F));
    };

    const TSharedPtr<FJsonObject> *ScalarMap = nullptr;
    if (Params->TryGetObjectField(TEXT("scalarParameters"), ScalarMap) &&
        ScalarMap && ScalarMap->IsValid()) {
      for (const auto &Pair : (*ScalarMap)->Values) {
        double V = 0.0;
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetNumber(V)) {
          RecordFail(TEXT("scalar"), Pair.Key, TEXT("value must be a number"));
          continue;
        }
        if (!UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
                MIC, FName(*Pair.Key), (float)V)) {
          RecordFail(TEXT("scalar"), Pair.Key,
                     TEXT("parameter not found on parent"));
        }
      }
    }

    const TSharedPtr<FJsonObject> *VectorMap = nullptr;
    if (Params->TryGetObjectField(TEXT("vectorParameters"), VectorMap) &&
        VectorMap && VectorMap->IsValid()) {
      for (const auto &Pair : (*VectorMap)->Values) {
        FString S;
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(S)) {
          RecordFail(TEXT("vector"), Pair.Key,
                     TEXT("value must be a 'R,G,B' or 'R,G,B,A' string"));
          continue;
        }
        FLinearColor Color;
        if (!ParseLinearColor(S, Color)) {
          RecordFail(TEXT("vector"), Pair.Key,
                     FString::Printf(TEXT("malformed color '%s'"), *S));
          continue;
        }
        if (!UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
                MIC, FName(*Pair.Key), Color)) {
          RecordFail(TEXT("vector"), Pair.Key,
                     TEXT("parameter not found on parent"));
        }
      }
    }

    const TSharedPtr<FJsonObject> *TextureMap = nullptr;
    if (Params->TryGetObjectField(TEXT("textureParameters"), TextureMap) &&
        TextureMap && TextureMap->IsValid()) {
      for (const auto &Pair : (*TextureMap)->Values) {
        FString S;
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(S)) {
          RecordFail(TEXT("texture"), Pair.Key,
                     TEXT("value must be a texture asset path"));
          continue;
        }
        FString LoadErr;
        UObject *TexObj =
            Common::LoadAssetByPath(S, UTexture::StaticClass(), LoadErr);
        UTexture *Tex = Cast<UTexture>(TexObj);
        if (!Tex) {
          RecordFail(
              TEXT("texture"), Pair.Key,
              LoadErr.IsEmpty()
                  ? FString::Printf(TEXT("could not load texture '%s'"), *S)
                  : LoadErr);
          continue;
        }
        if (!UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(
                MIC, FName(*Pair.Key), Tex)) {
          RecordFail(TEXT("texture"), Pair.Key,
                     TEXT("parameter not found on parent"));
        }
      }
    }

    if (UEditorAssetSubsystem *AS =
            GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                    : nullptr) {
      AS->SaveLoadedAsset(MIC, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), MIC->GetPathName());
    Result->SetStringField(TEXT("parent"), ParentMat->GetPathName());
    if (Failed.Num() > 0)
      Result->SetArrayField(TEXT("failedParameters"), Failed);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateMaterialInstanceTool() {
  return MakeShared<FCreateMaterialInstanceTool>();
}
} // namespace UAgent
