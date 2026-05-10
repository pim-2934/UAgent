#include "DeveloperGate.h"

#include "../../Protocol/ACPTypes.h"
#include "UAgentSettings.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"

namespace UAgent {
namespace Common {
namespace {
// Memoized writability check. The plugin source tree's permissions don't
// flip mid-session in any realistic dev workflow; one syscall on first ask
// is the right tradeoff vs paying for it every chat turn.
bool ProbeToolsSourceWritable(const FString &ToolsDir) {
  if (ToolsDir.IsEmpty() || !FPaths::DirectoryExists(ToolsDir)) {
    return false;
  }
  const FString ProbePath = ToolsDir / TEXT(".uagent-write-probe");
  if (!FFileHelper::SaveStringToFile(
          TEXT(""), *ProbePath,
          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) {
    return false;
  }
  IFileManager::Get().Delete(*ProbePath, /*RequireExists=*/false,
                             /*EvenReadOnly=*/true, /*Quiet=*/true);
  return true;
}
} // namespace

FString GetToolsSourceDir() {
  const TSharedPtr<IPlugin> Plugin =
      IPluginManager::Get().FindPlugin(TEXT("UAgent"));
  if (!Plugin.IsValid()) {
    return FString();
  }
  return FPaths::ConvertRelativePathToFull(
      Plugin->GetBaseDir() / TEXT("Source/UAgent/Private/Tools"));
}

FString GetProposalsDir() {
  const FString Dir = FPaths::ConvertRelativePathToFull(
      FPaths::ProjectSavedDir() / TEXT("UAgent/Proposals"));
  IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
  return Dir;
}

bool IsDeveloperToolingEnabled() {
  const UUAgentSettings *Settings = GetDefault<UUAgentSettings>();
  if (!Settings || !Settings->bDeveloperMode) {
    return false;
  }

  static TOptional<bool> CachedWritable;
  if (!CachedWritable.IsSet()) {
    const FString ToolsDir = GetToolsSourceDir();
    const bool bWritable = ProbeToolsSourceWritable(ToolsDir);
    CachedWritable = bWritable;
    if (!bWritable) {
      UE_LOG(LogUAgent, Display,
             TEXT("DeveloperGate: bDeveloperMode is set but plugin source "
                  "tree '%s' is not writable; developer tools remain off."),
             *ToolsDir);
    }
  }
  return CachedWritable.GetValue();
}
} // namespace Common
} // namespace UAgent
