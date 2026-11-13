using UnrealBuildTool;

public class EpicodeNetwork : ModuleRules
{
	public EpicodeNetwork(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EpicodeCore",
			"EpicodeSandbox",
			"Json",
			"JsonUtilities",
			"DeveloperSettings",
			"NetCore",
			"Networking",
			"AIModule",
			"NavigationSystem",
			"Projects"
		});
	}
}
