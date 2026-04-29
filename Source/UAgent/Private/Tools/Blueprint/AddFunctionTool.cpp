#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"
#include "../Common/AssetPathUtils.h"
#include "BlueprintQueries.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UAgent"

namespace UAgent {
namespace {
class FAddFunctionTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/add_function");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT("Add an empty user function graph to a Blueprint. Returns the "
                "new graph name. Use add_variable with the same BP to add "
                "parameters (via the function's internal entry node), or "
                "follow up with create_node targeting the new graph.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"blueprintPath": { "type": "string" },
						"name": { "type": "string", "description": "Function name" },
						"saveAsset": { "type": "boolean" }
					},
					"required": ["blueprintPath", "name"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));
    FString BPPath, Name;
    Params->TryGetStringField(TEXT("blueprintPath"), BPPath);
    Params->TryGetStringField(TEXT("name"), Name);
    if (BPPath.IsEmpty() || Name.IsEmpty()) {
      return FToolResponse::InvalidParams(
          TEXT("blueprintPath and name required"));
    }
    bool bSave = true;
    Params->TryGetBoolField(TEXT("saveAsset"), bSave);

    FString Err;
    UBlueprint *BP = BlueprintAccess::LoadBlueprintByPath(BPPath, Err);
    if (!BP)
      return FToolResponse::Fail(-32000, Err);

    const FScopedTransaction Tx(LOCTEXT("AddFnTx", "Add Blueprint Function"));
    BP->Modify();

    const FName FnName(*Name);
    const FName UniqueName =
        FBlueprintEditorUtils::FindUniqueKismetName(BP, FnName.ToString());
    UEdGraph *NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        BP, UniqueName, UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph<UClass>(
        BP, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromClass=*/nullptr);

    FKismetEditorUtilities::CompileBlueprint(BP);
    if (bSave) {
      FString PkgPath, PkgName, PkgErr;
      Common::SplitContentPath(BP->GetPathName(), PkgPath, PkgName, PkgErr);
      UEditorAssetLibrary::SaveAsset(PkgPath, /*bOnlyIfIsDirty=*/false);
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), UniqueName.ToString());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateAddFunctionTool() {
  return MakeShared<FAddFunctionTool>();
}
} // namespace UAgent

#undef LOCTEXT_NAMESPACE
