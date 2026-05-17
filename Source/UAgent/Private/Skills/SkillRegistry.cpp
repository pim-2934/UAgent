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

bool FSkillRegistry::LoadResource(const FSkillEntry &Entry,
                                  const FString &RelPath, FString &OutContent,
                                  FString &OutError) const {
  if (Entry.BaseDir.IsEmpty()) {
    OutError = FString::Printf(
        TEXT("Skill '%s' is a flat single-file skill and has no resources. "
             "Only directory-based skills (`<name>/SKILL.md` + siblings) can "
             "carry additional resource files."),
        *Entry.Name);
    return false;
  }

  // Reject empty paths, absolute paths, and any `..` segment up front.
  // Resource paths are bridge-style — opaque slugs the agent quotes back to
  // us, not arbitrary filesystem references — so a strict allowlist is the
  // right shape even though FPaths::Collapse would catch most escapes.
  const FString Trimmed = RelPath.TrimStartAndEnd();
  if (Trimmed.IsEmpty()) {
    OutError = TEXT("Resource path is empty.");
    return false;
  }
  if (Trimmed.StartsWith(TEXT("/")) || Trimmed.StartsWith(TEXT("\\")) ||
      (Trimmed.Len() >= 2 && Trimmed[1] == TEXT(':'))) {
    OutError = TEXT("Resource path must be relative to the skill directory.");
    return false;
  }
  if (Trimmed.Contains(TEXT(".."))) {
    OutError = TEXT("Resource path may not contain `..` segments.");
    return false;
  }
  // SKILL.md is loaded via LoadBody — refusing it here makes invoke_skill's
  // behavior single-purpose per call (body OR resource, never ambiguous).
  if (Trimmed.Equals(TEXT("SKILL.md"), ESearchCase::IgnoreCase)) {
    OutError = TEXT("Use the no-resource form of invoke_skill to load "
                    "SKILL.md (the skill body).");
    return false;
  }

  FString Normalized = Trimmed.Replace(TEXT("\\"), TEXT("/"));
  const FString FullPath = Entry.BaseDir / Normalized;

  if (!FPaths::FileExists(FullPath)) {
    OutError =
        FString::Printf(TEXT("Resource '%s' not found under skill '%s'."),
                        *Normalized, *Entry.Name);
    return false;
  }

  if (!FFileHelper::LoadFileToString(OutContent, *FullPath)) {
    OutError =
        FString::Printf(TEXT("Failed to read resource file at %s."), *FullPath);
    return false;
  }
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

  // Helper that takes a SKILL.md (or flat .md) and parses + registers it.
  // bBaseDir empty means the flat single-file layout.
  auto RegisterFromMarkdown = [&](const FString &MarkdownPath,
                                  const FString &BaseDir) {
    FString Content;
    if (!FFileHelper::LoadFileToString(Content, *MarkdownPath)) {
      UE_LOG(LogUAgent, Warning, TEXT("Skill file unreadable, skipping: %s"),
             *MarkdownPath);
      return;
    }

    FString Name, Description;
    int32 BodyStart = 0;
    if (!ParseFrontmatter(Content, Name, Description, BodyStart)) {
      UE_LOG(LogUAgent, Warning,
             TEXT("Skill file %s: missing or malformed frontmatter (need "
                  "`name:` and `description:` between `---` delimiters); "
                  "skipping"),
             *MarkdownPath);
      return;
    }

    FSkillEntry Entry;
    Entry.Name = MoveTemp(Name);
    Entry.Description = MoveTemp(Description);
    Entry.FilePath = MarkdownPath;
    Entry.BaseDir = BaseDir;
    Entry.bFromProject = bIsProjectSource;

    if (!BaseDir.IsEmpty())
      EnumerateResources(BaseDir, Entry.Resources);

    if (bIsProjectSource && OutByName.Contains(Entry.Name)) {
      UE_LOG(LogUAgent, Display,
             TEXT("Project skill '%s' from %s overrides plugin-shipped skill"),
             *Entry.Name, *MarkdownPath);
    }

    OutByName.Add(Entry.Name, Entry);
  };

  // Flat layout: any `*.md` directly in the skills root.
  {
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.md")),
                                  /*Files=*/true, /*Directories=*/false);
    for (const FString &File : Files)
      RegisterFromMarkdown(Dir / File, /*BaseDir=*/FString());
  }

  // Directory layout: any immediate subdirectory containing SKILL.md.
  {
    TArray<FString> Subdirs;
    IFileManager::Get().FindFiles(Subdirs, *(Dir / TEXT("*")),
                                  /*Files=*/false, /*Directories=*/true);
    for (const FString &Subdir : Subdirs) {
      const FString SubdirPath = Dir / Subdir;
      // Be case-insensitive on the filename per the Agent Skills convention,
      // but the canonical name we record is always `SKILL.md` so downstream
      // tooling has one path to match.
      const FString SkillMd = SubdirPath / TEXT("SKILL.md");
      if (!FPaths::FileExists(SkillMd))
        continue;
      RegisterFromMarkdown(SkillMd, SubdirPath);
    }
  }
}

void FSkillRegistry::EnumerateResources(const FString &BaseDir,
                                        TArray<FString> &OutResources) {
  OutResources.Reset();
  // Recursive walk — references/ and similar nested directories are part of
  // the agreed Agent Skills layout. Cap depth implicitly: we only follow
  // directories that exist on disk, so a runaway loop would already be
  // a filesystem problem.
  TArray<FString> AllFiles;
  IFileManager::Get().FindFilesRecursive(AllFiles, *BaseDir, TEXT("*"),
                                         /*Files=*/true, /*Directories=*/false,
                                         /*bClearFileNames=*/true);

  // Normalize the prefix once. Windows engine builds occasionally surface
  // backslashes here depending on which FindFiles* variant returned the entry,
  // and FString::operator/ mixes separators, so canonicalize before matching.
  FString NormPrefix = BaseDir;
  NormPrefix.ReplaceInline(TEXT("\\"), TEXT("/"));
  if (!NormPrefix.EndsWith(TEXT("/")))
    NormPrefix += TEXT("/");

  for (const FString &Abs : AllFiles) {
    FString Norm = Abs;
    Norm.ReplaceInline(TEXT("\\"), TEXT("/"));

    if (!Norm.StartsWith(NormPrefix, ESearchCase::IgnoreCase))
      continue;
    FString Rel = Norm.Mid(NormPrefix.Len());
    if (Rel.IsEmpty())
      continue;
    // SKILL.md is intentionally elided — it's loaded via LoadBody, not as a
    // resource; advertising it would only invite the agent to fetch the same
    // bytes twice.
    if (Rel.Equals(TEXT("SKILL.md"), ESearchCase::IgnoreCase))
      continue;
    OutResources.Add(MoveTemp(Rel));
  }

  OutResources.Sort();
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
