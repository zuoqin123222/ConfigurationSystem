using UnrealBuildTool;
using System.Collections.Generic;

public class ConfigurationSystemEditorTarget : TargetRules
{
	public ConfigurationSystemEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ConfigurationSystem");
		// 显式编译测试资产生成命令所在的 Editor 模块。
		ExtraModuleNames.Add("ConfigurationSystemEditor");
	}
}
