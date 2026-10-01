#include "PathTracingAlphaProbe.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathTracingAlphaProbe, Log, All);

namespace PathTracingAlphaProbe
{
	constexpr uint32 RequiredSamples = 16;
	constexpr uint8 TransparentByteThreshold = 1;
	constexpr uint8 OpaqueByteThreshold = 254;
	constexpr float TransparentFloatThreshold = 1.0f / 255.0f;
	constexpr float OpaqueFloatThreshold = 254.0f / 255.0f;
	constexpr float RgbPollutionThreshold = 1.0f / 255.0f;

	const TCHAR* GetBuildConfigurationName()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_TEST
		return TEXT("Test");
#elif UE_BUILD_DEBUG
		return TEXT("Debug");
#elif UE_BUILD_DEVELOPMENT
		return TEXT("Development");
#else
		return TEXT("Unknown");
#endif
	}

	TSharedRef<FJsonObject> MakeBoundsJson(const FIntRect& Bounds, const bool bHasContent)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("hasContent"), bHasContent);
		Json->SetNumberField(TEXT("minX"), bHasContent ? Bounds.Min.X : 0);
		Json->SetNumberField(TEXT("minY"), bHasContent ? Bounds.Min.Y : 0);
		Json->SetNumberField(TEXT("maxXExclusive"), bHasContent ? Bounds.Max.X : 0);
		Json->SetNumberField(TEXT("maxYExclusive"), bHasContent ? Bounds.Max.Y : 0);
		Json->SetNumberField(TEXT("width"), bHasContent ? Bounds.Width() : 0);
		Json->SetNumberField(TEXT("height"), bHasContent ? Bounds.Height() : 0);
		return Json;
	}

	TSharedRef<FJsonObject> MakeStatisticsJson(
		const FPathTracingAlphaProbe::FPixelStatistics& Statistics)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("transparentPixels"), static_cast<double>(Statistics.TransparentPixels));
		Json->SetNumberField(TEXT("opaquePixels"), static_cast<double>(Statistics.OpaquePixels));
		Json->SetNumberField(TEXT("translucentPixels"), static_cast<double>(Statistics.TranslucentPixels));
		Json->SetNumberField(
			TEXT("transparentRgbPollutedPixels"),
			static_cast<double>(Statistics.TransparentRgbPollutedPixels));
		Json->SetNumberField(TEXT("maxTransparentRgb"), Statistics.MaxTransparentRgb);
		Json->SetObjectField(
			TEXT("contentBounds"),
			MakeBoundsJson(Statistics.ContentBounds, Statistics.bHasContent));
		return Json;
	}

	void SetConsoleVariable(const TCHAR* Name, const int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	bool AddCube(
		UWorld& World,
		UStaticMesh& CubeMesh,
		UMaterialInterface& Material,
		const FVector& Location,
		TArray<TWeakObjectPtr<AActor>>& SpawnedActors)
	{
		AStaticMeshActor* Cube = World.SpawnActor<AStaticMeshActor>(
			Location,
			FRotator::ZeroRotator);
		if (Cube == nullptr)
		{
			return false;
		}

		UStaticMeshComponent* Component = Cube->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(&CubeMesh);
		Component->SetMaterial(0, &Material);
		Component->SetWorldScale3D(FVector(1.35f));
		SpawnedActors.Add(Cube);
		return true;
	}
}

/**
 * 只读取公开 FSceneViewStateInterface 的 Path Tracing 进度。
 * SetupView 在游戏线程运行，因此无需访问 Renderer Private 类型或做跨线程同步。
 */
class FPathTracingAlphaProbeViewExtension final : public FSceneViewExtensionBase
{
public:
	FPathTracingAlphaProbeViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
#if RHI_RAYTRACING
		if (InViewFamily.EngineShowFlags.PathTracing && InView.State != nullptr)
		{
			SampleIndex = InView.State->GetPathTracingSampleIndex();
			SampleCount = InView.State->GetPathTracingSampleCount();
			bObservedPathTracingView = true;
		}
#endif
	}

	bool HasObservation() const { return bObservedPathTracingView; }
	uint32 GetSampleIndex() const { return SampleIndex; }
	uint32 GetSampleCount() const { return SampleCount; }

private:
	bool bObservedPathTracingView = false;
	uint32 SampleIndex = 0;
	uint32 SampleCount = 0;
};

void FPathTracingAlphaProbe::Start()
{
	OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PathTracingAlphaProbe"));
	JsonOutputPath = FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe.json"));
	FParse::Value(FCommandLine::Get(), TEXT("PathTracingAlphaProbeOutput="), JsonOutputPath);
	JsonOutputPath = FPaths::ConvertRelativePathToFull(JsonOutputPath);
	OutputDirectory = FPaths::GetPath(JsonOutputPath);
	PngOutputPath = FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe.png"));
	ExrOutputPath = FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe.exr"));
	CroppedPngOutputPath = FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe-Cropped.png"));
	CroppedExrOutputPath = FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe-Cropped.exr"));
	NormalizedPngOutputPath = FPaths::Combine(
		OutputDirectory,
		TEXT("PathTracingAlphaProbe-Normalized.png"));
	NormalizedExrOutputPath = FPaths::Combine(
		OutputDirectory,
		TEXT("PathTracingAlphaProbe-Normalized.exr"));
	CroppedNormalizedPngOutputPath = FPaths::Combine(
		OutputDirectory,
		TEXT("PathTracingAlphaProbe-Cropped-Normalized.png"));
	CroppedNormalizedExrOutputPath = FPaths::Combine(
		OutputDirectory,
		TEXT("PathTracingAlphaProbe-Cropped-Normalized.exr"));
	PreviewOutputPaths = {
		FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe-Cropped-Normalized-Black.png")),
		FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe-Cropped-Normalized-White.png")),
		FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe-Cropped-Normalized-Gray.png")),
		FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe-Cropped-Normalized-Red.png")),
		FPaths::Combine(OutputDirectory, TEXT("PathTracingAlphaProbe-Cropped-Normalized-Checkerboard.png"))
	};
	bPreviewWritten.Init(false, PreviewOutputPaths.Num());

	double ParsedTimeout = TimeoutSeconds;
	if (FParse::Value(FCommandLine::Get(), TEXT("PathTracingAlphaProbeTimeout="), ParsedTimeout))
	{
		TimeoutSeconds = FMath::Clamp(ParsedTimeout, 10.0, 300.0);
	}

	EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(
		this,
		&FPathTracingAlphaProbe::OnEngineLoopInitComplete);
}

void FPathTracingAlphaProbe::Shutdown()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (ViewportRenderedHandle.IsValid())
	{
		UGameViewportClient::OnViewportRendered().Remove(ViewportRenderedHandle);
		ViewportRenderedHandle.Reset();
	}
	ViewExtension.Reset();
}

void FPathTracingAlphaProbe::OnEngineLoopInitComplete()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}

	StartTimeSeconds = FPlatformTime::Seconds();
	ViewExtension = FSceneViewExtensions::NewExtension<FPathTracingAlphaProbeViewExtension>();
	ViewportRenderedHandle = UGameViewportClient::OnViewportRendered().AddRaw(
		this,
		&FPathTracingAlphaProbe::OnViewportRendered);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FPathTracingAlphaProbe::Tick));
}

bool FPathTracingAlphaProbe::Tick(const float DeltaTime)
{
	(void)DeltaTime;

	if (FPlatformTime::Seconds() - StartTimeSeconds > TimeoutSeconds)
	{
		Finish(false, TEXT("等待 Viewport 或 16 个 Path Tracing 样本超时。"));
		return false;
	}

	if (State == EState::WaitingForViewport)
	{
		if (SetupSceneAndPathTracing())
		{
			State = EState::WaitingForSamples;
		}
		return State != EState::Finished;
	}

	if (!ViewExtension.IsValid() || !ViewExtension->HasObservation())
	{
		return true;
	}

	FinalSampleIndex = ViewExtension->GetSampleIndex();
	TargetSampleCount = ViewExtension->GetSampleCount();
	if (!bWarmupResetRequested)
	{
		if (FPlatformTime::Seconds() - SceneReadyTimeSeconds < 5.0)
		{
			return true;
		}
		// 预热阶段限制为 15 SPP；随后改为 16 SPP，配置变化会可靠地重置 fallback 累积。
		PathTracingAlphaProbe::SetConsoleVariable(
			TEXT("r.PathTracing.SamplesPerPixel"),
			PathTracingAlphaProbe::RequiredSamples);
		bWarmupResetRequested = true;
		return true;
	}
	if (!bWarmupResetObserved)
	{
		if (FinalSampleIndex < PathTracingAlphaProbe::RequiredSamples)
		{
			bWarmupResetObserved = true;
		}
		return true;
	}
	if (FinalSampleIndex < PathTracingAlphaProbe::RequiredSamples)
	{
		return true;
	}

	// Viewport 的 RHI 纹理只保证在 OnViewportRendered 阶段有效；Ticker 中直接回读会遇到空纹理。
	bCapturePending = true;
	return true;
}

void FPathTracingAlphaProbe::OnViewportRendered(FViewport* Viewport)
{
	if (!bCapturePending || State == EState::Finished || Viewport == nullptr
		|| GEngine == nullptr || GEngine->GameViewport == nullptr
		|| Viewport != GEngine->GameViewport->Viewport)
	{
		return;
	}
	bCapturePending = false;

	FString FailureReason;
	const bool bCaptured = CaptureViewport(*Viewport, FailureReason);
	Finish(bCaptured, FailureReason);
}

bool FPathTracingAlphaProbe::SetupSceneAndPathTracing()
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr
		|| GEngine->GameViewport->Viewport == nullptr
		|| GEngine->GameViewport->GetWorld() == nullptr)
	{
		return false;
	}

#if !RHI_RAYTRACING
	Finish(false, TEXT("当前构建未包含 RHI_RAYTRACING。"));
	return false;
#else
	if (!GRHISupportsRayTracing || !GRHISupportsRayTracingShaders || !IsRayTracingEnabled()
		|| !FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(GMaxRHIShaderPlatform))
	{
		Finish(false, TEXT("当前 RHI、硬件或 Shader Platform 不满足 Path Tracing 条件。"));
		return false;
	}
#endif

	UWorld& World = *GEngine->GameViewport->GetWorld();
	APlayerController* PlayerController = World.GetFirstPlayerController();
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (PlayerController == nullptr || CubeMesh == nullptr)
	{
		Finish(false, TEXT("无法获得 PlayerController 或加载 Engine Cube。"));
		return false;
	}

	const TArray<FString> MaterialPaths = {
		TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"),
		TEXT("/Engine/EngineDebugMaterials/M_SimpleTranslucent.M_SimpleTranslucent"),
		TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial")
	};
	for (const FString& MaterialPath : MaterialPaths)
	{
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
		{
			LoadedMaterials.Add(Material);
			LoadedMaterialPaths.Add(MaterialPath);
		}
		else
		{
			MissingMaterialPaths.Add(MaterialPath);
		}
	}
	if (!MissingMaterialPaths.IsEmpty())
	{
		Finish(false, TEXT("一个或多个 Engine 探针材质加载失败。"));
		return false;
	}

	// 三个 Cube 分别覆盖不透明、简单半透明、自发光材质，背景保留为空以观察透明 Alpha。
	const FVector CubeLocations[] = {
		FVector(0.0, -155.0, 0.0),
		FVector(0.0, 0.0, 0.0),
		FVector(0.0, 155.0, 0.0)
	};
	for (int32 Index = 0; Index < LoadedMaterials.Num(); ++Index)
	{
		if (!PathTracingAlphaProbe::AddCube(
			World,
			*CubeMesh,
			*LoadedMaterials[Index],
			CubeLocations[Index],
			SpawnedActors))
		{
			Finish(false, TEXT("生成探针 Cube 失败。"));
			return false;
		}
	}

	ACameraActor* Camera = World.SpawnActor<ACameraActor>(
		FVector(-650.0, 0.0, 90.0),
		FRotator::ZeroRotator);
	APointLight* Light = World.SpawnActor<APointLight>(
		FVector(-180.0, 0.0, 300.0),
		FRotator::ZeroRotator);
	if (Camera == nullptr || Light == nullptr)
	{
		Finish(false, TEXT("生成探针相机或灯光失败。"));
		return false;
	}
	SpawnedActors.Add(Camera);
	SpawnedActors.Add(Light);
	Camera->SetActorRotation((FVector::ZeroVector - Camera->GetActorLocation()).Rotation());
	Camera->GetCameraComponent()->SetFieldOfView(52.0f);
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
	Camera->GetCameraComponent()->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	Camera->GetCameraComponent()->PostProcessSettings.DynamicGlobalIlluminationMethod =
		EDynamicGlobalIlluminationMethod::None;
	Light->PointLightComponent->SetMobility(EComponentMobility::Movable);
	Light->PointLightComponent->SetIntensity(1000000.0f);
	Light->PointLightComponent->SetAttenuationRadius(1800.0f);
	PlayerController->SetViewTarget(Camera);

	PathTracingAlphaProbe::SetConsoleVariable(
		TEXT("r.PathTracing.SamplesPerPixel"),
		PathTracingAlphaProbe::RequiredSamples - 1);
	PathTracingAlphaProbe::SetConsoleVariable(TEXT("r.PathTracing.ProgressDisplay"), 0);
	PathTracingAlphaProbe::SetConsoleVariable(TEXT("r.PathTracing.BackgroundAlpha"), 0);
	PathTracingAlphaProbe::SetConsoleVariable(TEXT("r.PathTracing.Denoiser"), 0);
	// 屏幕告警文字会污染透明区 RGB 与 Alpha，因此探针期间关闭所有屏幕调试消息。
	GAreScreenMessagesEnabled = false;
	GEngine->bEnableOnScreenDebugMessages = false;
	GEngine->bEnableOnScreenDebugMessagesDisplay = false;

	UGameViewportClient* GameViewport = GEngine->GameViewport;
	GameViewport->SetViewMode(VMI_PathTracing);
	if (GameViewport->ViewModeIndex != VMI_PathTracing)
	{
		// Cooked/受限 ViewMode 下复用 P0-2 验证过的公开 Runtime 适配路径。
		GameViewport->ViewModeIndex = VMI_PathTracing;
		ApplyViewMode(VMI_PathTracing, true, GameViewport->EngineShowFlags);
	}
	bViewModeApplied =
		GameViewport->ViewModeIndex == VMI_PathTracing
		&& GameViewport->EngineShowFlags.PathTracing;
	if (!bViewModeApplied)
	{
		Finish(false, TEXT("公开 PathTracing ViewMode 适配未能启用。"));
		return false;
	}

	bSceneCreated = true;
	SceneReadyTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(
		LogPathTracingAlphaProbe,
		Display,
		TEXT("Alpha 探针场景已创建，等待 Path Tracing 累积到 %u 样本。"),
		PathTracingAlphaProbe::RequiredSamples);
	return true;
}

bool FPathTracingAlphaProbe::CaptureViewport(FViewport& Viewport, FString& OutFailureReason)
{
	CapturedSize = Viewport.GetSizeXY();
	if (CapturedSize.X <= 0 || CapturedSize.Y <= 0)
	{
		OutFailureReason = TEXT("游戏 Viewport 尺寸无效。");
		return false;
	}

	TArray<FColor> Bgra8Pixels;
	TArray<FFloat16Color> Rgba16fPixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm, CubeFace_MAX);
	bBgra8Read = Viewport.ReadPixels(Bgra8Pixels, ReadFlags);
	bRgba16fRead = Viewport.ReadFloat16Pixels(Rgba16fPixels, ReadFlags);
	const int64 ExpectedPixelCount = static_cast<int64>(CapturedSize.X) * CapturedSize.Y;
	bBgra8Read &= Bgra8Pixels.Num() == ExpectedPixelCount;
	bRgba16fRead &= Rgba16fPixels.Num() == ExpectedPixelCount;
	if (!bBgra8Read || !bRgba16fRead)
	{
		OutFailureReason = TEXT("Viewport BGRA8 或 RGBA16F 回读失败；请确认 Float RGBA BackBuffer 设置已生效。");
		return false;
	}

	Bgra8Statistics = AnalyzeBgra8(Bgra8Pixels, CapturedSize);
	Rgba16fStatistics = AnalyzeRgba16f(Rgba16fPixels, CapturedSize);
	RawBackgroundAlpha = GetCornerBackgroundAlphaBgra8(Bgra8Pixels, CapturedSize);
	RawBackgroundAlphaRgba16f = GetCornerBackgroundAlphaRgba16f(Rgba16fPixels, CapturedSize);
	const bool bBgra8CoverageInverted = RawBackgroundAlpha > 127;
	const bool bRgba16fCoverageInverted = RawBackgroundAlphaRgba16f > 0.5f;
	if (bBgra8CoverageInverted != bRgba16fCoverageInverted)
	{
		OutFailureReason = TEXT("BGRA8 与 RGBA16F 四角背景 Alpha 对 coverage 方向的判定不一致。");
		return false;
	}
	bCoverageInverted = bBgra8CoverageInverted;

	TArray<FColor> NormalizedBgra8;
	TArray<FFloat16Color> NormalizedRgba16f;
	GlowRecoveredPixels = NormalizeBgra8(
		Bgra8Pixels,
		bCoverageInverted,
		NormalizedBgra8);
	GlowRecoveredPixelsRgba16f = NormalizeRgba16f(
		Rgba16fPixels,
		bCoverageInverted,
		NormalizedRgba16f);
	NormalizedBgra8Statistics = AnalyzeBgra8(NormalizedBgra8, CapturedSize);
	NormalizedRgba16fStatistics = AnalyzeRgba16f(NormalizedRgba16f, CapturedSize);
	Bgra8CoveredCubeCount = CountCoveredCubeBands(NormalizedBgra8, CapturedSize);
	Rgba16fCoveredCubeCount = CountCoveredCubeBands(NormalizedRgba16f, CapturedSize);
	if (!NormalizedBgra8Statistics.bHasContent || !NormalizedRgba16fStatistics.bHasContent)
	{
		OutFailureReason = TEXT("归一化 coverage 中没有可裁切内容。");
		return false;
	}

	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	bPngWritten = FImageUtils::SaveImageByExtension(
		*PngOutputPath,
		FImageView(Bgra8Pixels.GetData(), CapturedSize.X, CapturedSize.Y, EGammaSpace::sRGB));
	bExrWritten = FImageUtils::SaveImageByExtension(
		*ExrOutputPath,
		FImageView(Rgba16fPixels.GetData(), CapturedSize.X, CapturedSize.Y));

	TArray<FColor> CroppedBgra8;
	TArray<FFloat16Color> CroppedRgba16f;
	CropBgra8(
		Bgra8Pixels,
		CapturedSize,
		NormalizedBgra8Statistics.ContentBounds,
		CroppedBgra8);
	CropRgba16f(
		Rgba16fPixels,
		CapturedSize,
		NormalizedRgba16fStatistics.ContentBounds,
		CroppedRgba16f);
	bCroppedPngWritten = FImageUtils::SaveImageByExtension(
		*CroppedPngOutputPath,
		FImageView(
			CroppedBgra8.GetData(),
			NormalizedBgra8Statistics.ContentBounds.Width(),
			NormalizedBgra8Statistics.ContentBounds.Height(),
			EGammaSpace::sRGB));
	bCroppedExrWritten = FImageUtils::SaveImageByExtension(
		*CroppedExrOutputPath,
		FImageView(
			CroppedRgba16f.GetData(),
			NormalizedRgba16fStatistics.ContentBounds.Width(),
			NormalizedRgba16fStatistics.ContentBounds.Height()));

	// 所有裁切都以归一化 coverage 为准；BGRA8 与 RGBA16F 都输出完整图和裁切图。
	TArray<FColor> CroppedNormalizedBgra8;
	TArray<FFloat16Color> CroppedNormalizedRgba16f;
	CropBgra8(
		NormalizedBgra8,
		CapturedSize,
		NormalizedBgra8Statistics.ContentBounds,
		CroppedNormalizedBgra8);
	CropRgba16f(
		NormalizedRgba16f,
		CapturedSize,
		NormalizedRgba16fStatistics.ContentBounds,
		CroppedNormalizedRgba16f);
	const FIntPoint CroppedNormalizedSize(
		NormalizedBgra8Statistics.ContentBounds.Width(),
		NormalizedBgra8Statistics.ContentBounds.Height());
	const FIntPoint CroppedNormalizedRgba16fSize(
		NormalizedRgba16fStatistics.ContentBounds.Width(),
		NormalizedRgba16fStatistics.ContentBounds.Height());
	CroppedNormalizedBgra8Statistics = AnalyzeBgra8(
		CroppedNormalizedBgra8,
		CroppedNormalizedSize);
	CroppedNormalizedRgba16fStatistics = AnalyzeRgba16f(
		CroppedNormalizedRgba16f,
		CroppedNormalizedRgba16fSize);
	bNormalizationRequired =
		bCoverageInverted
		|| GlowRecoveredPixels > 0
		|| GlowRecoveredPixelsRgba16f > 0
		|| Bgra8Statistics.TransparentRgbPollutedPixels > 0
		|| Rgba16fStatistics.TransparentRgbPollutedPixels > 0;
	bNormalizedPngWritten = FImageUtils::SaveImageByExtension(
		*NormalizedPngOutputPath,
		FImageView(
			NormalizedBgra8.GetData(),
			CapturedSize.X,
			CapturedSize.Y,
			EGammaSpace::sRGB));
	bNormalizedExrWritten = FImageUtils::SaveImageByExtension(
		*NormalizedExrOutputPath,
		FImageView(NormalizedRgba16f.GetData(), CapturedSize.X, CapturedSize.Y));
	bCroppedNormalizedPngWritten = FImageUtils::SaveImageByExtension(
		*CroppedNormalizedPngOutputPath,
		FImageView(
			CroppedNormalizedBgra8.GetData(),
			CroppedNormalizedSize.X,
			CroppedNormalizedSize.Y,
			EGammaSpace::sRGB));
	bCroppedNormalizedExrWritten = FImageUtils::SaveImageByExtension(
		*CroppedNormalizedExrOutputPath,
		FImageView(
			CroppedNormalizedRgba16f.GetData(),
			CroppedNormalizedRgba16fSize.X,
			CroppedNormalizedRgba16fSize.Y));

	const FColor PreviewBackgrounds[] = {
		FColor::Black,
		FColor::White,
		FColor(128, 128, 128, 255),
		FColor::Red,
		FColor::Black
	};
	for (int32 PreviewIndex = 0; PreviewIndex < PreviewOutputPaths.Num(); ++PreviewIndex)
	{
		TArray<FColor> PreviewPixels;
		CompositeStraightAlpha(
			CroppedNormalizedBgra8,
			CroppedNormalizedSize,
			PreviewBackgrounds[PreviewIndex],
			PreviewIndex == PreviewOutputPaths.Num() - 1,
			PreviewPixels);
		bPreviewWritten[PreviewIndex] = FImageUtils::SaveImageByExtension(
			*PreviewOutputPaths[PreviewIndex],
			FImageView(
				PreviewPixels.GetData(),
				CroppedNormalizedSize.X,
				CroppedNormalizedSize.Y,
				EGammaSpace::sRGB));
	}

	const bool bAllPreviewsWritten = !bPreviewWritten.Contains(false);
	const bool bNormalizedStatisticsClean =
		NormalizedBgra8Statistics.TransparentRgbPollutedPixels == 0
		&& CroppedNormalizedBgra8Statistics.TransparentRgbPollutedPixels == 0
		&& NormalizedRgba16fStatistics.TransparentRgbPollutedPixels == 0
		&& CroppedNormalizedRgba16fStatistics.TransparentRgbPollutedPixels == 0;
	const bool bAllThreeCubesCovered =
		Bgra8CoveredCubeCount == 3
		&& Rgba16fCoveredCubeCount == 3;
	bWebReady =
		bNormalizedPngWritten
		&& bNormalizedExrWritten
		&& bCroppedNormalizedPngWritten
		&& bCroppedNormalizedExrWritten
		&& bAllPreviewsWritten
		&& bNormalizedStatisticsClean
		&& bAllThreeCubesCovered;

	if (!bPngWritten || !bExrWritten || !bCroppedPngWritten || !bCroppedExrWritten
		|| !bWebReady)
	{
		if (!bAllThreeCubesCovered)
		{
			OutFailureReason = FString::Printf(
				TEXT("归一化 coverage 未覆盖 3 个 Cube（BGRA8=%d，RGBA16F=%d）。"),
				Bgra8CoveredCubeCount,
				Rgba16fCoveredCubeCount);
		}
		else if (!bNormalizedStatisticsClean)
		{
			OutFailureReason = TEXT("规范化图的透明像素 RGB 污染统计不为零。");
		}
		else
		{
			OutFailureReason = TEXT("FImageUtils 未能写完原图、规范图、裁切图或五张 straight-alpha 预览图。");
		}
		return false;
	}
	return true;
}

FPathTracingAlphaProbe::FPixelStatistics FPathTracingAlphaProbe::AnalyzeBgra8(
	const TArray<FColor>& Pixels,
	const FIntPoint Size) const
{
	FPixelStatistics Result;
	FIntPoint Minimum(Size.X, Size.Y);
	FIntPoint Maximum(-1, -1);
	const uint8 BackgroundAlpha = Pixels.IsEmpty() ? 0 : Pixels[0].A;
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const FColor& Pixel = Pixels[Index];
		const float MaxRgb = FMath::Max3(Pixel.R, Pixel.G, Pixel.B) / 255.0f;
		if (Pixel.A <= PathTracingAlphaProbe::TransparentByteThreshold)
		{
			++Result.TransparentPixels;
			Result.MaxTransparentRgb = FMath::Max(Result.MaxTransparentRgb, static_cast<double>(MaxRgb));
			if (MaxRgb > PathTracingAlphaProbe::RgbPollutionThreshold)
			{
				++Result.TransparentRgbPollutedPixels;
			}
		}
		else
		{
			if (Pixel.A >= PathTracingAlphaProbe::OpaqueByteThreshold)
			{
				++Result.OpaquePixels;
			}
			else
			{
				++Result.TranslucentPixels;
			}
		}
		// BackBuffer 背景可能采用不透明合成 Alpha；只按相对背景 Alpha 的变化裁切。
		if (FMath::Abs(static_cast<int32>(Pixel.A) - BackgroundAlpha) > 1)
		{
			const int32 X = Index % Size.X;
			const int32 Y = Index / Size.X;
			Minimum.X = FMath::Min(Minimum.X, X);
			Minimum.Y = FMath::Min(Minimum.Y, Y);
			Maximum.X = FMath::Max(Maximum.X, X);
			Maximum.Y = FMath::Max(Maximum.Y, Y);
		}
	}
	Result.bHasContent = Maximum.X >= Minimum.X && Maximum.Y >= Minimum.Y;
	Result.ContentBounds = Result.bHasContent
		? FIntRect(Minimum, Maximum + FIntPoint(1, 1))
		: FIntRect();
	return Result;
}

FPathTracingAlphaProbe::FPixelStatistics FPathTracingAlphaProbe::AnalyzeRgba16f(
	const TArray<FFloat16Color>& Pixels,
	const FIntPoint Size) const
{
	FPixelStatistics Result;
	FIntPoint Minimum(Size.X, Size.Y);
	FIntPoint Maximum(-1, -1);
	const float BackgroundAlpha = Pixels.IsEmpty() ? 0.0f : Pixels[0].A.GetFloat();
	for (int32 Index = 0; Index < Pixels.Num(); ++Index)
	{
		const FLinearColor Pixel = Pixels[Index].GetFloats();
		const float MaxRgb = FMath::Max3(FMath::Abs(Pixel.R), FMath::Abs(Pixel.G), FMath::Abs(Pixel.B));
		if (Pixel.A <= PathTracingAlphaProbe::TransparentFloatThreshold)
		{
			++Result.TransparentPixels;
			Result.MaxTransparentRgb = FMath::Max(Result.MaxTransparentRgb, static_cast<double>(MaxRgb));
			if (MaxRgb > PathTracingAlphaProbe::RgbPollutionThreshold)
			{
				++Result.TransparentRgbPollutedPixels;
			}
		}
		else
		{
			if (Pixel.A >= PathTracingAlphaProbe::OpaqueFloatThreshold)
			{
				++Result.OpaquePixels;
			}
			else
			{
				++Result.TranslucentPixels;
			}
		}
		if (FMath::Abs(Pixel.A - BackgroundAlpha)
			> PathTracingAlphaProbe::TransparentFloatThreshold)
		{
			const int32 X = Index % Size.X;
			const int32 Y = Index / Size.X;
			Minimum.X = FMath::Min(Minimum.X, X);
			Minimum.Y = FMath::Min(Minimum.Y, Y);
			Maximum.X = FMath::Max(Maximum.X, X);
			Maximum.Y = FMath::Max(Maximum.Y, Y);
		}
	}
	Result.bHasContent = Maximum.X >= Minimum.X && Maximum.Y >= Minimum.Y;
	Result.ContentBounds = Result.bHasContent
		? FIntRect(Minimum, Maximum + FIntPoint(1, 1))
		: FIntRect();
	return Result;
}

void FPathTracingAlphaProbe::CropBgra8(
	const TArray<FColor>& Source,
	const FIntPoint SourceSize,
	const FIntRect& Bounds,
	TArray<FColor>& OutPixels)
{
	OutPixels.SetNumUninitialized(Bounds.Width() * Bounds.Height());
	for (int32 Y = 0; Y < Bounds.Height(); ++Y)
	{
		FMemory::Memcpy(
			OutPixels.GetData() + Y * Bounds.Width(),
			Source.GetData() + (Bounds.Min.Y + Y) * SourceSize.X + Bounds.Min.X,
			Bounds.Width() * sizeof(FColor));
	}
}

void FPathTracingAlphaProbe::CropRgba16f(
	const TArray<FFloat16Color>& Source,
	const FIntPoint SourceSize,
	const FIntRect& Bounds,
	TArray<FFloat16Color>& OutPixels)
{
	OutPixels.SetNumUninitialized(Bounds.Width() * Bounds.Height());
	for (int32 Y = 0; Y < Bounds.Height(); ++Y)
	{
		FMemory::Memcpy(
			OutPixels.GetData() + Y * Bounds.Width(),
			Source.GetData() + (Bounds.Min.Y + Y) * SourceSize.X + Bounds.Min.X,
			Bounds.Width() * sizeof(FFloat16Color));
	}
}

uint8 FPathTracingAlphaProbe::GetCornerBackgroundAlphaBgra8(
	const TArray<FColor>& Source,
	const FIntPoint Size)
{
	if (Size.X <= 0 || Size.Y <= 0
		|| Source.Num() != static_cast<int64>(Size.X) * Size.Y)
	{
		return 0;
	}
	const int32 LastX = Size.X - 1;
	const int32 LastY = Size.Y - 1;
	const uint32 Sum =
		Source[0].A
		+ Source[LastX].A
		+ Source[LastY * Size.X].A
		+ Source[LastY * Size.X + LastX].A;
	return static_cast<uint8>((Sum + 2) / 4);
}

float FPathTracingAlphaProbe::GetCornerBackgroundAlphaRgba16f(
	const TArray<FFloat16Color>& Source,
	const FIntPoint Size)
{
	if (Size.X <= 0 || Size.Y <= 0
		|| Source.Num() != static_cast<int64>(Size.X) * Size.Y)
	{
		return 0.0f;
	}
	const int32 LastX = Size.X - 1;
	const int32 LastY = Size.Y - 1;
	return (
		Source[0].A.GetFloat()
		+ Source[LastX].A.GetFloat()
		+ Source[LastY * Size.X].A.GetFloat()
		+ Source[LastY * Size.X + LastX].A.GetFloat()) * 0.25f;
}

int64 FPathTracingAlphaProbe::NormalizeBgra8(
	const TArray<FColor>& Source,
	const bool bInvertCoverage,
	TArray<FColor>& OutPixels)
{
	OutPixels = Source;
	int64 RecoveredPixels = 0;
	for (FColor& Pixel : OutPixels)
	{
		const uint8 Coverage = bInvertCoverage ? 255 - Pixel.A : Pixel.A;
		const uint8 MaxRgb = FMath::Max3(Pixel.R, Pixel.G, Pixel.B);
		if (Coverage <= PathTracingAlphaProbe::TransparentByteThreshold
			&& MaxRgb > PathTracingAlphaProbe::TransparentByteThreshold)
		{
			// 加法自发光没有几何 coverage：以最亮通道构造 Alpha，并从预乘贡献反解 straight RGB。
			Pixel.R = static_cast<uint8>(
				FMath::Clamp((static_cast<uint32>(Pixel.R) * 255 + MaxRgb / 2) / MaxRgb, 0u, 255u));
			Pixel.G = static_cast<uint8>(
				FMath::Clamp((static_cast<uint32>(Pixel.G) * 255 + MaxRgb / 2) / MaxRgb, 0u, 255u));
			Pixel.B = static_cast<uint8>(
				FMath::Clamp((static_cast<uint32>(Pixel.B) * 255 + MaxRgb / 2) / MaxRgb, 0u, 255u));
			Pixel.A = MaxRgb;
			++RecoveredPixels;
		}
		else
		{
			Pixel.A = Coverage;
			if (Coverage <= PathTracingAlphaProbe::TransparentByteThreshold)
			{
				Pixel.R = 0;
				Pixel.G = 0;
				Pixel.B = 0;
			}
		}
	}
	return RecoveredPixels;
}

int64 FPathTracingAlphaProbe::NormalizeRgba16f(
	const TArray<FFloat16Color>& Source,
	const bool bInvertCoverage,
	TArray<FFloat16Color>& OutPixels)
{
	OutPixels.SetNumUninitialized(Source.Num());
	int64 RecoveredPixels = 0;
	for (int32 Index = 0; Index < Source.Num(); ++Index)
	{
		FLinearColor Pixel = Source[Index].GetFloats();
		const float Coverage = FMath::Clamp(bInvertCoverage ? 1.0f - Pixel.A : Pixel.A, 0.0f, 1.0f);
		const float MaxRgb = FMath::Max3(Pixel.R, Pixel.G, Pixel.B);
		if (Coverage <= PathTracingAlphaProbe::TransparentFloatThreshold
			&& MaxRgb > PathTracingAlphaProbe::RgbPollutionThreshold)
		{
			const float SyntheticAlpha = FMath::Clamp(MaxRgb, 0.0f, 1.0f);
			Pixel.R /= SyntheticAlpha;
			Pixel.G /= SyntheticAlpha;
			Pixel.B /= SyntheticAlpha;
			Pixel.A = SyntheticAlpha;
			++RecoveredPixels;
		}
		else
		{
			Pixel.A = Coverage;
			if (Coverage <= PathTracingAlphaProbe::TransparentFloatThreshold)
			{
				Pixel.R = 0.0f;
				Pixel.G = 0.0f;
				Pixel.B = 0.0f;
			}
		}
		OutPixels[Index] = FFloat16Color(Pixel);
	}
	return RecoveredPixels;
}

int32 FPathTracingAlphaProbe::CountCoveredCubeBands(
	const TArray<FColor>& Source,
	const FIntPoint Size)
{
	int32 MinimumX = Size.X;
	int32 MaximumX = -1;
	for (int32 X = 0; X < Size.X; ++X)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			if (Source[Y * Size.X + X].A > PathTracingAlphaProbe::TransparentByteThreshold)
			{
				MinimumX = FMath::Min(MinimumX, X);
				MaximumX = FMath::Max(MaximumX, X);
				break;
			}
		}
	}
	if (MaximumX < MinimumX)
	{
		return 0;
	}

	int64 OccupiedPixels[3] = {0, 0, 0};
	const int32 ContentWidth = MaximumX - MinimumX + 1;
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = MinimumX; X <= MaximumX; ++X)
		{
			if (Source[Y * Size.X + X].A > PathTracingAlphaProbe::TransparentByteThreshold)
			{
				const int32 Band = FMath::Min(2, (X - MinimumX) * 3 / ContentWidth);
				++OccupiedPixels[Band];
			}
		}
	}
	const int64 MinimumPixelsPerCubeBand = FMath::Max(16, Size.Y / 4);
	return static_cast<int32>(OccupiedPixels[0] >= MinimumPixelsPerCubeBand)
		+ static_cast<int32>(OccupiedPixels[1] >= MinimumPixelsPerCubeBand)
		+ static_cast<int32>(OccupiedPixels[2] >= MinimumPixelsPerCubeBand);
}

int32 FPathTracingAlphaProbe::CountCoveredCubeBands(
	const TArray<FFloat16Color>& Source,
	const FIntPoint Size)
{
	int32 MinimumX = Size.X;
	int32 MaximumX = -1;
	for (int32 X = 0; X < Size.X; ++X)
	{
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			if (Source[Y * Size.X + X].A.GetFloat()
				> PathTracingAlphaProbe::TransparentFloatThreshold)
			{
				MinimumX = FMath::Min(MinimumX, X);
				MaximumX = FMath::Max(MaximumX, X);
				break;
			}
		}
	}
	if (MaximumX < MinimumX)
	{
		return 0;
	}

	int64 OccupiedPixels[3] = {0, 0, 0};
	const int32 ContentWidth = MaximumX - MinimumX + 1;
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = MinimumX; X <= MaximumX; ++X)
		{
			if (Source[Y * Size.X + X].A.GetFloat()
				> PathTracingAlphaProbe::TransparentFloatThreshold)
			{
				const int32 Band = FMath::Min(2, (X - MinimumX) * 3 / ContentWidth);
				++OccupiedPixels[Band];
			}
		}
	}
	const int64 MinimumPixelsPerCubeBand = FMath::Max(16, Size.Y / 4);
	return static_cast<int32>(OccupiedPixels[0] >= MinimumPixelsPerCubeBand)
		+ static_cast<int32>(OccupiedPixels[1] >= MinimumPixelsPerCubeBand)
		+ static_cast<int32>(OccupiedPixels[2] >= MinimumPixelsPerCubeBand);
}

void FPathTracingAlphaProbe::CompositeStraightAlpha(
	const TArray<FColor>& Source,
	const FIntPoint Size,
	const FColor& Background,
	const bool bCheckerboard,
	TArray<FColor>& OutPixels)
{
	check(Source.Num() == static_cast<int64>(Size.X) * Size.Y);
	OutPixels.SetNumUninitialized(Source.Num());
	for (int32 Index = 0; Index < Source.Num(); ++Index)
	{
		const int32 X = Index % Size.X;
		const int32 Y = Index / Size.X;
		const uint8 CheckerValue = ((X / 16 + Y / 16) & 1) == 0 ? 192 : 96;
		const FColor EffectiveBackground = bCheckerboard
			? FColor(CheckerValue, CheckerValue, CheckerValue, 255)
			: Background;
		const FColor& SourcePixel = Source[Index];
		const uint32 Alpha = SourcePixel.A;
		const uint32 InverseAlpha = 255 - Alpha;
		OutPixels[Index] = FColor(
			static_cast<uint8>((SourcePixel.R * Alpha + EffectiveBackground.R * InverseAlpha + 127) / 255),
			static_cast<uint8>((SourcePixel.G * Alpha + EffectiveBackground.G * InverseAlpha + 127) / 255),
			static_cast<uint8>((SourcePixel.B * Alpha + EffectiveBackground.B * InverseAlpha + 127) / 255),
			255);
	}
}

void FPathTracingAlphaProbe::Finish(const bool bSuccess, const FString& FailureReason)
{
	if (State == EState::Finished)
	{
		return;
	}
	State = EState::Finished;
	TickerHandle.Reset();
	if (ViewportRenderedHandle.IsValid())
	{
		UGameViewportClient::OnViewportRendered().Remove(ViewportRenderedHandle);
		ViewportRenderedHandle.Reset();
	}

	TArray<TSharedPtr<FJsonValue>> LoadedMaterialValues;
	for (const FString& Path : LoadedMaterialPaths)
	{
		LoadedMaterialValues.Add(MakeShared<FJsonValueString>(Path));
	}
	TArray<TSharedPtr<FJsonValue>> MissingMaterialValues;
	for (const FString& Path : MissingMaterialPaths)
	{
		MissingMaterialValues.Add(MakeShared<FJsonValueString>(Path));
	}

	TSharedRef<FJsonObject> Outputs = MakeShared<FJsonObject>();
	Outputs->SetStringField(TEXT("png"), FPaths::GetCleanFilename(PngOutputPath));
	Outputs->SetStringField(TEXT("exr"), FPaths::GetCleanFilename(ExrOutputPath));
	Outputs->SetStringField(TEXT("croppedPng"), FPaths::GetCleanFilename(CroppedPngOutputPath));
	Outputs->SetStringField(TEXT("croppedExr"), FPaths::GetCleanFilename(CroppedExrOutputPath));
	Outputs->SetStringField(TEXT("normalizedPng"), FPaths::GetCleanFilename(NormalizedPngOutputPath));
	Outputs->SetStringField(TEXT("normalizedExr"), FPaths::GetCleanFilename(NormalizedExrOutputPath));
	Outputs->SetStringField(
		TEXT("croppedNormalizedPng"),
		FPaths::GetCleanFilename(CroppedNormalizedPngOutputPath));
	Outputs->SetStringField(
		TEXT("croppedNormalizedExr"),
		FPaths::GetCleanFilename(CroppedNormalizedExrOutputPath));
	Outputs->SetBoolField(TEXT("pngWritten"), bPngWritten);
	Outputs->SetBoolField(TEXT("exrWritten"), bExrWritten);
	Outputs->SetBoolField(TEXT("croppedPngWritten"), bCroppedPngWritten);
	Outputs->SetBoolField(TEXT("croppedExrWritten"), bCroppedExrWritten);
	Outputs->SetBoolField(TEXT("normalizedPngWritten"), bNormalizedPngWritten);
	Outputs->SetBoolField(TEXT("normalizedExrWritten"), bNormalizedExrWritten);
	Outputs->SetBoolField(TEXT("croppedNormalizedPngWritten"), bCroppedNormalizedPngWritten);
	Outputs->SetBoolField(TEXT("croppedNormalizedExrWritten"), bCroppedNormalizedExrWritten);

	const TCHAR* PreviewNames[] = {
		TEXT("black"),
		TEXT("white"),
		TEXT("gray"),
		TEXT("red"),
		TEXT("checkerboard")
	};
	TSharedRef<FJsonObject> Previews = MakeShared<FJsonObject>();
	for (int32 PreviewIndex = 0; PreviewIndex < PreviewOutputPaths.Num(); ++PreviewIndex)
	{
		TSharedRef<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(
			TEXT("path"),
			FPaths::GetCleanFilename(PreviewOutputPaths[PreviewIndex]));
		Preview->SetBoolField(
			TEXT("written"),
			bPreviewWritten.IsValidIndex(PreviewIndex) && bPreviewWritten[PreviewIndex]);
		Previews->SetObjectField(PreviewNames[PreviewIndex], Preview);
	}
	Outputs->SetObjectField(TEXT("previews"), Previews);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 3);
	Root->SetStringField(TEXT("probe"), TEXT("PathTracingAlphaProbe"));
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("buildConfiguration"), PathTracingAlphaProbe::GetBuildConfigurationName());
	Root->SetBoolField(TEXT("withEditor"), WITH_EDITOR != 0);
	Root->SetStringField(TEXT("graphicsRHI"), FApp::GetGraphicsRHI());
	Root->SetStringField(TEXT("gpuAdapterName"), GRHIAdapterName);
	Root->SetBoolField(TEXT("sceneCreated"), bSceneCreated);
	Root->SetBoolField(TEXT("publicPathTracingAdapterApplied"), bViewModeApplied);
	Root->SetNumberField(TEXT("requiredSamples"), PathTracingAlphaProbe::RequiredSamples);
	Root->SetNumberField(TEXT("targetSampleCount"), TargetSampleCount);
	Root->SetNumberField(TEXT("finalSampleIndex"), FinalSampleIndex);
	Root->SetNumberField(TEXT("width"), CapturedSize.X);
	Root->SetNumberField(TEXT("height"), CapturedSize.Y);
	Root->SetBoolField(TEXT("bgra8Read"), bBgra8Read);
	Root->SetBoolField(TEXT("rgba16fRead"), bRgba16fRead);
	Root->SetNumberField(TEXT("rawBackgroundAlpha"), RawBackgroundAlpha);
	Root->SetNumberField(TEXT("rawBackgroundAlphaRgba16f"), RawBackgroundAlphaRgba16f);
	Root->SetBoolField(TEXT("coverageInverted"), bCoverageInverted);
	Root->SetNumberField(TEXT("glowRecoveredPixels"), static_cast<double>(GlowRecoveredPixels));
	Root->SetNumberField(
		TEXT("glowRecoveredPixelsRgba16f"),
		static_cast<double>(GlowRecoveredPixelsRgba16f));
	Root->SetNumberField(TEXT("bgra8CoveredCubeCount"), Bgra8CoveredCubeCount);
	Root->SetNumberField(TEXT("rgba16fCoveredCubeCount"), Rgba16fCoveredCubeCount);
	Root->SetArrayField(TEXT("loadedMaterials"), LoadedMaterialValues);
	Root->SetArrayField(TEXT("missingMaterials"), MissingMaterialValues);
	Root->SetObjectField(TEXT("bgra8"), PathTracingAlphaProbe::MakeStatisticsJson(Bgra8Statistics));
	Root->SetObjectField(TEXT("rgba16f"), PathTracingAlphaProbe::MakeStatisticsJson(Rgba16fStatistics));
	Root->SetObjectField(
		TEXT("normalizedBgra8"),
		PathTracingAlphaProbe::MakeStatisticsJson(NormalizedBgra8Statistics));
	Root->SetObjectField(
		TEXT("normalizedRgba16f"),
		PathTracingAlphaProbe::MakeStatisticsJson(NormalizedRgba16fStatistics));
	Root->SetObjectField(
		TEXT("croppedNormalizedBgra8"),
		PathTracingAlphaProbe::MakeStatisticsJson(CroppedNormalizedBgra8Statistics));
	Root->SetObjectField(
		TEXT("croppedNormalizedRgba16f"),
		PathTracingAlphaProbe::MakeStatisticsJson(CroppedNormalizedRgba16fStatistics));
	Root->SetBoolField(TEXT("normalizationRequired"), bNormalizationRequired);
	Root->SetStringField(TEXT("alphaMode"), TEXT("straight"));
	Root->SetBoolField(TEXT("webReady"), bWebReady);
	Root->SetObjectField(TEXT("outputs"), Outputs);
	Root->SetBoolField(TEXT("passed"), bSuccess);
	Root->SetStringField(TEXT("failureReason"), FailureReason);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bSerialized = FJsonSerializer::Serialize(Root, Writer);
	const bool bDirectoryReady = IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const bool bWritten = bSerialized && bDirectoryReady
		&& FFileHelper::SaveStringToFile(
			JsonText,
			*JsonOutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bWritten && bSuccess)
	{
		UE_LOG(
			LogPathTracingAlphaProbe,
			Display,
			TEXT("Path Tracing Alpha 探针通过；JSON：%s"),
			*JsonOutputPath);
	}
	else
	{
		UE_LOG(
			LogPathTracingAlphaProbe,
			Error,
			TEXT("Path Tracing Alpha 探针失败：%s；JSON：%s"),
			*FailureReason,
			*JsonOutputPath);
	}
	FPlatformMisc::RequestExitWithStatus(false, bWritten && bSuccess ? 0 : 2);
}
