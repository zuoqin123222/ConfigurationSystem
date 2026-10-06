using UnrealBuildTool;
using System.IO;

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
				// 首个可运行原子阶段的纯 C++ 配置面板。
				"UMG",
				"Slate",
				"SlateCore",
				// 右侧选配面板由 UE5.8 WebBrowserWidget 内嵌承载。
				"WebBrowserWidget",
				"WebBrowser",
				// Runtime Path Tracing 探针只依赖公开的 RHI/RenderCore 接口。
				"RHI",
				"RenderCore",
				// Alpha 探针用 FImage/FImageView 保留 BGRA8 与 RGBA16F 的 Alpha 精度。
				"ImageCore",
				// 探针在 Runtime 中生成稳定的 JSON 报告。
				"Json",
				// 内容包只使用 UE 原生 pak 容器检查与挂载接口，不依赖 HotPatcher API。
				"PakFile"
			}
		);

		// UE5.8 的 PreparePathTracingRTPSO 未导出；Shipping 目标为单体链接，
		// 显式依赖 Renderer 后可从产品模块安全触发该预热入口。
		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"Renderer",
				"ApplicationCore"
			}
		);

		// npm run build 从唯一 Web 源码生成在线发布目录，并把同一字节集同步到
		// Content/WebUI。这里递归分发完整 bundle、内嵌 catalog、icon 与 thumbnail，
		// Shipping 运行时只加载本地 file:// 页面，不依赖 Server 或网络。
		string WebUiSource = Path.GetFullPath(Path.Combine(
			ModuleDirectory,
			"..", "..", "Content", "WebUI"));
		if (!Directory.Exists(WebUiSource)
			&& Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			throw new BuildException(
				"Missing embedded WebUI. Run npm run build in source/clients/web before Shipping.");
		}
		if (Directory.Exists(WebUiSource))
		{
			foreach (string WebFile in Directory.GetFiles(
				WebUiSource, "*", SearchOption.AllDirectories))
			{
				string RelativePath = Path.GetRelativePath(
					WebUiSource, WebFile).Replace('\\', '/');
				RuntimeDependencies.Add(
					"$(TargetOutputDir)/WebUI/" + RelativePath,
					WebFile,
					StagedFileType.NonUFS);
			}
		}
	}
}
