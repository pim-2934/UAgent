#pragma once

#include "CoreMinimal.h"

namespace UAgent {
namespace Common {
/**
 * AND of three gates controlling whether developer-only tools light up:
 *   1. UUAgentSettings::bDeveloperMode is true.
 *   2. The "UAgent" plugin can be located by IPluginManager.
 *   3. <Plugin>/Source/UAgent/Private/Tools is writable, verified once per
 *      process by writing-and-deleting a probe file. Memoized in a static
 *      so subsequent calls are free.
 *
 * Cheap to call on the game thread per chat turn. The plugin's source dir
 * doesn't switch from writable to read-only mid-session in any realistic
 * dev workflow, so the memoization is correct in practice.
 *
 * Toggling bDeveloperMode mid-session does NOT take effect — tool
 * registration runs once in FUAgentModule::StartupModule, by design (the
 * propose_missing_tool flow's whole point is the halt-and-rebuild cycle).
 */
bool IsDeveloperToolingEnabled();

/** Absolute path to <Plugin>/Source/UAgent/Private/Tools. Empty when the
 * plugin can't be located. Used by the writability probe and surfaced in
 * proposal sidecars so the developer knows where to drop the new file. */
FString GetToolsSourceDir();

/** Absolute path to <ProjectSaved>/UAgent/Proposals. Created on first call.
 * Used by both ProposeMissingToolTool and SACPChatWindow's pending-proposal
 * scan on session start. */
FString GetProposalsDir();
} // namespace Common
} // namespace UAgent
