#include "PathTracingExperienceSubsystem.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

class FPathTracingExperienceViewExtension final : public FSceneViewExtensionBase
{
public:
	FPathTracingExperienceViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override
	{
	}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
#if RHI_RAYTRACING
		if (InViewFamily.EngineShowFlags.PathTracing && InView.State != nullptr)
		{
			SampleIndex = InView.State->GetPathTracingSampleIndex();
			SampleCount = InView.State->GetPathTracingSampleCount();
			bHasSample = true;
		}
#endif
	}

	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override
	{
	}

	bool bHasSample = false;
	uint32 SampleIndex = 0;
	uint32 SampleCount = 0;
};

void UPathTracingExperienceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ViewExtension = FSceneViewExtensions::NewExtension<FPathTracingExperienceViewExtension>();
}

void UPathTracingExperienceSubsystem::Deinitialize()
{
	FString Ignored;
	SetPathTracingEnabled(false, Ignored);
	ViewExtension.Reset();
	Super::Deinitialize();
}

bool UPathTracingExperienceSubsystem::SetPathTracingEnabled(
	const bool bEnabled,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	UGameViewportClient* Viewport = GEngine != nullptr ? GEngine->GameViewport : nullptr;
	if (Viewport == nullptr)
	{
		OutFailureReason = TEXT("游戏视口尚未就绪。");
		return false;
	}

	if (bEnabled)
	{
#if !RHI_RAYTRACING
		OutFailureReason = TEXT("当前构建未编译 Ray Tracing。");
		return false;
#else
		if (!GRHISupportsRayTracing || !GRHISupportsRayTracingShaders
			|| !IsRayTracingEnabled()
			|| !FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(GMaxRHIShaderPlatform))
		{
			OutFailureReason = TEXT("当前 GPU、RHI 或 Shader Platform 不支持 Path Tracing。");
			return false;
		}
#endif
		if (IConsoleVariable* Samples =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing.SamplesPerPixel")))
		{
			Samples->Set(FMath::Clamp(ProductSamplesPerPixel, 1, 8192), ECVF_SetByCode);
		}
		if (IConsoleVariable* Progress =
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing.ProgressDisplay")))
		{
			Progress->Set(0, ECVF_SetByCode);
		}

		Viewport->ViewModeIndex = VMI_PathTracing;
		ApplyViewMode(VMI_PathTracing, true, Viewport->EngineShowFlags);
		if (!Viewport->EngineShowFlags.PathTracing)
		{
			OutFailureReason = TEXT("运行时视图策略拒绝启用 Path Tracing。");
			return false;
		}
	}
	else
	{
		Viewport->ViewModeIndex = VMI_Lit;
		ApplyViewMode(VMI_Lit, true, Viewport->EngineShowFlags);
	}

	bPathTracingEnabled = bEnabled;
	CurrentSample = 0;
	TargetSamples = bEnabled ? ProductSamplesPerPixel : 0;
	OnProgressChanged.Broadcast(CurrentSample, TargetSamples);
	return true;
}

float UPathTracingExperienceSubsystem::GetProgress01() const
{
	return TargetSamples > 0
		? FMath::Clamp(static_cast<float>(CurrentSample) / TargetSamples, 0.0f, 1.0f)
		: 0.0f;
}

void UPathTracingExperienceSubsystem::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	if (!bPathTracingEnabled || !ViewExtension.IsValid() || !ViewExtension->bHasSample)
	{
		return;
	}

	const int32 NewSample = static_cast<int32>(ViewExtension->SampleIndex);
	const int32 NewTarget = FMath::Max(
		static_cast<int32>(ViewExtension->SampleCount), ProductSamplesPerPixel);
	if (NewSample != CurrentSample || NewTarget != TargetSamples)
	{
		CurrentSample = NewSample;
		TargetSamples = NewTarget;
		OnProgressChanged.Broadcast(CurrentSample, TargetSamples);
	}
}

TStatId UPathTracingExperienceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UPathTracingExperienceSubsystem, STATGROUP_Tickables);
}

UWorld* UPathTracingExperienceSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
}
