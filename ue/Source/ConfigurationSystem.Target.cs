using UnrealBuildTool;
using System.Collections.Generic;

public class ConfigurationSystemTarget : TargetRules
{
	public ConfigurationSystemTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ConfigurationSystem");
	}
}
