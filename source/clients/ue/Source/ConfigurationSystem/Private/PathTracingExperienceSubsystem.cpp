#include "PathTracingExperienceSubsystem.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PipelineStateCache.h"
#include "DynamicRHI.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RHIStats.h"
#include "RenderUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneViewExtension.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathTracingExperience, Log, All);

#if UE_BUILD_SHIPPING && RHI_RAYTRACING
// UE5.8 将此声明保留在 Renderer/Private/PathTracing.h 且未导出。
// Shipping 游戏目标采用单体链接，显式依赖 Renderer 后可直接复用该实现，
// 无需把引擎私有头文件暴露给项目。
void PreparePathTracingRTPSO();
#endif

namespace
{
	uint64 GetDedicatedVideoMemoryBytes()
	{
		FTextureMemoryStats MemoryStats;
		RHIGetTextureMemoryStats(MemoryStats);
		return MemoryStats.DedicatedVideoMemory > 0
			? static_cast<uint64>(MemoryStats.DedicatedVideoMemory)
			: 0;
	}

	void DispatchPathTracingRTPSOWarmup()
	{
#if UE_BUILD_SHIPPING && RHI_RAYTRACING
		PreparePathTracingRTPSO();
#endif
	}
}

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
	bPathTracingRequested = false;
	FString IgnoredFailureReason;
	ApplyViewMode(false, IgnoredFailureReason);
	ViewExtension.Reset();
	Super::Deinitialize();
}

bool FPathTracingWarmupPolicy::HasEnoughVideoMemory(
	const uint64 DedicatedVideoMemoryBytes)
{
	return DedicatedVideoMemoryBytes >= MinimumDedicatedVideoMemoryBytes;
}

EPathTracingWarmupState FPathTracingWarmupPolicy::AdvanceWaitState(
	const EPathTracingWarmupState State,
	const bool bRenderFenceComplete,
	const uint32 ActivePipelinePrecacheRequests,
	const double ElapsedSeconds)
{
	if (ElapsedSeconds >= TimeoutSeconds)
	{
		return EPathTracingWarmupState::Failed;
	}
	if (State == EPathTracingWarmupState::WaitingForRenderFence)
	{
		return bRenderFenceComplete
			? EPathTracingWarmupState::WaitingForPipelineCache
			: State;
	}
	if (State == EPathTracingWarmupState::WaitingForPipelineCache
		&& ActivePipelinePrecacheRequests == 0)
	{
		return EPathTracingWarmupState::Ready;
	}
	return State;
}

bool FPathTracingWarmupPolicy::ShouldApplyPathTracing(
	const EPathTracingWarmupState State,
	const bool bPathTracingRequested)
{
	return State == EPathTracingWarmupState::Ready && bPathTracingRequested;
}

bool UPathTracingExperienceSubsystem::SetPathTracingEnabled(
	const bool bEnabled,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!bEnabled)
	{
		bPathTracingRequested = false;
		return ApplyViewMode(false, OutFailureReason);
	}

	if (!ValidatePathTracingSupport(OutFailureReason))
	{
		bPathTracingRequested = false;
		return false;
	}
	bPathTracingRequested = true;

	if (WarmupState == EPathTracingWarmupState::Ready)
	{
		return ApplyViewMode(true, OutFailureReason);
	}
	if (IsPreparingPathTracing())
	{
		return true;
	}

	StartPathTracingWarmup();
	return true;
}

bool UPathTracingExperienceSubsystem::IsPreparingPathTracing() const
{
	return WarmupState == EPathTracingWarmupState::Starting
		|| WarmupState == EPathTracingWarmupState::WaitingForRenderFence
		|| WarmupState == EPathTracingWarmupState::WaitingForPipelineCache;
}

bool UPathTracingExperienceSubsystem::ValidatePathTracingSupport(
	FString& OutFailureReason) const
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		OutFailureReason = TEXT("游戏视口尚未就绪。");
		return false;
	}
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
	if (!FPathTracingWarmupPolicy::HasEnoughVideoMemory(
		GetDedicatedVideoMemoryBytes()))
	{
		OutFailureReason = TEXT("Path Tracing 至少需要 6 GB 独立显存。");
		return false;
	}
	return true;
#endif
}

bool UPathTracingExperienceSubsystem::ApplyViewMode(
	const bool bEnabled,
	FString& OutFailureReason)
{
	UGameViewportClient* Viewport = GEngine != nullptr ? GEngine->GameViewport : nullptr;
	if (Viewport == nullptr)
	{
		OutFailureReason = TEXT("游戏视口尚未就绪。");
		return false;
	}
	if (bEnabled)
	{
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
		::ApplyViewMode(VMI_PathTracing, true, Viewport->EngineShowFlags);
		if (!Viewport->EngineShowFlags.PathTracing)
		{
			OutFailureReason = TEXT("运行时视图策略拒绝启用 Path Tracing。");
			return false;
		}
		UE_LOG(
			LogPathTracingExperience,
			Display,
			TEXT("Path Tracing 预热已完成，现切换到产品渲染模式。"));
	}
	else
	{
		Viewport->ViewModeIndex = VMI_Lit;
		::ApplyViewMode(VMI_Lit, true, Viewport->EngineShowFlags);
	}

	bPathTracingEnabled = bEnabled;
	CurrentSample = 0;
	TargetSamples = bEnabled ? ProductSamplesPerPixel : 0;
	OnProgressChanged.Broadcast(CurrentSample, TargetSamples);
	return true;
}

void UPathTracingExperienceSubsystem::StartPathTracingWarmup()
{
	FString IgnoredFailureReason;
	ApplyViewMode(false, IgnoredFailureReason);
	WarmupFailureReason.Reset();
	WarmupStartSeconds = FPlatformTime::Seconds();
	SetWarmupState(EPathTracingWarmupState::Starting);
	UE_LOG(
		LogPathTracingExperience,
		Display,
		TEXT("开始后台预热 UE5.8 Path Tracing RTPSO；保持 Lit 画面。GPU=%s，独立显存=%.2f GiB。"),
		*GRHIGlobals.GpuInfo.AdapterName,
		static_cast<double>(GetDedicatedVideoMemoryBytes())
			/ (1024.0 * 1024.0 * 1024.0));
	// 入口自身只向渲染线程排队，必须在游戏线程直接调用，避免引入无意义的
	// 通用线程池竞态。Fence 排在预热命令之后，完成时所有 RTPSO 请求均已提交。
	DispatchPathTracingRTPSOWarmup();
	WarmupRenderFence.BeginFence();
	SetWarmupState(EPathTracingWarmupState::WaitingForRenderFence);
}

void UPathTracingExperienceSubsystem::SetWarmupState(
	const EPathTracingWarmupState NewState)
{
	if (WarmupState != NewState)
	{
		WarmupState = NewState;
		OnWarmupStateChanged.Broadcast();
	}
}

void UPathTracingExperienceSubsystem::FailWarmup(const FString& FailureReason)
{
	WarmupFailureReason = FailureReason;
	bPathTracingRequested = false;
	FString IgnoredFailureReason;
	ApplyViewMode(false, IgnoredFailureReason);
	SetWarmupState(EPathTracingWarmupState::Failed);
	UE_LOG(
		LogPathTracingExperience,
		Error,
		TEXT("Path Tracing 预热失败，已保持实时渲染：%s"),
		*FailureReason);
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
	if (FParse::Param(FCommandLine::Get(), TEXT("ConfigurationBatchBake")))
	{
		return;
	}
	if (WarmupState == EPathTracingWarmupState::Idle
		&& GEngine != nullptr
		&& GEngine->GameViewport != nullptr)
	{
		FString FailureReason;
		if (ValidatePathTracingSupport(FailureReason))
		{
			// 视口就绪后立即后台预热；此时不请求切换，画面始终保持 Lit。
			StartPathTracingWarmup();
		}
		else
		{
			FailWarmup(FailureReason);
		}
	}
	if (IsPreparingPathTracing())
	{
		const double ElapsedSeconds = FPlatformTime::Seconds() - WarmupStartSeconds;
		const EPathTracingWarmupState NextState =
			FPathTracingWarmupPolicy::AdvanceWaitState(
				WarmupState,
				WarmupRenderFence.IsFenceComplete(),
				static_cast<uint32>(FMath::Max(
					PipelineStateCache::GetNumActivePipelinePrecompileTasks(),
					0)),
				ElapsedSeconds);
		if (NextState == EPathTracingWarmupState::Failed)
		{
			FailWarmup(TEXT("Path Tracing 管线预热超过 90 秒，已保持实时渲染。"));
			return;
		}
		if (NextState != WarmupState)
		{
			SetWarmupState(NextState);
			if (NextState == EPathTracingWarmupState::Ready)
			{
				UE_LOG(
					LogPathTracingExperience,
					Display,
					TEXT("Path Tracing RTPSO 后台预热完成，耗时 %.2f 秒。"),
					ElapsedSeconds);
			}
		}
		if (FPathTracingWarmupPolicy::ShouldApplyPathTracing(
			WarmupState, bPathTracingRequested))
		{
			FString FailureReason;
			if (!ApplyViewMode(true, FailureReason))
			{
				FailWarmup(FailureReason);
			}
			else
			{
				OnWarmupStateChanged.Broadcast();
			}
		}
	}

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
