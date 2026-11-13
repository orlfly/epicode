using UnrealBuildTool;

public class EpicodeImport : ModuleRules
{
	public EpicodeImport(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EpicodeCore",
			"InterchangeCore",
			"InterchangeEngine",
			"InterchangeImport",
			"InterchangePipelines",
			"AssetRegistry",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"MeshDescription",
			"StaticMeshDescription",
			"SkeletalMeshDescription"
		});
	}
}
