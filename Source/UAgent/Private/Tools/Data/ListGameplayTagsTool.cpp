#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

namespace UAgent {
namespace {
class FListGameplayTagsTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/list_gameplay_tags");
  }

  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "List registered gameplay tags, optionally filtered by prefix.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"prefix": { "type": "string", "description": "Only tags starting with this prefix, e.g. 'DamageType'" },
						"limit": { "type": "integer", "minimum": 1 }
					}
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    FString Prefix;
    int32 Limit = 1000;
    if (Params.IsValid()) {
      Params->TryGetStringField(TEXT("prefix"), Prefix);
      Params->TryGetNumberField(TEXT("limit"), Limit);
    }
    if (Limit <= 0)
      Limit = 1000;

    FGameplayTagContainer Container;
    UGameplayTagsManager::Get().RequestAllGameplayTags(
        Container, /*OnlyIncludeDictionaryTags=*/true);

    TArray<FGameplayTag> Tags;
    Container.GetGameplayTagArray(Tags);

    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FGameplayTag &T : Tags) {
      if (Out.Num() >= Limit)
        break;
      const FString Name = T.ToString();
      if (!Prefix.IsEmpty() && !Name.StartsWith(Prefix))
        continue;
      Out.Add(MakeShared<FJsonValueString>(Name));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("tags"), Out);
    Result->SetNumberField(TEXT("total"), Tags.Num());
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateListGameplayTagsTool() {
  return MakeShared<FListGameplayTagsTool>();
}
} // namespace UAgent
