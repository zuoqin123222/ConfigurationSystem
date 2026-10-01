#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"

class AActor;
class FViewport;
class FPathTracingAlphaProbeViewExtension;
class UMaterialInterface;

/**
 * 由 -PathTracingAlphaProbe 显式启用的 Runtime Alpha 探针。
 *
 * 探针在 Editor -game 的运行时 World 中搭建最小场景，等待 Path Tracing
 * 累积到 16 个样本后，从游戏 Viewport 同时读取 BGRA8 与 RGBA16F。
 */
class FPathTracingAlphaProbe final
{
public:
	void Start();
	void Shutdown();

	/** 两种回读格式共用的 Alpha 分类与裁切统计。 */
	struct FPixelStatistics
	{
		int64 TransparentPixels = 0;
		int64 OpaquePixels = 0;
		int64 TranslucentPixels = 0;
		int64 TransparentRgbPollutedPixels = 0;
		double MaxTransparentRgb = 0.0;
		FIntRect ContentBounds;
		bool bHasContent = false;
	};

private:
	enum class EState : uint8
	{
		WaitingForViewport,
		WaitingForSamples,
		Finished
	};

	void OnEngineLoopInitComplete();
	void OnViewportRendered(FViewport* Viewport);
	bool Tick(float DeltaTime);
	bool SetupSceneAndPathTracing();
	bool CaptureViewport(FViewport& Viewport, FString& OutFailureReason);
	void Finish(bool bSuccess, const FString& FailureReason);

	FPixelStatistics AnalyzeBgra8(const TArray<FColor>& Pixels, FIntPoint Size) const;
	FPixelStatistics AnalyzeRgba16f(const TArray<FFloat16Color>& Pixels, FIntPoint Size) const;
	static void CropBgra8(
		const TArray<FColor>& Source,
		FIntPoint SourceSize,
		const FIntRect& Bounds,
		TArray<FColor>& OutPixels);
	static void CropRgba16f(
		const TArray<FFloat16Color>& Source,
		FIntPoint SourceSize,
		const FIntRect& Bounds,
		TArray<FFloat16Color>& OutPixels);
	static uint8 GetCornerBackgroundAlphaBgra8(
		const TArray<FColor>& Source,
		FIntPoint Size);
	static float GetCornerBackgroundAlphaRgba16f(
		const TArray<FFloat16Color>& Source,
		FIntPoint Size);
	static int64 NormalizeBgra8(
		const TArray<FColor>& Source,
		bool bInvertCoverage,
		TArray<FColor>& OutPixels);
	static int64 NormalizeRgba16f(
		const TArray<FFloat16Color>& Source,
		bool bInvertCoverage,
		TArray<FFloat16Color>& OutPixels);
	static int32 CountCoveredCubeBands(
		const TArray<FColor>& Source,
		FIntPoint Size);
	static int32 CountCoveredCubeBands(
		const TArray<FFloat16Color>& Source,
		FIntPoint Size);
	static void CompositeStraightAlpha(
		const TArray<FColor>& Source,
		FIntPoint Size,
		const FColor& Background,
		bool bCheckerboard,
		TArray<FColor>& OutPixels);

	FDelegateHandle EngineInitCompleteHandle;
	FDelegateHandle ViewportRenderedHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TSharedPtr<FPathTracingAlphaProbeViewExtension, ESPMode::ThreadSafe> ViewExtension;

	EState State = EState::WaitingForViewport;
	FString OutputDirectory;
	FString JsonOutputPath;
	FString PngOutputPath;
	FString ExrOutputPath;
	FString CroppedPngOutputPath;
	FString CroppedExrOutputPath;
	FString NormalizedPngOutputPath;
	FString NormalizedExrOutputPath;
	FString CroppedNormalizedPngOutputPath;
	FString CroppedNormalizedExrOutputPath;
	TArray<FString> PreviewOutputPaths;
	double StartTimeSeconds = 0.0;
	double SceneReadyTimeSeconds = 0.0;
	double TimeoutSeconds = 120.0;

	TArray<TWeakObjectPtr<AActor>> SpawnedActors;
	TArray<TObjectPtr<UMaterialInterface>> LoadedMaterials;
	TArray<FString> LoadedMaterialPaths;
	TArray<FString> MissingMaterialPaths;

	bool bSceneCreated = false;
	bool bViewModeApplied = false;
	bool bWarmupResetRequested = false;
	bool bWarmupResetObserved = false;
	bool bCapturePending = false;
	bool bBgra8Read = false;
	bool bRgba16fRead = false;
	bool bPngWritten = false;
	bool bExrWritten = false;
	bool bCroppedPngWritten = false;
	bool bCroppedExrWritten = false;
	bool bNormalizedPngWritten = false;
	bool bNormalizedExrWritten = false;
	bool bCroppedNormalizedPngWritten = false;
	bool bCroppedNormalizedExrWritten = false;
	TArray<bool> bPreviewWritten;
	bool bNormalizationRequired = false;
	bool bCoverageInverted = false;
	bool bWebReady = false;
	uint8 RawBackgroundAlpha = 0;
	float RawBackgroundAlphaRgba16f = 0.0f;
	int64 GlowRecoveredPixels = 0;
	int64 GlowRecoveredPixelsRgba16f = 0;
	int32 Bgra8CoveredCubeCount = 0;
	int32 Rgba16fCoveredCubeCount = 0;
	uint32 FinalSampleIndex = 0;
	uint32 TargetSampleCount = 0;
	FIntPoint CapturedSize = FIntPoint::ZeroValue;
	FPixelStatistics Bgra8Statistics;
	FPixelStatistics Rgba16fStatistics;
	FPixelStatistics NormalizedBgra8Statistics;
	FPixelStatistics NormalizedRgba16fStatistics;
	FPixelStatistics CroppedNormalizedBgra8Statistics;
	FPixelStatistics CroppedNormalizedRgba16fStatistics;
};
