#include "SkillRegistry.h"
#include "../Protocol/ACPTypes.h" // for LogUAgent

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UAgent {

FSkillRegistry &FSkillRegistry::Get() {
  static FSkillRegistry Instance;
  return Instance;
}

void FSkillRegistry::ScanIfNeeded() {
  if (bScanned)
    return;

  TMap<FString, FSkillEntry> ByName;

  // Plugin-shipped skills first — overridable by project entries.
  if (const TSharedPtr<IPlugin> Plugin =
          IPluginManager::Get().FindPlugin(TEXT("UAgent"))) {
    const FString PluginSkillsDir =
        Plugin->GetBaseDir() / TEXT("Resources/Skills");
    ScanDirectory(PluginSkillsDir, /*bIsProjectSource=*/false, ByName);
  }

  // Project-shipped skills — override plugin entries with the same name.
  const FString ProjectSkillsDir = FPaths::ProjectDir() / TEXT("UAgent/Skills");
  ScanDirectory(ProjectSkillsDir, /*bIsProjectSource=*/true, ByName);

  Entries.Reset();
  ByName.GenerateValueArray(Entries);
  Entries.Sort([](const FSkillEntry &A, const FSkillEntry &B) {
    return A.Name < B.Name;
  });

  bScanned = true;
}

void FSkillRegistry::ForceRescan() {
  bScanned = false;
  Entries.Reset();
}

const TArray<FSkillEntry> &FSkillRegistry::GetAll() {
  ScanIfNeeded();
  return Entries;
}

const FSkillEntry *FSkillRegistry::Find(const FString &Name) {
  ScanIfNeeded();
  for (const FSkillEntry &Entry : Entries) {
    if (Entry.Name == Name)
      return &Entry;
  }
  return nullptr;
}

bool FSkillRegistry::LoadBody(const FSkillEntry &Entry,
                              FString &OutBody) const {
  FString Content;
  if (!FFileHelper::LoadFileToString(Content, *Entry.FilePath))
    return false;

  FString Unused1, Unused2;
  int32 BodyStart = 0;
  if (!ParseFrontmatter(Content, Unused1, Unused2, BodyStart))
    return false;

  OutBody = Content.Mid(BodyStart);
  return true;
}

FString FSkillRegistry::BuildCatalogBlock() {
  ScanIfNeeded();
  if (Entries.Num() == 0)
    return FString();

  // The block is read once per session, prepended alongside AGENTS.md. Frame
  // it as instructions, not just a list — the agent needs to know *when* to
  // call invoke_skill, not only that the skills exist.
  FString Block = TEXT(
      "Available UAgent skills — opinionated guides for UE5 subsystems and "
      "frameworks shipped with this plugin. Call the `invoke_skill` tool "
      "with the skill's name BEFORE acting on a request that touches one of "
      "these topics — the catalog below is intentionally terse, the full "
      "body lives behind the tool. Skills carry doctrine and multi-step "
      "recipes; the registered editor tools carry single engine actions. "
      "Use both together.\n\n");

  for (const FSkillEntry &Entry : Entries) {
    Block +=
        FString::Printf(TEXT("- %s — %s\n"), *Entry.Name, *Entry.Description);
  }

  return Block;
}

void FSkillRegistry::ScanDirectory(const FString &Dir, bool bIsProjectSource,
                                   TMap<FString, FSkillEntry> &OutByName) {
  if (!FPaths::DirectoryExists(Dir))
    return;

  TArray<FString> Files;
  IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.md")),
                                /*Files=*/true, /*Directories=*/false);

  for (const FString &File : Files) {
    const FString FullPath = Dir / File;

    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *FullPath)) {
      UE_LOG(LogUAgent, Warning, TEXT("Skill file unreadable, skipping: %s"),
             *FullPath);
      continue;
    }

    FString Name, Description;
    int32 BodyStart = 0;
    if (!ParseFrontmatter(Content, Name, Description, BodyStart)) {
      UE_LOG(LogUAgent, Warning,
             TEXT("Skill file %s: missing or malformed frontmatter (need "
                  "`name:` and `description:` between `---` delimiters); "
                  "skipping"),
             *FullPath);
      continue;
    }

    FSkillEntry Entry;
    Entry.Name = MoveTemp(Name);
    Entry.Description = MoveTemp(Description);
    Entry.FilePath = FullPath;
    Entry.bFromProject = bIsProjectSource;

    if (bIsProjectSource && OutByName.Contains(Entry.Name)) {
      UE_LOG(LogUAgent, Display,
             TEXT("Project skill '%s' from %s overrides plugin-shipped skill"),
             *Entry.Name, *FullPath);
    }

    OutByName.Add(Entry.Name, Entry);
  }
}

bool FSkillRegistry::ParseFrontmatter(const FString &Content, FString &OutName,
                                      FString &OutDescription,
                                      int32 &OutBodyStart) {
  // Frontmatter must begin with `---` followed by a newline. Tolerate UTF-8
  // BOM and Windows CRLF — engine builds open files cross-platform.
  int32 Cursor = 0;
  if (Content.Len() >= 3 && Content[0] == TEXT('\xFEFF'))
    Cursor = 1; // skip BOM
  if (!Content.Mid(Cursor).StartsWith(TEXT("---")))
    return false;
  Cursor += 3;
  // Require a newline immediately after the opening `---` (no inline content).
  if (Cursor >= Content.Len() ||
      (Content[Cursor] != TEXT('\n') && Content[Cursor] != TEXT('\r')))
    return false;

  // Find the closing `---` at the start of a line. We accept either CRLF or
  // LF line endings; FindChar would over-match on `---` inside the body.
  int32 EndIdx = INDEX_NONE;
  {
    int32 SearchFrom = Cursor;
    while (SearchFrom < Content.Len()) {
      const int32 NewlineIdx =
          Content.Find(TEXT("\n"), ESearchCase::CaseSensitive,
                       ESearchDir::FromStart, SearchFrom);
      if (NewlineIdx == INDEX_NONE)
        return false;
      const int32 LineStart = NewlineIdx + 1;
      // A closing fence is `---` (optionally followed by whitespace + newline).
      const bool bIsFence = (LineStart + 3 <= Content.Len()) &&
                            Content[LineStart] == TEXT('-') &&
                            Content[LineStart + 1] == TEXT('-') &&
                            Content[LineStart + 2] == TEXT('-');
      if (bIsFence) {
        EndIdx = LineStart;
        break;
      }
      SearchFrom = NewlineIdx + 1;
    }
  }
  if (EndIdx == INDEX_NONE)
    return false;

  const FString Frontmatter = Content.Mid(Cursor, EndIdx - Cursor);

  // Body starts at the newline after the closing fence — skip past `---`
  // and any trailing CR/LF on that line.
  int32 BodyCursor = EndIdx + 3;
  if (BodyCursor < Content.Len() && Content[BodyCursor] == TEXT('\r'))
    ++BodyCursor;
  if (BodyCursor < Content.Len() && Content[BodyCursor] == TEXT('\n'))
    ++BodyCursor;
  OutBodyStart = BodyCursor;

  // Parse key:value lines. Only `name` and `description` are recognized —
  // unknown keys are ignored so we can add more later without breaking older
  // files.
  TArray<FString> Lines;
  Frontmatter.ParseIntoArrayLines(Lines, /*bCullEmpty=*/true);
  for (const FString &Line : Lines) {
    int32 ColonIdx = INDEX_NONE;
    if (!Line.FindChar(TEXT(':'), ColonIdx))
      continue;
    const FString Key = Line.Left(ColonIdx).TrimStartAndEnd();
    FString Value = Line.Mid(ColonIdx + 1).TrimStartAndEnd();
    // Strip optional surrounding quotes — both styles are common in YAML.
    if (Value.Len() >= 2 &&
        ((Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\""))) ||
         (Value.StartsWith(TEXT("'")) && Value.EndsWith(TEXT("'"))))) {
      Value = Value.Mid(1, Value.Len() - 2);
    }
    if (Key.Equals(TEXT("name"), ESearchCase::IgnoreCase))
      OutName = Value;
    else if (Key.Equals(TEXT("description"), ESearchCase::IgnoreCase))
      OutDescription = Value;
  }

  return !OutName.IsEmpty() && !OutDescription.IsEmpty();
}

} // namespace UAgent
