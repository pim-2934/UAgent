#pragma once

#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"

class FToolBarBuilder;
class FMenuBuilder;

namespace UAgent {
class FACPToolRegistry;
class FAssetContextBuilderRegistry;
class FMCPServer;
} // namespace UAgent

class FUAgentModule : public IModuleInterface {
public:
  /** IModuleInterface implementation */
  virtual void StartupModule() override;
  virtual void ShutdownModule() override;

  /** This function will be bound to Command (by default it will bring up plugin
   * window) */
  void PluginButtonClicked();

  /** Shared registry of agent→client tool handlers. Populated in StartupModule.
   */
  TSharedPtr<UAgent::FACPToolRegistry> GetToolRegistry() const {
    return ToolRegistry;
  }

  /** Shared registry of per-UClass asset→ContentBlock builders for prompt
   * context. */
  TSharedPtr<UAgent::FAssetContextBuilderRegistry>
  GetAssetContextRegistry() const {
    return AssetContextRegistry;
  }

private:
  void RegisterMenus();

  TSharedRef<class SDockTab>
  OnSpawnPluginTab(const class FSpawnTabArgs &SpawnTabArgs);

private:
  TSharedPtr<class FUICommandList> PluginCommands;
  TSharedPtr<UAgent::FACPToolRegistry> ToolRegistry;
  TSharedPtr<UAgent::FAssetContextBuilderRegistry> AssetContextRegistry;
  TUniquePtr<UAgent::FMCPServer> MCPServer;
};
