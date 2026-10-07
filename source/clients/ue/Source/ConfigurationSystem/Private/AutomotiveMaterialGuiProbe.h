#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AutomotiveMaterialGuiProbe.generated.h"

class UAutomotiveConfigurationState;
class UAutomotiveMaterialBinder;
class UAutomotiveMaterialLibrary;
class UMaterialInterface;

struct FAutomotiveMaterialGuiProbeSurfaceResult
{
	FString SurfaceId;
	FString OptionId;
	FString MaterialVariantId;
	FString Component;
	FString Slot;
	FString Before;
	FString After;
	FString ReceiptCode;
	FString ColorPolicy;
	bool bVisible = false;
	bool bUniqueSlotHit = false;
	bool bChanged = false;
	bool bNeutralProxy = false;
};

/**
 * 真实 RHI/viewport 阶段 4 验收探针。
 * 等待展厅车辆与真实 Game viewport 初始化后，逐项切换 Catalog 的 40 个 surface，
 * 验证 Binder 的唯一可见目标、回执和材质变化，最后截取 GUI 并写 JSON 证据。
 */
UCLASS()
class UAutomotiveMaterialGuiProbe final : public UObject
{
	GENERATED_BODY()

public:
	void Start();
	void Shutdown();

private:
	void OnEngineLoopInitComplete();
	bool Tick(float DeltaTime);
	bool TryInitializeTraversal();
	bool ProcessNextSurface(FString& OutFailureReason);
	static FString DescribeMaterial(UMaterialInterface* Material);
	void RequestScreenshot();
	void WriteReportAndExit(bool bSuccess, const FString& FailureReason);

	FDelegateHandle EngineInitCompleteHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	FString OutputPath;
	FString ScreenshotPath;
	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveConfigurationState> State;
	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveMaterialBinder> Binder;
	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveMaterialLibrary> Library;
	TArray<FString> SurfaceIds;
	TArray<FAutomotiveMaterialGuiProbeSurfaceResult> SurfaceResults;
	TArray<FString> UnsupportedSurfaceIds;
	int32 SurfaceIndex = 0;
	double ElapsedSeconds = 0.0;
	double TraversalCompletedSeconds = 0.0;
	bool bTraversalInitialized = false;
	bool bTraversalComplete = false;
	bool bScreenshotRequested = false;
};
