using UnrealBuildTool;

public class EpicodeSandbox : ModuleRules
{
	public EpicodeSandbox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EpicodeCore",
			"Json",
			"JsonUtilities",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects"
		});
	}
}
