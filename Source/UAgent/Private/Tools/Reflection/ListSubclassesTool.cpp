#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/ClassResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

namespace UAgent {
namespace {
class FListSubclassesTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/list_subclasses");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("List all native and Blueprint subclasses of the given base "
                "class. Use this to find Blueprints that extend a given class "
                "(e.g. all Character Blueprints in the project).");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"class": { "type": "string", "description": "Base class name or path" },
						"includeNative": { "type": "boolean", "description": "Include native C++ subclasses (default true)" },
						"includeBlueprint": { "type": "boolean", "description": "Include Blueprint subclasses (default true)" },
						"limit": { "type": "integer", "description": "Max results (default 500)", "minimum": 1 }
					},
					"required": ["class"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString ClassName;
    Params->TryGetStringField(TEXT("class"), ClassName);
    if (ClassName.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing 'class'"));

    bool bNative = true, bBP = true;
    int32 Limit = 500;
    Params->TryGetBoolField(TEXT("includeNative"), bNative);
    Params->TryGetBoolField(TEXT("includeBlueprint"), bBP);
    Params->TryGetNumberField(TEXT("limit"), Limit);
    if (Limit <= 0)
      Limit = 500;

    FString Err;
    UClass *Base = Common::ResolveClass(ClassName, Err);
    if (!Base)
      return FToolResponse::Fail(-32000, Err);

    TArray<TSharedPtr<FJsonValue>> Natives, BPs;

    if (bNative) {
      for (TObjectIterator<UClass> It; It; ++It) {
        UClass *C = *It;
        if (!C || C == Base)
          continue;
        if (!C->IsChildOf(Base))
          continue;
        if (C->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Deprecated))
          continue;
        // Skip BP generated — handled below via AssetRegistry.
        if (C->ClassGeneratedBy)
          continue;

        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("path"), C->GetPathName());
        J->SetStringField(TEXT("name"), C->GetName());
        Natives.Add(MakeShared<FJsonValueObject>(J));
        if (Natives.Num() + BPs.Num() >= Limit)
          break;
      }
    }

    if (bBP && (Natives.Num() + BPs.Num() < Limit)) {
      IAssetRegistry &Registry = FAssetRegistryModule::GetRegistry();
      TArray<FAssetData> BPAssets;
      FARFilter Filter;
      Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
      Filter.bRecursiveClasses = true;
      Registry.GetAssets(Filter, BPAssets);

      for (const FAssetData &A : BPAssets) {
        if (Natives.Num() + BPs.Num() >= Limit)
          break;
        const FString Native =
            A.GetTagValueRef<FString>(FName(TEXT("NativeParentClass")));
        const FString Parent =
            A.GetTagValueRef<FString>(FName(TEXT("ParentClass")));

        const bool bMatches =
            (!Native.IsEmpty() && Native.Contains(Base->GetPathName())) ||
            (!Parent.IsEmpty() && Parent.Contains(Base->GetPathName()));
        if (!bMatches)
          continue;

        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("path"), A.GetObjectPathString());
        J->SetStringField(TEXT("name"), A.AssetName.ToString());
        if (!Parent.IsEmpty())
          J->SetStringField(TEXT("parentClass"), Parent);
        BPs.Add(MakeShared<FJsonValueObject>(J));
      }
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("baseClass"), Base->GetPathName());
    Result->SetArrayField(TEXT("native"), Natives);
    Result->SetArrayField(TEXT("blueprint"), BPs);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateListSubclassesTool() {
  return MakeShared<FListSubclassesTool>();
}
} // namespace UAgent
