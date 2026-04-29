#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/ClassResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/DataAsset.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UAgent {
namespace {
class FCreateDataAssetTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_data_asset");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Create a new UDataAsset (or UPrimaryDataAsset) asset of a given "
        "subclass. Optional 'properties' applies ImportText values to the new "
        "instance. Works for config-style DataAsset classes like action sets, "
        "behavior configs, and initializer data.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"path": { "type": "string", "description": "Target asset path, e.g. '/Game/Data/DA_WarriorMoveset'" },
						"class": { "type": "string", "description": "UDataAsset subclass (short name, script path, or Blueprint class path)" },
						"properties": {
							"type": "object",
							"description": "Optional map of propertyName -> ImportText string, applied after creation.",
							"additionalProperties": { "type": "string" }
						}
					},
					"required": ["path", "class"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Path, ClassName;
    Params->TryGetStringField(TEXT("path"), Path);
    Params->TryGetStringField(TEXT("class"), ClassName);
    if (Path.IsEmpty() || ClassName.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("'path' and 'class' are required"));
    }

    FString Err;
    UClass *DataClass = Common::ResolveClass(ClassName, Err);
    if (!DataClass)
      return FToolResponse::Fail(-32000, Err);
    if (!DataClass->IsChildOf(UDataAsset::StaticClass())) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("%s is not a UDataAsset subclass"),
                                  *DataClass->GetName()));
    }
    if (DataClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated |
                                    CLASS_NewerVersionExists)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(
                      TEXT("%s cannot be instantiated (abstract/deprecated)"),
                      *DataClass->GetName()));
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

    UDataAsset *Asset =
        NewObject<UDataAsset>(Package, DataClass, FName(*AssetName),
                              RF_Public | RF_Standalone | RF_Transactional);
    if (!Asset)
      return FToolResponse::Fail(-32000, TEXT("NewObject returned null"));

    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();

    TArray<TSharedPtr<FJsonValue>> Failed;
    const TSharedPtr<FJsonObject> *PropMap = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), PropMap) && PropMap &&
        PropMap->IsValid()) {
      Asset->Modify();
      for (const auto &Pair : (*PropMap)->Values) {
        const FString &PropName = Pair.Key;
        FString PropValue;
        if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(PropValue)) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"),
                            TEXT("value must be a string (ImportText form)"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FProperty *Prop = DataClass->FindPropertyByName(FName(*PropName));
        if (!Prop) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(
              TEXT("error"),
              FString::Printf(TEXT("not found on %s"), *DataClass->GetName()));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        void *Addr = Prop->ContainerPtrToValuePtr<void>(Asset);
        if (!Prop->ImportText_Direct(*PropValue, Addr, Asset, PPF_None)) {
          TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
          F->SetStringField(TEXT("property"), PropName);
          F->SetStringField(TEXT("error"), TEXT("ImportText failed"));
          Failed.Add(MakeShared<FJsonValueObject>(F));
          continue;
        }

        FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
        Asset->PostEditChangeProperty(Evt);
      }
    }

    if (UEditorAssetSubsystem *AS =
            GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                    : nullptr) {
      AS->SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Asset->GetPathName());
    Result->SetStringField(TEXT("class"), DataClass->GetPathName());
    if (Failed.Num() > 0)
      Result->SetArrayField(TEXT("failedProperties"), Failed);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateDataAssetTool() {
  return MakeShared<FCreateDataAssetTool>();
}
} // namespace UAgent
