#include "BuiltinTools.h"
#include "../Protocol/ACPToolRegistry.h"

namespace UAgent {
void RegisterBuiltinTools(FACPToolRegistry &Registry) {
  // Fs + session.
  Registry.Register(CreateFsReadTextFileTool());
  Registry.Register(CreateFsWriteTextFileTool());
  Registry.Register(CreateRequestPermissionTool());

  // Blueprint (node-level).
  Registry.Register(CreateReadBlueprintTool());
  Registry.Register(CreateCreateNodeTool());
  Registry.Register(CreateLinkNodesTool());
  Registry.Register(CreateSetPinDefaultTool());
  Registry.Register(CreateDeleteNodeTool());
  Registry.Register(CreateUnlinkNodesTool());

  // Blueprint authoring.
  Registry.Register(CreateCreateBlueprintTool());
  Registry.Register(CreateCompileBlueprintTool());
  Registry.Register(CreateAddComponentTool());
  Registry.Register(CreateAddVariableTool());
  Registry.Register(CreateAddFunctionTool());
  Registry.Register(CreateAddEventTool());
  Registry.Register(CreateSetDefaultValueTool());
  Registry.Register(CreateSetComponentPropertyTool());
  Registry.Register(CreateSetComponentMaterialTool());
  Registry.Register(CreateGetComponentPropertiesTool());
  Registry.Register(CreateAddInterfaceTool());

  // Asset discovery + editing.
  Registry.Register(CreateListAssetsTool());
  Registry.Register(CreateFindReferencesTool());
  Registry.Register(CreateFindDependenciesTool());
  Registry.Register(CreateDuplicateAssetTool());
  Registry.Register(CreateGrepAssetJsonTool());
  Registry.Register(CreateCreateMaterialTool());
  Registry.Register(CreateCreateMaterialInstanceTool());
  Registry.Register(CreateCreateDataAssetTool());
  Registry.Register(CreateCreateInputActionTool());
  Registry.Register(CreateCreateInputMappingContextTool());
  Registry.Register(CreateDeleteAssetTool());
  Registry.Register(CreateRenameAssetTool());

  // Reflection.
  Registry.Register(CreateGetClassInfoTool());
  Registry.Register(CreateListSubclassesTool());
  Registry.Register(CreateFindNativeClassTool());
  Registry.Register(CreateReadHeaderTool());
  Registry.Register(CreateFindFunctionTool());

  // Data.
  Registry.Register(CreateReadDataTableTool());
  Registry.Register(CreateReadDataAssetTool());
  Registry.Register(CreateListGameplayTagsTool());
  Registry.Register(CreateReadInputMappingsTool());
  Registry.Register(CreateCreateGameplayTagTool());

  // Editor control.
  Registry.Register(CreateOpenAssetTool());
  Registry.Register(CreateFocusInContentBrowserTool());
  Registry.Register(CreateReadEditorLogTool());
  Registry.Register(CreateRunConsoleCommandTool());
  Registry.Register(CreatePlayInEditorTool());
  Registry.Register(CreateStopPieTool());
  Registry.Register(CreateSaveAssetTool());
  Registry.Register(CreateCaptureViewportTool());

  // Level.
  Registry.Register(CreateListLevelActorsTool());
  Registry.Register(CreateGetActorPropertiesTool());
  Registry.Register(CreateSetActorPropertyTool());
  Registry.Register(CreateSpawnActorTool());
  Registry.Register(CreateDestroyActorTool());
  Registry.Register(CreateCreateLevelTool());
  Registry.Register(CreateSetWorldSettingsTool());

  // Config.
  Registry.Register(CreateReadConfigTool());
  Registry.Register(CreateWriteConfigTool());
}
} // namespace UAgent
