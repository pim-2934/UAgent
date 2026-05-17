#include "../../Protocol/ACPToolRegistry.h"
#include "../../Skills/SkillRegistry.h"
#include "../BuiltinTools.h"

namespace UAgent {
namespace {
class FInvokeSkillTool : public IACPTool {
public:
  virtual FString GetMethod() const override {
    return TEXT("_ue5/invoke_skill");
  }

  // Reads a markdown file shipped with the plugin (or a sibling under
  // <ProjectDir>/UAgent/Skills). Touches no project data — classified as
  // read-only so Default permission mode auto-allows without prompting.
  virtual bool IsReadOnly() const override { return true; }

  virtual FString GetDescription() const override {
    return TEXT(
        "Load the full body of a UAgent skill — an opinionated guide for a "
        "UE5 subsystem (e.g. GAS, Replication, Enhanced Input) or a "
        "project-specific topic. The available skills are listed in the "
        "system context block prepended to each session's first prompt. "
        "Call this BEFORE writing code or invoking other tools on a topic "
        "covered by a skill — the catalog entries are intentionally terse "
        "and the doctrine lives in the body. Returns {name, description, "
        "body}.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
            "type": "object",
            "properties": {
              "name": {
                "type": "string",
                "description": "Skill slug as listed in the available-skills catalog (e.g. 'gas', 'replication')."
              }
            },
            "required": ["name"]
          })JSON");
  }

  virtual FToolResponse
  Execute(const TSharedPtr<FJsonObject> &Params) override {
    if (!Params.IsValid())
      return FToolResponse::InvalidParams(TEXT("missing params"));

    FString Name;
    Params->TryGetStringField(TEXT("name"), Name);
    Name = Name.TrimStartAndEnd();
    if (Name.IsEmpty())
      return FToolResponse::InvalidParams(TEXT("missing 'name'"));

    FSkillRegistry &Registry = FSkillRegistry::Get();
    const FSkillEntry *Entry = Registry.Find(Name);
    if (!Entry) {
      const TArray<FSkillEntry> &All = Registry.GetAll();
      TArray<FString> AvailableNames;
      AvailableNames.Reserve(All.Num());
      for (const FSkillEntry &E : All)
        AvailableNames.Add(E.Name);
      const FString Available = AvailableNames.Num() > 0
                                    ? FString::Join(AvailableNames, TEXT(", "))
                                    : TEXT("(no skills registered)");
      return FToolResponse::InvalidParams(FString::Printf(
          TEXT("Unknown skill '%s'. Available: %s"), *Name, *Available));
    }

    FString Body;
    if (!Registry.LoadBody(*Entry, Body)) {
      return FToolResponse::Fail(
          -32000, FString::Printf(TEXT("Failed to read skill body from %s"),
                                  *Entry->FilePath));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Entry->Name);
    Result->SetStringField(TEXT("description"), Entry->Description);
    Result->SetStringField(TEXT("body"), Body);
    Result->SetBoolField(TEXT("fromProject"), Entry->bFromProject);
    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateInvokeSkillTool() {
  return MakeShared<FInvokeSkillTool>();
}
} // namespace UAgent
