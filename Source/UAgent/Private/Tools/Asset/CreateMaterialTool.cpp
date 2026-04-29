#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "AssetToolsModule.h"
#include "Editor.h"
#include "Factories/MaterialFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/Paths.h"
#include "SceneTypes.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace UAgent {
namespace {
// Parses "R,G,B" or "R,G,B,A" (0..1 range) into a linear color. Missing alpha
// defaults to 1.0.
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

bool ConnectColor(UMaterial *Mat, const FLinearColor &Color,
                  EMaterialProperty Target, int32 YOffset) {
  UMaterialExpressionConstant3Vector *Expr =
      Cast<UMaterialExpressionConstant3Vector>(
          UMaterialEditingLibrary::CreateMaterialExpression(
              Mat, UMaterialExpressionConstant3Vector::StaticClass(),
              /*NodePosX=*/-400, /*NodePosY=*/YOffset));
  if (!Expr)
    return false;
  Expr->Constant = Color;
  return UMaterialEditingLibrary::ConnectMaterialProperty(Expr, FString(),
                                                          Target);
}

bool ConnectScalar(UMaterial *Mat, float Value, EMaterialProperty Target,
                   int32 YOffset) {
  UMaterialExpressionConstant *Expr = Cast<UMaterialExpressionConstant>(
      UMaterialEditingLibrary::CreateMaterialExpression(
          Mat, UMaterialExpressionConstant::StaticClass(),
          /*NodePosX=*/-400, /*NodePosY=*/YOffset));
  if (!Expr)
    return false;
  Expr->R = Value;
  return UMaterialEditingLibrary::ConnectMaterialProperty(Expr, FString(),
                                                          Target);
}

class FCreateMaterialTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_material");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Create a new UMaterial asset with constant expressions wired into the "
        "most common inputs. 'baseColor' and 'emissive' are 'R,G,B' or "
        "'R,G,B,A' strings in 0..1. For shadingModel='unlit', a supplied "
        "'baseColor' is automatically routed to Emissive (since unlit ignores "
        "BaseColor). Saves the asset.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path":         { "type": "string", "description": "Target asset path, e.g. '/Game/Materials/M_ArenaFloor'" },
						"shadingModel": { "type": "string", "enum": ["default_lit", "unlit"], "description": "Default 'default_lit'." },
						"blendMode":    { "type": "string", "enum": ["opaque", "masked", "translucent", "additive"], "description": "Default 'opaque'." },
						"baseColor":    { "type": "string", "description": "'R,G,B' or 'R,G,B,A' in 0..1" },
						"emissive":     { "type": "string", "description": "'R,G,B' or 'R,G,B,A' in 0..1" },
						"metallic":     { "type": "number", "minimum": 0, "maximum": 1 },
						"roughness":    { "type": "number", "minimum": 0, "maximum": 1 },
						"specular":     { "type": "number", "minimum": 0, "maximum": 1 }
					},
					"required": ["path"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Path, ShadingModel = TEXT("default_lit"),
                  BlendMode = TEXT("opaque"), BaseColor, Emissive;
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("shadingModel"), ShadingModel);
    Params->TryGetStringField(TEXT("blendMode"), BlendMode);
    Params->TryGetStringField(TEXT("baseColor"), BaseColor);
    Params->TryGetStringField(TEXT("emissive"), Emissive);

    double Metallic = -1.0, Roughness = -1.0, Specular = -1.0;
    const bool bSetMetallic =
        Params->TryGetNumberField(TEXT("metallic"), Metallic);
    const bool bSetRoughness =
        Params->TryGetNumberField(TEXT("roughness"), Roughness);
    const bool bSetSpecular =
        Params->TryGetNumberField(TEXT("specular"), Specular);

    FString PackagePath, AssetName, Err;
    if (!Common::SplitContentPath(Path, PackagePath, AssetName, Err)) {
      return FToolResponse::InvalidParams(Err);
    }
    // AssetTools::CreateAsset wants the parent directory, not the full
    // package path — '/Game/Materials', not '/Game/Materials/M_Name'.
    const FString ParentDir = FPaths::GetPath(PackagePath);

    FAssetToolsModule &AssetToolsModule =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(
            TEXT("AssetTools"));
    UMaterialFactoryNew *Factory = NewObject<UMaterialFactoryNew>();
    UObject *Created = AssetToolsModule.Get().CreateAsset(
        AssetName, ParentDir, UMaterial::StaticClass(), Factory);
    UMaterial *Mat = Cast<UMaterial>(Created);
    if (!Mat)
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("CreateAsset failed for '%s'"), *Path));

    if (ShadingModel.Equals(TEXT("unlit"), ESearchCase::IgnoreCase)) {
      Mat->SetShadingModel(MSM_Unlit);
    } else {
      Mat->SetShadingModel(MSM_DefaultLit);
    }

    if (BlendMode.Equals(TEXT("masked"), ESearchCase::IgnoreCase))
      Mat->BlendMode = BLEND_Masked;
    else if (BlendMode.Equals(TEXT("translucent"), ESearchCase::IgnoreCase))
      Mat->BlendMode = BLEND_Translucent;
    else if (BlendMode.Equals(TEXT("additive"), ESearchCase::IgnoreCase))
      Mat->BlendMode = BLEND_Additive;
    else
      Mat->BlendMode = BLEND_Opaque;

    const bool bUnlit = Mat->GetShadingModels().HasShadingModel(MSM_Unlit);

    FLinearColor Color;
    if (!BaseColor.IsEmpty() && ParseLinearColor(BaseColor, Color)) {
      const EMaterialProperty Target = bUnlit ? MP_EmissiveColor : MP_BaseColor;
      ConnectColor(Mat, Color, Target, /*Y=*/-200);
    }
    if (!Emissive.IsEmpty() && ParseLinearColor(Emissive, Color)) {
      ConnectColor(Mat, Color, MP_EmissiveColor, /*Y=*/0);
    }
    if (bSetMetallic)
      ConnectScalar(Mat, (float)Metallic, MP_Metallic, /*Y=*/200);
    if (bSetRoughness)
      ConnectScalar(Mat, (float)Roughness, MP_Roughness, /*Y=*/300);
    if (bSetSpecular)
      ConnectScalar(Mat, (float)Specular, MP_Specular, /*Y=*/400);

    UMaterialEditingLibrary::RecompileMaterial(Mat);

    if (UEditorAssetSubsystem *AS =
            GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                    : nullptr) {
      AS->SaveLoadedAsset(Mat, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Mat->GetPathName());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateMaterialTool() {
  return MakeShared<FCreateMaterialTool>();
}
} // namespace UAgent
