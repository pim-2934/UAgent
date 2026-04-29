#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"

namespace UAgent::FsPathUtils {
/**
 * True iff InPath (after full-path and .. collapsing) lies under the project
 * directory. Uses a trailing-slash boundary so that "/Foo/Project2/evil" can
 * never match "/Foo/Project" as a prefix, and IgnoreCase so Windows callers
 * that pass a different drive-letter/path casing still get a correct answer.
 */
inline bool IsInsideProject(const FString &InPath) {
  FString Full = FPaths::ConvertRelativePathToFull(InPath);
  FString Proj = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
  FPaths::CollapseRelativeDirectories(Full);
  FPaths::CollapseRelativeDirectories(Proj);
  if (!Proj.EndsWith(TEXT("/")))
    Proj.AppendChar(TEXT('/'));
  return Full.StartsWith(Proj, ESearchCase::IgnoreCase);
}
} // namespace UAgent::FsPathUtils
