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
        "and the doctrine lives in the body. "
        "Returns {name, description, body, resources[]}. For directory-based "
        "skills (`<name>/SKILL.md` with sibling files), `resources` lists "
        "extra files (manifest.yaml, references/*.md, …) under the skill "
        "directory; call this tool again with the same `name` plus a "
        "`resource` argument set to one of those relative paths to load that "
        "file's contents instead of the SKILL.md body.");
  }

  virtual TSharedPtr<FJsonObject> GetInputSchema() const override {
    return ParseJsonObject(LR"JSON({
            "type": "object",
            "properties": {
              "name": {
                "type": "string",
                "description": "Skill slug as listed in the available-skills catalog (e.g. 'gas', 'replication', 'acfu-orchestrator')."
              },
              "resource": {
                "type": "string",
                "description": "Optional. Relative path (forward-slash) to a sibling file inside a directory-based skill, e.g. 'manifest.yaml' or 'references/acfu-reference.md'. Must be one of the values previously surfaced in the `resources` array. Omit to load the skill body."
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

    FString Resource;
    Params->TryGetStringField(TEXT("resource"), Resource);
    Resource = Resource.TrimStartAndEnd();

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

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Entry->Name);
    Result->SetStringField(TEXT("description"), Entry->Description);
    Result->SetBoolField(TEXT("fromProject"), Entry->bFromProject);

    if (Resource.IsEmpty()) {
      // Body path — load SKILL.md (or the flat .md) and advertise any
      // sibling resources so the agent knows it can call us again to fetch
      // them by name.
      FString Body;
      if (!Registry.LoadBody(*Entry, Body)) {
        return FToolResponse::Fail(
            -32000, FString::Printf(TEXT("Failed to read skill body from %s"),
                                    *Entry->FilePath));
      }
      Result->SetStringField(TEXT("body"), Body);

      TArray<TSharedPtr<FJsonValue>> Resources;
      Resources.Reserve(Entry->Resources.Num());
      for (const FString &R : Entry->Resources)
        Resources.Add(MakeShared<FJsonValueString>(R));
      Result->SetArrayField(TEXT("resources"), Resources);
    } else {
      // Resource path — load the named sibling file. Validation lives in the
      // registry so the same allowlist logic protects every caller.
      FString Content, Error;
      if (!Registry.LoadResource(*Entry, Resource, Content, Error))
        return FToolResponse::InvalidParams(Error);
      Result->SetStringField(TEXT("resource"), Resource);
      Result->SetStringField(TEXT("content"), Content);
    }

    return FToolResponse::Ok(Result);
  }
};
} // namespace

TSharedRef<IACPTool> CreateInvokeSkillTool() {
  return MakeShared<FInvokeSkillTool>();
}
} // namespace UAgent
