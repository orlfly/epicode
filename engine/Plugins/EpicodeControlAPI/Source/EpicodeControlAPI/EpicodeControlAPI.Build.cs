using UnrealBuildTool;

public class EpicodeControlAPI : ModuleRules
{
	public EpicodeControlAPI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EpicodeCore",
			"EpicodeNetwork",
			"EpicodeSandbox",
			"EpicodeDirector",
			"HTTPServer",
			"Sockets",
			"Networking",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Networking"
		});
	}
}
