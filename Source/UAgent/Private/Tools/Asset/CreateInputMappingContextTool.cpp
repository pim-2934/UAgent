#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"

namespace UAgent {
namespace {
class FCreateInputMappingContextTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_input_mapping_context");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Create a new UInputMappingContext asset. Optional 'mappings' "
                "is an array of {action, key} pairs; 'action' is a "
                "UInputAction asset path, 'key' is an FKey name like "
                "'SpaceBar' or 'Gamepad_FaceButton_Bottom'. Saves the asset.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path": { "type": "string", "description": "Target asset path, e.g. '/Game/Input/IMC_Default'" },
						"mappings": {
							"type": "array",
							"items": {
								"type": "object",
								"properties": {
									"action": { "type": "string", "description": "Path to a UInputAction asset, e.g. '/Game/Input/IA_Jump'" },
									"key":    { "type": "string", "description": "FKey name, e.g. 'SpaceBar', 'Gamepad_FaceButton_Bottom', 'W'" }
								},
								"required": ["action", "key"]
							}
						}
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

    FString PackageName, AssetName, Err;
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

    UInputMappingContext *IMC = NewObject<UInputMappingContext>(
        Package, UInputMappingContext::StaticClass(), FName(*AssetName),
        RF_Public | RF_Standalone | RF_Transactional);
    if (!IMC)
      return FToolResponse::Fail(-32000, TEXT("NewObject returned null"));

    FAssetRegistryModule::AssetCreated(IMC);
    Package->MarkPackageDirty();

    TArray<TSharedPtr<FJsonValue>> Failed;
    int32 Added = 0;
    const TArray<TSharedPtr<FJsonValue>> *Mappings = nullptr;
    if (Params->TryGetArrayField(TEXT("mappings"), Mappings) && Mappings) {
      IMC->Modify();
      for (const TSharedPtr<FJsonValue> &Entry : *Mappings) {
        if (!Entry.IsValid() || Entry->Type != EJson::Object) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("error"),
                            TEXT("mapping entry must be an object"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }
        const TSharedPtr<FJsonObject> &Obj = Entry->AsObject();
        FString ActionPath, KeyName;
        Obj->TryGetStringField(TEXT("action"), ActionPath);
        Obj->TryGetStringField(TEXT("key"), KeyName);
        if (ActionPath.IsEmpty() || KeyName.IsEmpty()) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("error"),
                            TEXT("mapping requires 'action' and 'key'"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FString LoadErr;
        UObject *Obj2 = Common::LoadAssetByPath(
            ActionPath, UInputAction::StaticClass(), LoadErr);
        UInputAction *Action = Cast<UInputAction>(Obj2);
        if (!Action) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("action"), ActionPath);
          F->SetStringField(TEXT("error"), LoadErr.IsEmpty()
                                               ? TEXT("action not found")
                                               : LoadErr);
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        const FKey Key(*KeyName);
        if (!Key.IsValid()) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("key"), KeyName);
          F->SetStringField(TEXT("error"), TEXT("unknown FKey name"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        IMC->MapKey(Action, Key);
        ++Added;
      }
    }

    if (UEditorAssetSubsystem *AS =
            GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                    : nullptr) {
      AS->SaveLoadedAsset(IMC, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), IMC->GetPathName());
    Result->SetNumberField(TEXT("mappingsAdded"), Added);
    if (Failed.Num() > 0)
      Result->SetArrayField(TEXT("failedMappings"), Failed);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateInputMappingContextTool() {
  return MakeShared<FCreateInputMappingContextTool>();
}
} // namespace UAgent
