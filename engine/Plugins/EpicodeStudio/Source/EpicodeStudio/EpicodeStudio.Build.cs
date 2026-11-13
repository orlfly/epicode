using UnrealBuildTool;

public class EpicodeStudio : ModuleRules
{
	public EpicodeStudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EpicodeCore",
			"EpicodeImport",
			"EpicodeRender",
			"EpicodeControlAPI",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"EditorWidgets",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"LevelEditor",
			"ContentBrowser",
			"PropertyEditor",
			"DesktopPlatform",
			"HTTP",
			"InputCore",
			"ApplicationCore"
		});
	}
}
