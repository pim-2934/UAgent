#include "UAgent.h"
#include "LevelEditor.h"
#include "MCP/MCPServer.h"
#include "Protocol/ACPToolRegistry.h"
#include "ToolMenus.h"
#include "Tools/BuiltinTools.h"
#include "Tools/Editor/EditorLogSink.h"
#include "UAgentCommands.h"
#include "UAgentSettings.h"
#include "UAgentStyle.h"
#include "UI/ACPAssetContext.h"
#include "UI/AssetContextRegistry.h"
#include "UI/SACPChatWindow.h"
#include "Widgets/Docking/SDockTab.h"

static const FName UAgentTabName("UAgent");

#define LOCTEXT_NAMESPACE "FUAgentModule"

void FUAgentModule::StartupModule() {
  // This code will execute after your module is loaded into memory; the exact
  // timing is specified in the .uplugin file per-module

  // Register log sink before tools so read_editor_log picks up messages emitted
  // during startup.
  if (GLog)
    GLog->AddOutputDevice(&UAgent::FEditorLogSink::Get());

  ToolRegistry = MakeShared<UAgent::FACPToolRegistry>();
  UAgent::RegisterBuiltinTools(*ToolRegistry);

  AssetContextRegistry = MakeShared<UAgent::FAssetContextBuilderRegistry>();
  UAgent::RegisterBuiltinAssetContextBuilders(*AssetContextRegistry);

  if (const UUAgentSettings *Settings = GetDefault<UUAgentSettings>();
      Settings && Settings->bEnableMCPServer) {
    MCPServer = MakeUnique<UAgent::FMCPServer>();
    if (!MCPServer->Start(Settings->MCPServerPort, ToolRegistry)) {
      MCPServer.Reset();
    }
  }

  FUAgentStyle::Initialize();
  FUAgentStyle::ReloadTextures();

  FUAgentCommands::Register();

  PluginCommands = MakeShareable(new FUICommandList);

  PluginCommands->MapAction(
      FUAgentCommands::Get().OpenPluginWindow,
      FExecuteAction::CreateRaw(this, &FUAgentModule::PluginButtonClicked),
      FCanExecuteAction());

  UToolMenus::RegisterStartupCallback(
      FSimpleMulticastDelegate::FDelegate::CreateRaw(
          this, &FUAgentModule::RegisterMenus));

  FGlobalTabmanager::Get()
      ->RegisterNomadTabSpawner(
          UAgentTabName,
          FOnSpawnTab::CreateRaw(this, &FUAgentModule::OnSpawnPluginTab))
      .SetDisplayName(LOCTEXT("FUAgentTabTitle", "UAgent"))
      .SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FUAgentModule::ShutdownModule() {
  // This function may be called during shutdown to clean up your module.  For
  // modules that support dynamic reloading, we call this function before
  // unloading the module.

  UToolMenus::UnRegisterStartupCallback(this);

  UToolMenus::UnregisterOwner(this);

  FUAgentStyle::Shutdown();

  FUAgentCommands::Unregister();

  FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UAgentTabName);

  if (MCPServer.IsValid()) {
    MCPServer->Stop();
    MCPServer.Reset();
  }
  AssetContextRegistry.Reset();
  ToolRegistry.Reset();

  if (GLog)
    GLog->RemoveOutputDevice(&UAgent::FEditorLogSink::Get());
}

TSharedRef<SDockTab>
FUAgentModule::OnSpawnPluginTab(const FSpawnTabArgs &SpawnTabArgs) {
  return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SACPChatWindow)];
}

void FUAgentModule::PluginButtonClicked() {
  FGlobalTabmanager::Get()->TryInvokeTab(UAgentTabName);
}

void FUAgentModule::RegisterMenus() {
  // Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
  FToolMenuOwnerScoped OwnerScoped(this);

  {
    UToolMenu *Menu =
        UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
    {
      FToolMenuSection &Section = Menu->FindOrAddSection("WindowLayout");
      Section.AddMenuEntryWithCommandList(
          FUAgentCommands::Get().OpenPluginWindow, PluginCommands);
    }
  }

  {
    UToolMenu *ToolbarMenu = UToolMenus::Get()->ExtendMenu(
        "LevelEditor.LevelEditorToolBar.PlayToolBar");
    {
      FToolMenuSection &Section = ToolbarMenu->FindOrAddSection("PluginTools");
      {
        FToolMenuEntry &Entry =
            Section.AddEntry(FToolMenuEntry::InitToolBarButton(
                FUAgentCommands::Get().OpenPluginWindow));
        Entry.SetCommandList(PluginCommands);
      }
    }
  }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUAgentModule, UAgent)