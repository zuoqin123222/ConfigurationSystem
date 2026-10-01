#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct FStreamableHandle;
class FPathTracingProbe;
class FPathTracingAlphaProbe;

/** 游戏模块，同时按命令行显式启用相互独立的 Runtime 技术探针。 */
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
	TSharedPtr<FPathTracingProbe> PathTracingProbe;
	TSharedPtr<FPathTracingAlphaProbe> PathTracingAlphaProbe;
};
