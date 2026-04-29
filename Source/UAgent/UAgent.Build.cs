using UnrealBuildTool;

public class UAgent : ModuleRules
{
	public UAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"ApplicationCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"DeveloperSettings",
				"Json",
				"JsonUtilities",

				// Save-file dialog for chat transcript export.
				"DesktopPlatform",

				// Embedded HTTP server for the MCP bridge (Private/MCP/).
				"HTTP",
				"HTTPServer",
				"Sockets",                  // loopback-only peer check in MCPServer

				// Asset / content-browser access for @-mention + auto-context.
				"AssetRegistry",
				"AssetTools",
				"ContentBrowser",
				"ContentBrowserData",

				// Blueprint graph manipulation.
				"BlueprintGraph",
				"GraphEditor",
				"KismetCompiler",
				"Kismet",

				// Phase-0 additions for expanded tool set.
				"SubobjectDataInterface",   // reserved for future add_component via new API
				"GameplayTags",             // list_gameplay_tags
				"GameplayTagsEditor",       // create_gameplay_tag (IGameplayTagsEditorModule)
				"EnhancedInput",            // read_input_mappings
				"AnimGraph",                // read_anim_blueprint
				"AnimGraphRuntime",
				"EditorScriptingUtilities", // UEditorAssetLibrary (duplicate/save)
				"LevelEditor",              // ULevelEditorSubsystem (create_level)
				"MaterialEditor",           // UMaterialEditingLibrary (create_material*)
			}
		);
	}
}
