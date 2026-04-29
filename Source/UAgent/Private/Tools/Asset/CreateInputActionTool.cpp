#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "InputAction.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"

namespace UAgent {
namespace {
bool ParseValueType(const FString &In, EInputActionValueType &Out,
                    FString &OutError) {
  if (In.IsEmpty() || In.Equals(TEXT("bool"), ESearchCase::IgnoreCase) ||
      In.Equals(TEXT("boolean"), ESearchCase::IgnoreCase)) {
    Out = EInputActionValueType::Boolean;
    return true;
  }
  if (In.Equals(TEXT("axis1d"), ESearchCase::IgnoreCase) ||
      In.Equals(TEXT("axis1"), ESearchCase::IgnoreCase)) {
    Out = EInputActionValueType::Axis1D;
    return true;
  }
  if (In.Equals(TEXT("axis2d"), ESearchCase::IgnoreCase) ||
      In.Equals(TEXT("axis2"), ESearchCase::IgnoreCase)) {
    Out = EInputActionValueType::Axis2D;
    return true;
  }
  if (In.Equals(TEXT("axis3d"), ESearchCase::IgnoreCase) ||
      In.Equals(TEXT("axis3"), ESearchCase::IgnoreCase)) {
    Out = EInputActionValueType::Axis3D;
    return true;
  }
  OutError = FString::Printf(
      TEXT("unknown valueType '%s' (expected bool, axis1d, axis2d, axis3d)"),
      *In);
  return false;
}

class FCreateInputActionTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_input_action");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Create a new UInputAction asset. valueType: 'bool' (default), "
                "'axis1d', 'axis2d', 'axis3d'. Saves the asset.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path":      { "type": "string", "description": "Target asset path, e.g. '/Game/Input/IA_Jump'" },
						"valueType": { "type": "string", "enum": ["bool", "axis1d", "axis2d", "axis3d"], "description": "Default 'bool'." }
					},
					"required": ["path"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString Path, ValueTypeStr;
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("valueType"), ValueTypeStr);
    if (Path.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("'path' is required"));

    EInputActionValueType ValueType = EInputActionValueType::Boolean;
    FString Err;
    if (!ParseValueType(ValueTypeStr, ValueType, Err)) {
      return FToolResponse::InvalidParams(Err);
    }

    FString PackageName, AssetName;
    if (!Common::SplitContentPath(Path, PackageName, AssetName, Err)) {
      return FToolResponse::InvalidParams(Err);
    }
    if (StaticFindObject(UPackage::StaticClass(), nullptr, *PackageName) !=
            nullptr ||
        FPackageName::DoesPackageExist(PackageName)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("asset already exists at '%s'"), *Path));
    }

    UPackage *Package = CreatePackage(*PackageName);
    if (!Package)
      return FToolResponse::Fail(
          -32000,
          FString::Printf(TEXT("CreatePackage failed for '%s'"), *PackageName));
    Package->FullyLoad();

    UInputAction *Action = NewObject<UInputAction>(
        Package, UInputAction::StaticClass(), FName(*AssetName),
        RF_Public | RF_Standalone | RF_Transactional);
    if (!Action)
      return FToolResponse::Fail(-32000, TEXT("NewObject returned null"));
    Action->ValueType = ValueType;

    FAssetRegistryModule::AssetCreated(Action);
    Package->MarkPackageDirty();

    if (UEditorAssetSubsystem *AS =
            GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                    : nullptr) {
      AS->SaveLoadedAsset(Action, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Action->GetPathName());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateInputActionTool() {
  return MakeShared<FCreateInputActionTool>();
}
} // namespace UAgent
