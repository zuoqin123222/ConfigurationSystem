#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct FStreamableHandle;

/** 游戏模块，同时承载仅由 -PrimaryAssetProbe 显式启用的自动化探针。 */
class FConfigurationSystemModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void SchedulePrimaryAssetProbe();
	bool TickPrimaryAssetProbe(float DeltaTime);
	void RunPrimaryAssetProbe();
	void FinishPrimaryAssetProbe();
	void WriteProbeReportAndExit(bool bLoadRequestCompleted);

	FDelegateHandle EngineInitCompleteHandle;
	FTSTicker::FDelegateHandle ProbeTickerHandle;
	TSharedPtr<FStreamableHandle> ProbeLoadHandle;
	TArray<FPrimaryAssetId> ProbeAssetIds;
	FString ProbeOutputPath;
};
