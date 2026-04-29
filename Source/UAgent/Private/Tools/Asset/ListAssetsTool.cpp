#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/TopLevelAssetPath.h"

namespace UAgent {
namespace {
class FListAssetsTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/list_assets");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "List assets from the AssetRegistry by class, path prefix and/or name "
        "substring. Use this to discover assets in the project (e.g. all "
        "Blueprint subclasses under /Game/MyFolder, all DataTables). Returns "
        "an array of {path, class, parentClass, tags}.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"classPath": { "type": "string", "description": "Filter by class path, e.g. /Script/Engine.Blueprint or /Script/Engine.Character" },
						"pathPrefix": { "type": "string", "description": "Only assets under this content path, e.g. /Game/MyFolder" },
						"nameContains": { "type": "string", "description": "Case-insensitive substring match on asset name" },
						"recursive": { "type": "boolean", "description": "Recurse into subfolders of pathPrefix. Default true." },
						"limit": { "type": "integer", "description": "Max results (default 200)", "minimum": 1 }
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    FString ClassPath, PathPrefix, NameContains;
    bool bRecursive = true;
    int32 Limit = 200;
    if (Params.IsValid()) {
      Params->TryGetStringField(TEXT("classPath"), ClassPath);
      Params->TryGetStringField(TEXT("pathPrefix"), PathPrefix);
      Params->TryGetStringField(TEXT("nameContains"), NameContains);
      Params->TryGetBoolField(TEXT("recursive"), bRecursive);
      Params->TryGetNumberField(TEXT("limit"), Limit);
    }
    if (Limit <= 0)
      Limit = 200;

    IAssetRegistry &Registry = FAssetRegistryModule::GetRegistry();

    FARFilter Filter;
    Filter.bRecursivePaths = bRecursive;
    Filter.bRecursiveClasses = true;
    if (!ClassPath.IsEmpty()) {
      Filter.ClassPaths.Add(FTopLevelAssetPath(FName(*ClassPath)));
    }
    if (!PathPrefix.IsEmpty()) {
      Filter.PackagePaths.Add(FName(*PathPrefix));
    }

    TArray<FAssetData> Results;
    Registry.GetAssets(Filter, Results);

    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Reserve(FMath::Min(Results.Num(), Limit));
    for (const FAssetData &A : Results) {
      if (Items.Num() >= Limit)
        break;
      if (!NameContains.IsEmpty() &&
          !A.AssetName.ToString().Contains(NameContains,
                                           ESearchCase::IgnoreCase)) {
        continue;
      }

      TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
      Item->SetStringField(TEXT("name"), A.AssetName.ToString());
      Item->SetStringField(TEXT("path"), A.GetObjectPathString());
      Item->SetStringField(TEXT("class"), A.AssetClassPath.ToString());

      const FString ParentTag =
          A.GetTagValueRef<FString>(FName(TEXT("ParentClass")));
      if (!ParentTag.IsEmpty())
        Item->SetStringField(TEXT("parentClass"), ParentTag);

      TSharedRef<FJsonObject> Tags = MakeShared<FJsonObject>();
      A.EnumerateTags([&Tags](const TPair<FName, FAssetTagValueRef> &Pair) {
        Tags->SetStringField(Pair.Key.ToString(), Pair.Value.AsString());
      });
      Item->SetObjectField(TEXT("tags"), Tags);

      Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("assets"), Items);
    Result->SetNumberField(TEXT("total"), Results.Num());
    Result->SetBoolField(TEXT("truncated"), Results.Num() > Items.Num());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateListAssetsTool() {
  return MakeShared<FListAssetsTool>();
}
} // namespace UAgent
