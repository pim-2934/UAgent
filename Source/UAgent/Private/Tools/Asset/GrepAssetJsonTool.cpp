#include "../../Protocol/ACPToolRegistry.h"
#include "../Blueprint/BlueprintSerialization.h"
#include "../BuiltinTools.h"
#include "../Common/PropertyToJson.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UAgent {
namespace {
FString SerializeAsset(UObject *Asset) {
  if (!Asset)
    return FString();

  TSharedPtr<FJsonObject> Obj;
  if (UBlueprint *BP = Cast<UBlueprint>(Asset)) {
    FString Err;
    Obj = BlueprintAccess::BuildBlueprintDump(BP->GetPathName(), INT_MAX, Err);
  } else if (UDataTable *DT = Cast<UDataTable>(Asset)) {
    Obj = MakeShared<FJsonObject>();
    TSharedRef<FJsonObject> Rows = MakeShared<FJsonObject>();
    if (DT->RowStruct) {
      for (const TPair<FName, uint8 *> &Pair : DT->GetRowMap()) {
        if (Pair.Value)
          Rows->SetObjectField(
              Pair.Key.ToString(),
              Common::PropertiesToJsonObject(DT->RowStruct, Pair.Value));
      }
    }
    Obj->SetObjectField(TEXT("rows"), Rows);
  } else {
    Obj = MakeShared<FJsonObject>();
    Obj->SetObjectField(TEXT("properties"), Common::PropertiesToJsonObject(
                                                Asset->GetClass(), Asset));
  }

  if (!Obj.IsValid())
    return FString();
  FString S;
  TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
  FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
  return S;
}

class FGrepAssetJsonTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/grep_asset_json");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Regex-match a pattern against serialized JSON dumps of assets. Slow — "
        "scope with pathPrefix and classPath. Stops at first `limit` matches.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"pattern": { "type": "string" },
						"pathPrefix": { "type": "string" },
						"classPath": { "type": "string" },
						"limit": { "type": "integer", "minimum": 1 }
					},
					"required": ["pattern"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString Pattern, PathPrefix, ClassPath;
    int32 Limit = 100;
    Params->TryGetStringField(TEXT("pattern"), Pattern);
    Params->TryGetStringField(TEXT("pathPrefix"), PathPrefix);
    Params->TryGetStringField(TEXT("classPath"), ClassPath);
    Params->TryGetNumberField(TEXT("limit"), Limit);
    if (Pattern.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing pattern"));
    if (Limit <= 0)
      Limit = 100;

    IAssetRegistry &Registry = FAssetRegistryModule::GetRegistry();
    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    if (!PathPrefix.IsEmpty())
      Filter.PackagePaths.Add(FName(*PathPrefix));
    if (!ClassPath.IsEmpty())
      Filter.ClassPaths.Add(FTopLevelAssetPath(FName(*ClassPath)));

    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);

    const FRegexPattern Rx(Pattern);

    TArray<TSharedPtr<FJsonValue>> Hits;
    int32 Scanned = 0;
    for (const FAssetData &A : Assets) {
      if (Hits.Num() >= Limit)
        break;
      UObject *Obj = A.GetAsset();
      if (!Obj)
        continue;
      ++Scanned;

      const FString Json = SerializeAsset(Obj);
      FRegexMatcher M(Rx, Json);
      if (M.FindNext()) {
        TSharedRef<FJsonObject> H = MakeShared<FJsonObject>();
        H->SetStringField(TEXT("path"), Obj->GetPathName());
        H->SetStringField(TEXT("match"),
                          Json.Mid(M.GetMatchBeginning(),
                                   FMath::Min(200, M.GetMatchEnding() -
                                                       M.GetMatchBeginning())));
        Hits.Add(MakeShared<FJsonValueObject>(H));
      }
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("hits"), Hits);
    Result->SetNumberField(TEXT("scanned"), Scanned);
    Result->SetBoolField(TEXT("truncated"), Hits.Num() >= Limit);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateGrepAssetJsonTool() {
  return MakeShared<FGrepAssetJsonTool>();
}
} // namespace UAgent
