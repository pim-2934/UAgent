#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/PropertyToJson.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "InputAction.h"
#include "InputMappingContext.h"

namespace UAgent {
namespace {
class FReadInputMappingsTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/read_input_mappings");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("List Enhanced Input actions and mapping contexts in the "
                "project, with their key bindings and modifiers. Use to see "
                "how the project wires input to gameplay actions.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"pathPrefix": { "type": "string", "description": "Restrict scan to a content path" }
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    FString PathPrefix;
    if (Params.IsValid())
      Params->TryGetStringField(TEXT("pathPrefix"), PathPrefix);

    IAssetRegistry &Registry = FAssetRegistryModule::GetRegistry();
    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(
        UInputMappingContext::StaticClass()->GetClassPathName());
    if (!PathPrefix.IsEmpty())
      Filter.PackagePaths.Add(FName(*PathPrefix));

    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Actions, Contexts;
    for (const FAssetData &A : Assets) {
      UObject *Loaded = A.GetAsset();
      if (!Loaded)
        continue;

      if (UInputAction *Act = Cast<UInputAction>(Loaded)) {
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("path"), Act->GetPathName());
        J->SetObjectField(TEXT("properties"),
                          Common::PropertiesToJsonObject(Act->GetClass(), Act));
        Actions.Add(MakeShared<FJsonValueObject>(J));
      } else if (UInputMappingContext *Ctx =
                     Cast<UInputMappingContext>(Loaded)) {
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("path"), Ctx->GetPathName());
        J->SetObjectField(TEXT("properties"),
                          Common::PropertiesToJsonObject(Ctx->GetClass(), Ctx));
        Contexts.Add(MakeShared<FJsonValueObject>(J));
      }
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("actions"), Actions);
    Result->SetArrayField(TEXT("contexts"), Contexts);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateReadInputMappingsTool() {
  return MakeShared<FReadInputMappingsTool>();
}
} // namespace UAgent
