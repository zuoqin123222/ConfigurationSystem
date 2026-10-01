#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"

struct FStreamableHandle;
class UWorld;

/** 在 -game 或打包程序中验证地图、模块边界与 Primary Asset Cook/加载链路。 */
class FPackagingBoundaryProbe final
{
public:
	void Start();
	void Shutdown();

private:
	bool Tick(float DeltaTime);
	void BeginAssetLoad(UWorld& World);
	void FinishAssetLoad();
	void WriteReportAndExit(bool bLoadRequestCompleted, const FString& FailureReason = FString());

	FTSTicker::FDelegateHandle TickerHandle;
	TSharedPtr<FStreamableHandle> LoadHandle;
	TArray<FPrimaryAssetId> AssetIds;
	FString OutputPath;
	FString LoadedMap;
	double StartSeconds = 0.0;
	int32 MarkerCount = 0;
	bool bMarkerValid = false;
	bool bCubeHardReferenceValid = false;
	bool bRequiresCookedData = false;
	bool bEditorOnlyDataFilteredFromMap = false;
	bool bEditorModuleExists = false;
	bool bEditorModuleLoaded = false;
};
