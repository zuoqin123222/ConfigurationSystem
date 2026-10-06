#pragma once

#include "AutomotiveConfigurationState.h"
#include "CarConfigurationState.h"
#include "Containers/Ticker.h"
#include "CoreMinimal.h"

class AActor;
class ACameraActor;
class AConfiguratorVehicleActor;
class FConfigurationBatchBakeViewExtension;
class FViewport;

struct CONFIGURATIONSYSTEM_API FConfigurationBakeCamera
{
	FString RenderViewId;
	FName ActorTag;
	FTransform Transform;
};

struct CONFIGURATIONSYSTEM_API FConfigurationBakeTask
{
	FString ConfigurationKey;
	TMap<FString, FString> Selections;
	TMap<FString, FAutomotiveCustomization> Customizations;
	/** v1 兼容映射；v2 始终通过动态 selections/customizations 执行。 */
	FCarConfigurationSelection Selection;
	FString RenderViewId;
	FString RelativePath;
};

struct CONFIGURATIONSYSTEM_API FConfigurationBakeOutputSettings
{
	FString Profile;
	int32 Width = 1022;
	int32 Height = 664;
	int32 SamplesPerPixel = 64;
	bool bPathTracing = true;
	bool bDenoiser = true;

	static bool Resolve(
		const FString& RequestedProfile,
		bool bShippingBuild,
		FConfigurationBakeOutputSettings& OutSettings,
		FString& OutError);
	bool HasDesktopStageAspectRatio() const;
};

enum class EConfigurationBakeShaderWaitResult : uint8
{
	NotCompiling,
	Waiting,
	Completed,
	TimedOut
};

struct CONFIGURATIONSYSTEM_API FConfigurationBakeShaderWaitTracker
{
	EConfigurationBakeShaderWaitResult Update(
		bool bIsCompiling,
		double NowSeconds,
		double TimeoutSeconds);

	void Reset();

private:
	double StartedAt = 0.0;
	bool bWasCompiling = false;
};

struct CONFIGURATIONSYSTEM_API FConfigurationBakePlan
{
	FString SchemaVersion;
	FString PublicationVersion;
	FString CatalogVersion;
	FString VehicleId;
	int32 ExpectedRenderCount = 0;
	TArray<FString> RenderViewIds;
	TArray<FConfigurationBakeCamera> Cameras;
	TArray<FConfigurationBakeTask> Tasks;

	static bool Load(const FString& JsonPath, FConfigurationBakePlan& OutPlan, TArray<FString>& OutErrors);
	bool Validate(TArray<FString>& OutErrors) const;
};

/**
 * 顺序执行 published configurations 中全部配置 x RenderView 的 Runtime Bake。
 * Editor 控制台和 -ConfigurationBatchBake 命令行入口共用该实现。
 */
class CONFIGURATIONSYSTEM_API FConfigurationBatchBake final
{
public:
	static bool ComputeFileSha256(
		const FString& Filename,
		FString& OutSha256,
		FString& OutError);

	void Start(bool bInExitOnComplete);
	void Shutdown();
	bool IsRunning() const;

private:
	enum class EState : uint8
	{
		WaitingForViewport,
		PreparingTask,
		WaitingForReset,
		WaitingForSamples,
		WaitingForRealtimeFrames,
		Finished
	};

	void OnEngineLoopInitComplete();
	bool Tick(float DeltaTime);
	void OnViewportRendered(FViewport* Viewport);
	bool SetupScene(FString& OutError);
	void PrepareCurrentTask();
	bool CaptureCurrentTask(FViewport& Viewport, FString& OutError);
	void CompleteCurrentTask(bool bSuccess, const FString& Error);
	void Finish(const FString& FatalError);
	bool WriteManifest(const FString& FatalError);

	FDelegateHandle EngineInitCompleteHandle;
	FDelegateHandle ViewportRenderedHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TSharedPtr<FConfigurationBatchBakeViewExtension, ESPMode::ThreadSafe> ViewExtension;
	EState State = EState::WaitingForViewport;
	FConfigurationBakePlan Plan;
	FString InputPath;
	FString StagingDirectory;
	TArray<TSharedPtr<class FJsonValue>> RenderResults;
	TArray<TWeakObjectPtr<AActor>> SpawnedActors;
	TArray<TWeakObjectPtr<AActor>> HiddenActorsToRestore;
	TMap<FString, TWeakObjectPtr<ACameraActor>> CamerasByViewId;
	TWeakObjectPtr<AConfiguratorVehicleActor> Vehicle;
	int32 CurrentTaskIndex = 0;
	uint32 SamplesPerPixel = 16;
	int32 OutputWidth = 1022;
	int32 OutputHeight = 664;
	int32 RealtimeFramesBeforeCapture = 8;
	int32 RealtimeFramesRendered = 0;
	double RunStartedAt = 0.0;
	double TaskStartedAt = 0.0;
	double ConfigurationChangedAt = 0.0;
	double TaskTimeoutSeconds = 120.0;
	FConfigurationBakeShaderWaitTracker ShaderWaitTracker;
	bool bObservedReset = false;
	bool bCapturePending = false;
	bool bExitOnComplete = false;
	bool bUsePathTracing = true;
	bool bUseDenoiser = true;
};

/** Editor 模块无需依赖 Runtime 模块私有头即可启动并持有一次 Bake。 */
CONFIGURATIONSYSTEM_API void StartConfigurationBatchBakeFromEditor();
