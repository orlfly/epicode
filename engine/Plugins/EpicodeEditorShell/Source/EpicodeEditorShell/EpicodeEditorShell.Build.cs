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
			"ApplicationCore",
			"InputCore",
			"Slate",
			"SlateCore",
			"EpicodeCore",
			"UnrealEd",
			"LevelEditor",
			"EditorFramework",
			"EditorStyle",
			"AppFramework",
			"ToolMenus",
			"Projects",
			"Json",
			"JsonUtilities"
		});
	}
}
