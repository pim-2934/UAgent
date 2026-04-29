#include "../../Protocol/ACPToolRegistry.h"
#include "../BuiltinTools.h"

#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Modules/ModuleManager.h"

namespace UAgent {
namespace {
class FCreateGameplayTagTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/create_gameplay_tag");
  }

  virtual bool IsReadOnly() const override { return false; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Register a new gameplay tag in the project's tag INIs via "
        "IGameplayTagsEditorModule. If the tag already exists, returns "
        "alreadyExists=true without rewriting the INI. Parent tags are implied "
        "('Ability.Attack.Heavy' requires 'Ability' and 'Ability.Attack' to "
        "also exist or to be created by this call).");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
					"type": "object",
					"properties": {
						"tag":     { "type": "string", "description": "Dotted tag name, e.g. 'Ability.Attack.Heavy'" },
						"comment": { "type": "string", "description": "Optional tooltip/comment stored next to the tag." },
						"source":  { "type": "string", "description": "Optional INI source name (default 'DefaultGameplayTags.ini'). Rarely needed." },
						"restricted":                   { "type": "boolean", "description": "Create as a restricted tag. Default false." },
						"allowNonRestrictedChildren":   { "type": "boolean", "description": "Only meaningful when restricted=true. Default true." }
					},
					"required": ["tag"]
				})JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Tag, Comment, Source;
    Params->TryGetStringField(TEXT("tag"), Tag);
    Params->TryGetStringField(TEXT("comment"), Comment);
    Params->TryGetStringField(TEXT("source"), Source);
    if (Tag.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("'tag' is required"));

    bool bRestricted = false;
    bool bAllowNonRestrictedChildren = true;
    Params->TryGetBoolField(TEXT("restricted"), bRestricted);
    Params->TryGetBoolField(TEXT("allowNonRestrictedChildren"),
                            bAllowNonRestrictedChildren);

    UGameplayTagsManager &Mgr = UGameplayTagsManager::Get();
    const FGameplayTag Existing =
        Mgr.RequestGameplayTag(FName(*Tag), /*ErrorIfNotFound=*/false);
    if (Existing.IsValid()) {
      TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
      R->SetStringField(TEXT("tag"), Tag);
      R->SetBoolField(TEXT("alreadyExists"), true);
      return FToolResponse::Ok(R);
    }

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("GameplayTagsEditor"))) {
      FModuleManager::Get().LoadModuleChecked<IGameplayTagsEditorModule>(
          TEXT("GameplayTagsEditor"));
    }
    IGameplayTagsEditorModule &TagsEditor = IGameplayTagsEditorModule::Get();

    const FName SourceName = Source.IsEmpty() ? NAME_None : FName(*Source);
    if (!TagsEditor.AddNewGameplayTagToINI(Tag, Comment, SourceName,
                                           bRestricted,
                                           bAllowNonRestrictedChildren)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(
                      TEXT("AddNewGameplayTagToINI failed for '%s'"), *Tag));
    }

    TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetStringField(TEXT("tag"), Tag);
    R->SetBoolField(TEXT("alreadyExists"), false);
    if (!Source.IsEmpty())
      R->SetStringField(TEXT("source"), Source);
    return FToolResponse::Ok(R);
  }
};
} // namespace

TSharedRef<IACPTool> CreateCreateGameplayTagTool() {
  return MakeShared<FCreateGameplayTagTool>();
}
} // namespace UAgent
