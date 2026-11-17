using UnrealBuildTool;

public class EpicodeEditorShell : ModuleRules
{
	public EpicodeEditorShell(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AssetRegistry",
			"ApplicationCore",
			"InputCore",
			"Slate",
			"SlateCore",
			"EpicodeCore",
			"EpicodeAssistant",
			"UnrealEd",
			"LevelEditor",
			"EditorFramework",
			"EditorStyle",
			"EditorWidgets",
			"AppFramework",
			"ToolMenus",
			"Projects",
			"DesktopPlatform",
			"Json",
			"JsonUtilities"
		});
	}
}
