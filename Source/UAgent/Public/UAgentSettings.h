#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UAgentSettings.generated.h"

/**
 * Governs how session/request_permission is answered. Surfaced as a dropdown
 * under the chat input; persisted per-user via PermissionModeStore (not in
 * UUAgentSettings, so it doesn't leak into the project's shared config).
 */
UENUM()
enum class EACPPermissionMode : uint8 {
  ReadOnly UMETA(DisplayName = "Read Only"),
  Default UMETA(DisplayName = "Default"),
  FullAccess UMETA(DisplayName = "Full Access")
};

namespace UAgent {
/**
 * Per-user persistence for the chat window's selected permission mode. Backed
 * by GEditorPerProjectIni so the choice survives editor restarts without
 * being committed to the project's shared Engine.ini.
 */
namespace PermissionModeStore {
UAGENT_API EACPPermissionMode Load();
UAGENT_API void Save(EACPPermissionMode Mode);
} // namespace PermissionModeStore

/**
 * Per-user persistence for the user's preferred agent model. Stored as the
 * raw `value` string from the agent's configOptions (e.g. "claude-opus-4-5"),
 * which is agent-specific — empty when no preference has been picked yet, or
 * when the saved value isn't offered by the current agent. Same backing store
 * as PermissionModeStore.
 */
namespace ModelStore {
UAGENT_API FString Load();
UAGENT_API void Save(const FString &Value);
} // namespace ModelStore
} // namespace UAgent

UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "UAgent"))
class UAGENT_API UUAgentSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  UUAgentSettings();

  virtual FName GetContainerName() const override { return TEXT("Project"); }
  virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

  /**
   * Path to the ACP agent launcher. Accepts `.cmd` / `.bat` (run via cmd),
   * `.exe` (run direct), `.js` (run via `node` from PATH), or an
   * extensionless npm shim on Windows (auto-upgraded to its `.cmd` sibling).
   */
  UPROPERTY(Config, EditAnywhere, Category = "Agent",
            meta = (DisplayName = "Agent Command"))
  FString AgentCommand;

  /** Additional args appended after the agent command. */
  UPROPERTY(Config, EditAnywhere, Category = "Agent",
            meta = (DisplayName = "Extra Args"))
  TArray<FString> AgentArgs;

  /** When true, currently open assets are auto-added as context with each
   * message. */
  UPROPERTY(Config, EditAnywhere, Category = "Context",
            meta = (DisplayName = "Auto-Include Open Assets"))
  bool bAutoIncludeOpenAssets = true;

  /** Hard cap on inlined Blueprint summary length (chars). */
  UPROPERTY(Config, EditAnywhere, Category = "Context",
            meta = (DisplayName = "Max Blueprint Summary Chars", ClampMin = 512,
                    ClampMax = 100000))
  int32 MaxBlueprintSummaryChars = 8000;

  /** Per-request timeout in seconds (0 = no timeout). */
  UPROPERTY(Config, EditAnywhere, Category = "Agent",
            meta = (DisplayName = "Request Timeout Seconds", ClampMin = 0,
                    ClampMax = 3600))
  int32 RequestTimeoutSeconds = 300;

  /**
   * When true, the editor hosts an MCP server that exposes the Blueprint tools
   * (_ue5/read_blueprint etc.) so external agents — Claude Code, Claude
   * Desktop, Cursor, Zed — can discover and invoke them. Disable to close
   * the port entirely; chat still works over ACP.
   */
  UPROPERTY(Config, EditAnywhere, Category = "MCP",
            meta = (DisplayName = "Enable MCP Server"))
  bool bEnableMCPServer = true;

  /** Loopback port the MCP server binds to when enabled. */
  UPROPERTY(Config, EditAnywhere, Category = "MCP",
            meta = (DisplayName = "MCP Server Port", ClampMin = 1024,
                    ClampMax = 65535))
  int32 MCPServerPort = 47777;
};
