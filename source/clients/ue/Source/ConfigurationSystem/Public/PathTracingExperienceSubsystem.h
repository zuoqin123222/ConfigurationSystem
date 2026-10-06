#pragma once

#include "CoreMinimal.h"
#include "RenderCommandFence.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "PathTracingExperienceSubsystem.generated.h"

class FPathTracingExperienceViewExtension;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FPathTracingProgressChanged, int32, CurrentSample, int32, TargetSamples);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPathTracingWarmupStateChanged);

enum class EPathTracingWarmupState : uint8
{
	Idle,
	Starting,
	WaitingForRenderFence,
	WaitingForPipelineCache,
	Ready,
	Failed
};

/** 与渲染器和 UObject 无关的状态机决策，供运行时与自动化测试共用。 */
struct CONFIGURATIONSYSTEM_API FPathTracingWarmupPolicy
{
	static constexpr uint64 MinimumDedicatedVideoMemoryBytes =
		6ull * 1024ull * 1024ull * 1024ull;
	static constexpr double TimeoutSeconds = 90.0;

	static bool HasEnoughVideoMemory(uint64 DedicatedVideoMemoryBytes);
	static EPathTracingWarmupState AdvanceWaitState(
		EPathTracingWarmupState State,
		bool bRenderFenceComplete,
		uint32 ActivePipelinePrecacheRequests,
		double ElapsedSeconds);
	static bool ShouldApplyPathTracing(
		EPathTracingWarmupState State,
		bool bPathTracingRequested);
};

/**
 * 产品运行时 Path Tracing 门面。只使用公开 ViewMode/ViewState API，
 * 对不支持的平台给出失败原因，并向 C++ UMG 推送精确采样进度。
 */
UCLASS()
class CONFIGURATIONSYSTEM_API UPathTracingExperienceSubsystem final
	: public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="Configurator|Path Tracing")
	bool SetPathTracingEnabled(bool bEnabled, FString& OutFailureReason);

	UFUNCTION(BlueprintPure, Category="Configurator|Path Tracing")
	bool IsPathTracingEnabled() const { return bPathTracingEnabled; }

	UFUNCTION(BlueprintPure, Category="Configurator|Path Tracing")
	bool IsPreparingPathTracing() const;

	UFUNCTION(BlueprintPure, Category="Configurator|Path Tracing")
	float GetProgress01() const;

	UFUNCTION(BlueprintPure, Category="Configurator|Path Tracing")
	int32 GetCurrentSample() const { return CurrentSample; }

	UFUNCTION(BlueprintPure, Category="Configurator|Path Tracing")
	int32 GetTargetSamples() const { return TargetSamples; }

	UPROPERTY(BlueprintAssignable, Category="Configurator|Path Tracing")
	FPathTracingProgressChanged OnProgressChanged;

	UPROPERTY(BlueprintAssignable, Category="Configurator|Path Tracing")
	FPathTracingWarmupStateChanged OnWarmupStateChanged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Configurator|Path Tracing",
		meta=(ClampMin="1", ClampMax="8192"))
	int32 ProductSamplesPerPixel = 256;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual UWorld* GetTickableGameObjectWorld() const override;

private:
	bool ValidatePathTracingSupport(FString& OutFailureReason) const;
	bool ApplyViewMode(bool bEnabled, FString& OutFailureReason);
	void StartPathTracingWarmup();
	void SetWarmupState(EPathTracingWarmupState NewState);
	void FailWarmup(const FString& FailureReason);

	TSharedPtr<FPathTracingExperienceViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FRenderCommandFence WarmupRenderFence;
	EPathTracingWarmupState WarmupState = EPathTracingWarmupState::Idle;
	double WarmupStartSeconds = 0.0;
	FString WarmupFailureReason;
	bool bPathTracingRequested = false;
	bool bPathTracingEnabled = false;
	int32 CurrentSample = 0;
	int32 TargetSamples = 0;
};
