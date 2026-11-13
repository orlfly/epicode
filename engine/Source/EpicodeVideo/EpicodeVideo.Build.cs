using UnrealBuildTool;

public class EpicodeVideo : ModuleRules
{
	public EpicodeVideo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EpicodeCore",
			"EpicodeNetwork"
		});

		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.Add("EpicodeControlAPI");
		}
	}
}
