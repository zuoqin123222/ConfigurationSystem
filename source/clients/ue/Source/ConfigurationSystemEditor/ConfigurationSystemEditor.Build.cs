using UnrealBuildTool;

public class ConfigurationSystemEditor : ModuleRules
{
	public ConfigurationSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AssetRegistry",
				"AssetTools",
				"DesktopPlatform",
				"InputCore",
				"Json",
				"MaterialEditor",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolMenus",
				// 仅 Editor 模块依赖保存资产和编辑器通知能力。
				"UnrealEd",
				"ConfigurationSystem"
			}
		);
	}
}
