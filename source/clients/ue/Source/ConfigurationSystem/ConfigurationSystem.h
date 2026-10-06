#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct FStreamableHandle;
class FPathTracingProbe;
class FPathTracingAlphaProbe;
class FConfigurationBatchBake;
class FPackagingBoundaryProbe;
class FVehicleHierarchyProbe;
class UConfigurationStateProbe;
class UAutomotiveMaterialGuiProbe;
class FContentPackMountService;

/** 游戏模块，同时按命令行显式启用相互独立的 Runtime 技术探针。 */
class CONFIGURATIONSYSTEM_API FConfigurationSystemModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	/** Editor 控制台与 Runtime 命令行共用的批量 Bake 入口。 */
	void StartConfigurationBatchBake(bool bExitOnComplete);

private:
	void SchedulePrimaryAssetProbe();
	bool TickPrimaryAssetProbe(float DeltaTime);
	void RunPrimaryAssetProbe();
	void FinishPrimaryAssetProbe();
	void WriteProbeReportAndExit(bool bLoadRequestCompleted);
	bool TickContentPackProbe(float DeltaTime);
	void RunContentPackProbe();

	FDelegateHandle EngineInitCompleteHandle;
	FTSTicker::FDelegateHandle ProbeTickerHandle;
	TSharedPtr<FStreamableHandle> ProbeLoadHandle;
	TArray<FPrimaryAssetId> ProbeAssetIds;
	FString ProbeOutputPath;
	TSharedPtr<FPathTracingProbe> PathTracingProbe;
	TSharedPtr<FPathTracingAlphaProbe> PathTracingAlphaProbe;
	TSharedPtr<FConfigurationBatchBake> ConfigurationBatchBake;
	TSharedPtr<FPackagingBoundaryProbe> PackagingBoundaryProbe;
	TSharedPtr<FVehicleHierarchyProbe> VehicleHierarchyProbe;
	UConfigurationStateProbe* ConfigurationStateProbe = nullptr;
	UAutomotiveMaterialGuiProbe* AutomotiveMaterialGuiProbe = nullptr;
	TUniquePtr<FContentPackMountService> ContentPackProbeService;
	FTSTicker::FDelegateHandle ContentPackProbeTickerHandle;
};
