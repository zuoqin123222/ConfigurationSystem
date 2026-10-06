#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AutomotiveMaterialGuiProbe.generated.h"

/**
 * 真实 RHI/viewport 阶段 4 验收探针。
 * 等待展厅、车辆与 CEF 初始化后应用车漆和 variant 事务，截取 GUI 并写 JSON 证据。
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
	bool TryApplyTransaction();
	void RequestScreenshot();
	void WriteReportAndExit(bool bSuccess, const FString& FailureReason);

	FDelegateHandle EngineInitCompleteHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	FString OutputPath;
	FString ScreenshotPath;
	FString ReceiptJson;
	FString PaintComponentName;
	FString InteriorComponentName;
	FString PaintMaterialName;
	FString InteriorMaterialName;
	double ElapsedSeconds = 0.0;
	bool bTransactionApplied = false;
	bool bScreenshotRequested = false;
};
