using System.IO;
using UnrealBuildTool;

public class EpicodeRender : ModuleRules
{
	public EpicodeRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EpicodeCore",
			"EpicodeDirector",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"MovieRenderPipelineCore",
			"MovieRenderPipelineRenderPasses",
			"MovieRenderPipelineSettings",
			"MovieRenderPipelineEditor",
			"MovieRenderPipelineMP4Encoder",
			"EditorScriptingUtilities",
			"MovieScene",
			"LevelSequence",
			"MovieSceneTracks",
			"RenderCore",
			"RHI",
			"ImageWrapper",
			"Imath",
			"UEOpenExr",
			"UEOpenExrRTTI"
		});
	}
}
