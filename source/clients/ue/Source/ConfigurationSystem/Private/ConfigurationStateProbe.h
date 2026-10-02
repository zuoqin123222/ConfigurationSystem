#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConfigurationStateProbe.generated.h"

class UCarConfigurationState;

/** 由 -ConfigurationStateProbe 启用的配置状态 Runtime 自动探针。 */
UCLASS()
class UConfigurationStateProbe final : public UObject
{
	GENERATED_BODY()

public:
	void Start();
	void Shutdown();

private:
	void OnEngineLoopInitComplete();
	bool Tick(float DeltaTime);
	void Run();
	void WriteReportAndExit(bool bSuccess, const FString& FailureReason);

	UFUNCTION()
	void HandleStateChanged();

	FDelegateHandle EngineInitCompleteHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TObjectPtr<UCarConfigurationState> State;
	FString OutputPath;
	int32 EventCount = 0;
	int32 ExpectedEventCount = 0;
	int32 UniqueKeyCount = 0;
	bool bInitializationPassed = false;
	bool bSixteenUniqueKeysPassed = false;
	bool bPricesPassed = false;
	bool bTemplatesPassed = false;
	bool bInvalidInputsPassed = false;
	bool bEventCountPassed = false;
	TArray<FString> CanonicalKeys;
	TArray<int64> ActualPrices;
	TArray<int64> ExpectedPrices;
};
