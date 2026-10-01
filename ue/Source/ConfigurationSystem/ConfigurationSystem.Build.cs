using UnrealBuildTool;

public class ConfigurationSystem : ModuleRules
{
	public ConfigurationSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AssetRegistry",
				"InputCore",
				"EnhancedInput",
				// Runtime Path Tracing 探针只依赖公开的 RHI/RenderCore 接口。
				"RHI",
				"RenderCore",
				// 探针在 Runtime 中生成稳定的 JSON 报告。
				"Json"
			}
		);
	}
}
