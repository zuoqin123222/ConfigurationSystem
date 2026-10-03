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
				// 内嵌浏览器加载前异步探测同源 /health。
				"HTTP",
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

		// catalog.thumbnailUrl 指向 Web 公共色卡；把同一份经校验的 WebP 以 NonUFS
		// 资源随桌面端分发，运行时可按 URL 路径直接加载，避免 Editor-only 资产导入。
		string ThumbnailSource = Path.GetFullPath(Path.Combine(
			ModuleDirectory,
			"..", "..", "..", "web", "public", "sc01", "thumbnails"));
		if (Directory.Exists(ThumbnailSource))
		{
			foreach (string Thumbnail in Directory.GetFiles(ThumbnailSource, "*.webp"))
			{
				RuntimeDependencies.Add(
					"$(TargetOutputDir)/sc01/thumbnails/" + Path.GetFileName(Thumbnail),
					Thumbnail,
					StagedFileType.NonUFS);
			}
		}
	}
}
