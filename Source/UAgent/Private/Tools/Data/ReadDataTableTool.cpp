#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "../Common/PropertyToJson.h"

#include "Engine/DataTable.h"

namespace UAgent {
namespace {
class FReadDataTableTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/read_data_table");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT("Dump rows of a UDataTable as JSON keyed by row name. Pass "
                "rowNames to filter.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"assetPath": { "type": "string", "description": "Path to the DataTable asset" },
						"rowNames": { "type": "array", "items": { "type": "string" }, "description": "Optional subset of row names to return. Omit for all rows." }
					},
					"required": ["assetPath"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString AssetPath;
    Params->TryGetStringField(TEXT("assetPath"), AssetPath);
    if (AssetPath.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing assetPath"));

    FString Err;
    UObject *Loaded =
        Common::LoadAssetByPath(AssetPath, UDataTable::StaticClass(), Err);
    if (!Loaded)
      return FToolResponse::Fail(-32000, Err);
    UDataTable *Table = Cast<UDataTable>(Loaded);

    TSet<FName> Filter;
    const TArray<TSharedPtr<FJsonValue>> *FilterArr = nullptr;
    if (Params->TryGetArrayField(TEXT("rowNames"), FilterArr)) {
      for (const TSharedPtr<FJsonValue> &V : *FilterArr) {
        FString S;
        if (V.IsValid() && V->TryGetString(S))
          Filter.Add(FName(*S));
      }
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("path"), Table->GetPathName());
    if (Table->RowStruct)
      Root->SetStringField(TEXT("rowStruct"), Table->RowStruct->GetPathName());

    TSharedRef<FJsonObject> Rows = MakeShared<FJsonObject>();
    const TMap<FName, uint8 *> &RowMap = Table->GetRowMap();
    for (const TPair<FName, uint8 *> &Pair : RowMap) {
      if (Filter.Num() > 0 && !Filter.Contains(Pair.Key))
        continue;
      if (!Pair.Value || !Table->RowStruct)
        continue;
      Rows->SetObjectField(
          Pair.Key.ToString(),
          Common::PropertiesToJsonObject(Table->RowStruct, Pair.Value));
    }
    Root->SetObjectField(TEXT("rows"), Rows);
    return FToolResponse::Ok(Root);
  }
};
} // namespace

TSharedRef<IACPTool> CreateReadDataTableTool() {
  return MakeShared<FReadDataTableTool>();
}
} // namespace UAgent
