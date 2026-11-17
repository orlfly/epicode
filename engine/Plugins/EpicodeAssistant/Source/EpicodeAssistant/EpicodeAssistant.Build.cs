using UnrealBuildTool;

public class EpicodeAssistant : ModuleRules
{
	public EpicodeAssistant(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"EpicodeCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
		});
	}
}
