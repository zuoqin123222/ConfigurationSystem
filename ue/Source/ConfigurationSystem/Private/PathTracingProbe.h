#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"

class FPathTracingProbeViewExtension;

/**
 * 仅由 -PathTracingProbe 启用的 Runtime 技术探针。
 *
 * 探针只使用公开 Runtime API，并在完成或超时后写出 JSON、退出进程。
 */
class FPathTracingProbe final
{
public:
	void Start();
	void Shutdown();

private:
	enum class EState : uint8
	{
		WaitingForViewport,
		WaitingForSampleGrowth,
		WaitingForReset,
		WaitingForRegrowth,
		Finished
	};

	void OnEngineLoopInitComplete();
	bool Tick(float DeltaTime);
	bool TryStartPathTracing();
	bool RotateCamera();
	bool RestoreRealtimeMode();
	void Finish(bool bSuccess, const FString& FailureReason);

	FDelegateHandle EngineInitCompleteHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TSharedPtr<FPathTracingProbeViewExtension, ESPMode::ThreadSafe> ViewExtension;

	EState State = EState::WaitingForViewport;
	FString OutputPath;
	double StartTimeSeconds = 0.0;
	double TimeoutSeconds = 30.0;

	bool bCompiledWithRayTracing = false;
	bool bRHISupportsRayTracing = false;
	bool bRHISupportsRayTracingShaders = false;
	bool bRayTracingEnabled = false;
	bool bPlatformSupportsPathTracing = false;
	bool bGuardedSetViewModeApplied = false;
	bool bDirectApplyViewModeAttempted = false;
	bool bDirectApplyViewModeApplied = false;
	bool bViewModeApplied = false;
	bool bSampleGrowthObserved = false;
	bool bCameraRotationApplied = false;
	bool bResetObserved = false;
	bool bRegrowthObserved = false;
	bool bRealtimeModeRestored = false;

	uint32 InitialSampleIndex = 0;
	uint32 SampleIndexBeforeRotation = 0;
	uint32 SampleIndexAfterRotation = 0;
	uint32 SampleIndexAfterRegrowth = 0;
	uint32 FinalSampleIndex = 0;
	uint32 TargetSampleCount = 0;
	FRotator OriginalControlRotation = FRotator::ZeroRotator;
};
