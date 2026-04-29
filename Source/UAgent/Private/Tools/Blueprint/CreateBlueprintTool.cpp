#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/ClassResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UAgent {
namespace {
class FCreateBlueprintTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_blueprint");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Create a new Blueprint deriving from the given parent class. "
                "Returns the new asset's path. Saves by default.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"parentClass": { "type": "string", "description": "Parent class name or path, e.g. 'Character' or '/Script/Engine.Actor'" },
						"assetPath": { "type": "string", "description": "New Blueprint path, e.g. /Game/MyGame/BP_MyChar" },
						"saveAsset": { "type": "boolean", "description": "Save the new asset (default true)" }
					},
					"required": ["parentClass", "assetPath"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ParentName, AssetPath;
    Params->TryGetStringField(TEXT("parentClass"), ParentName);
    Params->TryGetStringField(TEXT("assetPath"), AssetPath);
    if (ParentName.IsEmpty() || AssetPath.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("parentClass and assetPath required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    FString Err;
    UClass *Parent = Common::ResolveClass(ParentName, Err);
    if (!Parent)
      return FToolResponse::Fail(-32000, Err);

    FString PackagePath, AssetName;
    if (!Common::SplitContentPath(AssetPath, PackagePath, AssetName, Err))
      return FToolResponse::InvalidParams(Err);

    UPackage *Pkg = CreatePackage(*PackagePath);
    if (!Pkg)
      return FToolResponse::Fail(-32000, TEXT("CreatePackage failed"));
    Pkg->FullyLoad();

    // FKismetEditorUtilities::CreateBlueprint asserts the name is unused
    // (Kismet2.cpp `check(FindObject<UBlueprint>(...) == NULL)`), which
    // crashes the editor rather than returning an error. Pre-check so the
    // collision surfaces as a tool error the agent can recover from.
    if (FindObject<UObject>(Pkg, *AssetName)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(
                      TEXT("asset already exists at '%s' — pick a different "
                           "assetPath or delete/rename the existing asset"),
                      *AssetPath));
    }

    UBlueprint *BP = FKismetEditorUtilities::CreateBlueprint(
        Parent, Pkg, FName(*AssetName), BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
        FName(TEXT("UAgent")));
    if (!BP)
      return FToolResponse::Fail(-32000, TEXT("CreateBlueprint failed"));

    FAssetRegistryModule::AssetCreated(BP);
    BP->MarkPackageDirty();

    if (bSave) {
      UEditorAssetLibrary::SaveAsset(PackagePath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), BP->GetPathName());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateBlueprintTool() {
  return MakeShared<FCreateBlueprintTool>();
}
} // namespace UAgent
