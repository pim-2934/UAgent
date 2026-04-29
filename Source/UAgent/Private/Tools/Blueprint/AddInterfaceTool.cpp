#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/ClassResolver.h"
#include "BlueprintQueries.h"

#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/Interface.h"
#include "UObject/TopLevelAssetPath.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
bool IsAlreadyImplemented(const UBlueprint *BP, const UClass *Iface) {
  for (const FBPInterfaceDescription &Desc : BP->ImplementedInterfaces) {
    if (Desc.Interface == Iface)
      return true;
  }
  return false;
}

class FAddInterfaceTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/add_interface");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Add an interface implementation to a Blueprint. Accepts C++ "
                "interface names, script paths (e.g. "
                "'/Script/Engine.Interface_PostProcessVolume'), or Blueprint "
                "interface asset paths. Compiles and saves by default.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath":  { "type": "string" },
						"interfaceClass": { "type": "string", "description": "Interface UClass name, script path, or Blueprint interface asset path" },
						"saveAsset":      { "type": "boolean" }
					},
					"required": ["blueprintPath", "interfaceClass"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, InterfaceName;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("interfaceClass"), InterfaceName);
    if (BPPath.IsEmpty() || InterfaceName.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath and interfaceClass required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    UClass *Iface = Common::ResolveClass(InterfaceName, Err);
    if (!Iface)
      return FToolResponse::Fail(-32000, Err);
    if (!Iface->HasAnyClassFlags(CLASS_Interface) ||
        Iface == UInterface::StaticClass()) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("%s is not an interface class"),
                                  *Iface->GetName()));
    }

    const bool bAlready = IsAlreadyImplemented(BP, Iface);
    if (bAlready) {
      TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
      R->SetStringField(TEXT("interface"), Iface->GetPathName());
      R->SetBoolField(TEXT("alreadyImplemented"), true);
      return FToolResponse::Ok(R);
    }

    const FScopedTransaction Tx(
        LOCTEXT("AddInterfaceTx", "Add Blueprint Interface"));
    BP->Modify();

    const FTopLevelAssetPath IfacePath(Iface);
    if (!FBlueprintEditorUtils::ImplementNewInterface(BP, IfacePath)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("ImplementNewInterface refused '%s'"),
                                  *Iface->GetPathName()));
    }

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("interface"), Iface->GetPathName());
    R->SetBoolField(TEXT("alreadyImplemented"), false);
    return FToolResponse::Ok(R);
  }
};
} // namespace

TSharedRef<IACPTool> CreateAddInterfaceTool() {
  return MakeShared<FAddInterfaceTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
