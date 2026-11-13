using UnrealBuildTool;
using System.Collections.Generic;

public class EpicodeVideoServerTarget : TargetRules
{
	public EpicodeVideoServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("EpicodeVideo");
	}
}
