#pragma once

#include "CoreMinimal.h"

namespace UAgent {

/**
 * A discovered skill — frontmatter metadata plus the path to its markdown
 * file. Body text is loaded on demand via FSkillRegistry::LoadBody so the
 * catalog injection block stays small even when shipped skills are long.
 *
 * Two on-disk shapes are supported, both discovered by ScanDirectory:
 *  1. Flat: `<root>/<name>.md` — single file, no sibling resources possible.
 *     `FilePath` is the .md; `BaseDir` is empty; `Resources` is empty.
 *  2. Directory: `<root>/<name>/SKILL.md` — a directory holding the SKILL.md
 *     plus optional sibling files (manifest.yaml, references/*.md, assets/,
 *     scripts/, …). `FilePath` is the SKILL.md; `BaseDir` is the directory;
 *     `Resources` enumerates everything else under BaseDir, with relative
 *     paths normalized to forward slashes so they can be passed back through
 *     `LoadResource` regardless of platform.
 */
struct FSkillEntry {
  FString Name;              // frontmatter `name`, kebab-case slug.
  FString Description;       // frontmatter `description`, one-line summary.
  FString FilePath;          // absolute path to SKILL.md (or the flat .md).
  FString BaseDir;           // absolute directory containing SKILL.md; empty
                             // for flat single-file skills. Used as the
                             // resource-resolution root.
  TArray<FString> Resources; // relative paths (forward-slash) to sibling
                             // files under BaseDir, excluding SKILL.md itself.
                             // Empty for flat skills.
  bool bFromProject = false; // true if loaded from <ProjectDir>/UAgent/Skills.
};

/**
 * Module-wide registry of UAgent skills — author-written markdown recipes
 * shipped with the plugin (Resources/Skills/*.md) and optionally extended
 * per-project (<ProjectDir>/UAgent/Skills/*.md). Project skills with the
 * same `name` as a plugin skill override the plugin entry.
 *
 * Skills surface to the agent through two mechanisms:
 *   1. A catalog block (name + one-line description per skill) is prepended
 *      to the first user prompt of every session, alongside AGENTS.md.
 *   2. The `invoke_skill` tool loads the full body on demand when the agent
 *      decides a topic in the catalog is relevant to the user's request.
 *
 * Scans are lazy and cached. ForceRescan() drops the cache; SACPChatWindow
 * calls this on every new session so edits to skill files between sessions
 * are picked up without restarting the editor.
 */
class FSkillRegistry {
public:
  static FSkillRegistry &Get();

  /** Scan on first access. No-op once cached. */
  void ScanIfNeeded();

  /** Drop the cache; the next ScanIfNeeded() re-reads both directories. */
  void ForceRescan();

  /** All registered skills, sorted by name. Triggers ScanIfNeeded. */
  const TArray<FSkillEntry> &GetAll();

  /** Returns the entry by name, or nullptr if not registered. */
  const FSkillEntry *Find(const FString &Name);

  /**
   * Read the full markdown body (everything after the closing `---`) for the
   * given entry. Returns true on success. Does no caching — the body is
   * potentially large and only fetched when the agent calls `invoke_skill`.
   */
  bool LoadBody(const FSkillEntry &Entry, FString &OutBody) const;

  /**
   * Load a sibling resource file from a directory-based skill. RelPath is a
   * forward-slash-separated path relative to the skill's BaseDir (e.g.
   * "manifest.yaml" or "references/acfu-reference.md"). Returns false for
   * flat single-file skills (no BaseDir), for paths that try to escape the
   * skill directory via `..` or absolute roots, for paths that name SKILL.md
   * itself (callers should use LoadBody for that), and for missing files.
   * OutError is filled with a human-readable reason on failure.
   */
  bool LoadResource(const FSkillEntry &Entry, const FString &RelPath,
                    FString &OutContent, FString &OutError) const;

  /**
   * Build the catalog block injected into the first prompt of each session.
   * Returns an empty string when no skills are registered, so the caller can
   * skip the insertion cleanly.
   */
  FString BuildCatalogBlock();

private:
  TArray<FSkillEntry> Entries;
  bool bScanned = false;

  void ScanDirectory(const FString &Dir, bool bIsProjectSource,
                     TMap<FString, FSkillEntry> &OutByName);

  /** Walk BaseDir and fill OutResources with every file other than SKILL.md,
   * as forward-slash relative paths. Recursive — references/ subdirectories
   * are part of the directory-skill convention. */
  static void EnumerateResources(const FString &BaseDir,
                                 TArray<FString> &OutResources);

  /** Parse the YAML-ish frontmatter (only `name:` and `description:` are read).
   * Returns true when both fields are populated; sets OutBodyStart to the
   * index of the first character after the closing `---` line. */
  static bool ParseFrontmatter(const FString &Content, FString &OutName,
                               FString &OutDescription, int32 &OutBodyStart);
};

} // namespace UAgent
