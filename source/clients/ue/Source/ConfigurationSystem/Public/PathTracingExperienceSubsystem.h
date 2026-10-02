#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "PathTracingExperienceSubsystem.generated.h"

class FPathTracingExperienceViewExtension;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FPathTracingProgressChanged, int32, CurrentSample, int32, TargetSamples);

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
	float GetProgress01() const;

	UFUNCTION(BlueprintPure, Category="Configurator|Path Tracing")
	int32 GetCurrentSample() const { return CurrentSample; }

	UFUNCTION(BlueprintPure, Category="Configurator|Path Tracing")
	int32 GetTargetSamples() const { return TargetSamples; }

	UPROPERTY(BlueprintAssignable, Category="Configurator|Path Tracing")
	FPathTracingProgressChanged OnProgressChanged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Configurator|Path Tracing",
		meta=(ClampMin="1", ClampMax="8192"))
	int32 ProductSamplesPerPixel = 256;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual UWorld* GetTickableGameObjectWorld() const override;

private:
	TSharedPtr<FPathTracingExperienceViewExtension, ESPMode::ThreadSafe> ViewExtension;
	bool bPathTracingEnabled = false;
	int32 CurrentSample = 0;
	int32 TargetSamples = 0;
};
