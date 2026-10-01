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
				// 探针在 Runtime 中生成稳定的 JSON 报告。
				"Json"
			}
		);
	}
}
