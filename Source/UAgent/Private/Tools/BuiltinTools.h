#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

namespace UAgent {
class IACPTool;
class FACPToolRegistry;

/** Registers every built-in tool with the given registry. */
void RegisterBuiltinTools(FACPToolRegistry &Registry);

// ─── Per-tool factories. Each lives in its own .cpp under
// Private/Tools/<Group>/. ───
//
// Adding a new tool:
//   1. Drop a NewThingTool.cpp defining a class : public IACPTool in an
//   anonymous
//      namespace plus a TSharedRef<IACPTool> CreateNewThingTool() factory.
//   2. Forward-declare the factory here.
//   3. Register it in BuiltinTools.cpp.

// Fs + session (existing).
TSharedRef<IACPTool> CreateFsReadTextFileTool();
TSharedRef<IACPTool> CreateFsWriteTextFileTool();
TSharedRef<IACPTool> CreateRequestPermissionTool();

// Blueprint (existing).
TSharedRef<IACPTool> CreateReadBlueprintTool();
TSharedRef<IACPTool> CreateCreateNodeTool();
TSharedRef<IACPTool> CreateLinkNodesTool();
TSharedRef<IACPTool> CreateSetPinDefaultTool();
TSharedRef<IACPTool> CreateDeleteNodeTool();
TSharedRef<IACPTool> CreateUnlinkNodesTool();

// Blueprint authoring (phase 3/4).
TSharedRef<IACPTool> CreateCreateBlueprintTool();
TSharedRef<IACPTool> CreateCompileBlueprintTool();
TSharedRef<IACPTool> CreateAddComponentTool();
TSharedRef<IACPTool> CreateAddVariableTool();
TSharedRef<IACPTool> CreateAddFunctionTool();
TSharedRef<IACPTool> CreateAddEventTool();
TSharedRef<IACPTool> CreateSetDefaultValueTool();
TSharedRef<IACPTool> CreateSetComponentPropertyTool();
TSharedRef<IACPTool> CreateSetComponentMaterialTool();
TSharedRef<IACPTool> CreateGetComponentPropertiesTool();
TSharedRef<IACPTool> CreateAddInterfaceTool();

// Asset (phases 1/3/6).
TSharedRef<IACPTool> CreateListAssetsTool();
TSharedRef<IACPTool> CreateFindReferencesTool();
TSharedRef<IACPTool> CreateFindDependenciesTool();
TSharedRef<IACPTool> CreateDuplicateAssetTool();
TSharedRef<IACPTool> CreateGrepAssetJsonTool();
TSharedRef<IACPTool> CreateCreateMaterialTool();
TSharedRef<IACPTool> CreateCreateMaterialInstanceTool();
TSharedRef<IACPTool> CreateCreateDataAssetTool();
TSharedRef<IACPTool> CreateCreateInputActionTool();
TSharedRef<IACPTool> CreateCreateInputMappingContextTool();
TSharedRef<IACPTool> CreateDeleteAssetTool();
TSharedRef<IACPTool> CreateRenameAssetTool();

// Reflection (phases 1/2/6).
TSharedRef<IACPTool> CreateGetClassInfoTool();
TSharedRef<IACPTool> CreateListSubclassesTool();
TSharedRef<IACPTool> CreateFindNativeClassTool();
TSharedRef<IACPTool> CreateReadHeaderTool();
TSharedRef<IACPTool> CreateFindFunctionTool();

// Data (phases 1/2/5/6).
TSharedRef<IACPTool> CreateReadDataTableTool();
TSharedRef<IACPTool> CreateReadDataAssetTool();
TSharedRef<IACPTool> CreateListGameplayTagsTool();
TSharedRef<IACPTool> CreateReadInputMappingsTool();
TSharedRef<IACPTool> CreateCreateGameplayTagTool();

// Editor (phases 1/2/5/6).
TSharedRef<IACPTool> CreateOpenAssetTool();
TSharedRef<IACPTool> CreateFocusInContentBrowserTool();
TSharedRef<IACPTool> CreateReadEditorLogTool();
TSharedRef<IACPTool> CreateRunConsoleCommandTool();
TSharedRef<IACPTool> CreatePlayInEditorTool();
TSharedRef<IACPTool> CreateStopPieTool();
TSharedRef<IACPTool> CreateSaveAssetTool();
TSharedRef<IACPTool> CreateCaptureViewportTool();

// Level (phase 5).
TSharedRef<IACPTool> CreateListLevelActorsTool();
TSharedRef<IACPTool> CreateGetActorPropertiesTool();
TSharedRef<IACPTool> CreateSetActorPropertyTool();
TSharedRef<IACPTool> CreateSpawnActorTool();
TSharedRef<IACPTool> CreateDestroyActorTool();
TSharedRef<IACPTool> CreateCreateLevelTool();
TSharedRef<IACPTool> CreateSetWorldSettingsTool();

// Config (phase 6).
TSharedRef<IACPTool> CreateReadConfigTool();
TSharedRef<IACPTool> CreateWriteConfigTool();
} // namespace UAgent
