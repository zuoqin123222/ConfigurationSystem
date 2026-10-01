#include "PathTracingProbe.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathTracingProbe, Log, All);

namespace PathTracingProbe
{
	const TCHAR* GetBuildConfigurationName()
	{
#if UE_BUILD_SHIPPING
		return TEXT("Shipping");
#elif UE_BUILD_TEST
		return TEXT("Test");
#elif UE_BUILD_DEBUG
		return TEXT("Debug");
#elif UE_BUILD_DEVELOPMENT
		return TEXT("Development");
#else
		return TEXT("Unknown");
#endif
	}

	TSharedRef<FJsonObject> ReadConsoleVariable(const TCHAR* Name)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		Json->SetBoolField(TEXT("found"), Variable != nullptr);
		Json->SetStringField(TEXT("value"), Variable != nullptr ? Variable->GetString() : FString());
		Json->SetNumberField(TEXT("flags"), Variable != nullptr ? static_cast<uint32>(Variable->GetFlags()) : 0);
		return Json;
	}
}

/**
 * SetupView 在游戏线程执行，因此探针 Tick 可直接读取最近一次公开 ViewState 快照，
 * 不需要触碰 Renderer Private 类型，也不需要跨线程同步。
 */
class FPathTracingProbeViewExtension final : public FSceneViewExtensionBase
{
public:
	FPathTracingProbeViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override
	{
#if RHI_RAYTRACING
		if (InViewFamily.EngineShowFlags.PathTracing && InView.State != nullptr)
		{
			SampleIndex = InView.State->GetPathTracingSampleIndex();
			SampleCount = InView.State->GetPathTracingSampleCount();
			bObservedPathTracingView = true;
		}
#endif
	}

	bool HasObservation() const { return bObservedPathTracingView; }
	uint32 GetSampleIndex() const { return SampleIndex; }
	uint32 GetSampleCount() const { return SampleCount; }

private:
	bool bObservedPathTracingView = false;
	uint32 SampleIndex = 0;
	uint32 SampleCount = 0;
};

void FPathTracingProbe::Start()
{
	OutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("PathTracingProbe"),
		TEXT("PathTracingProbe.json"));
	FParse::Value(FCommandLine::Get(), TEXT("PathTracingProbeOutput="), OutputPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	double ParsedTimeout = TimeoutSeconds;
	if (FParse::Value(FCommandLine::Get(), TEXT("PathTracingProbeTimeout="), ParsedTimeout))
	{
		TimeoutSeconds = FMath::Clamp(ParsedTimeout, 5.0, 120.0);
	}

	EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(
		this,
		&FPathTracingProbe::OnEngineLoopInitComplete);
}

void FPathTracingProbe::Shutdown()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	ViewExtension.Reset();
}

void FPathTracingProbe::OnEngineLoopInitComplete()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}

	StartTimeSeconds = FPlatformTime::Seconds();
	ViewExtension = FSceneViewExtensions::NewExtension<FPathTracingProbeViewExtension>();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FPathTracingProbe::Tick));
}

bool FPathTracingProbe::Tick(const float DeltaTime)
{
	(void)DeltaTime;

	if (FPlatformTime::Seconds() - StartTimeSeconds > TimeoutSeconds)
	{
		Finish(false, TEXT("等待 Path Tracing 采样或相机重置超时。"));
		return false;
	}

	if (State == EState::WaitingForViewport)
	{
		TryStartPathTracing();
		return State != EState::Finished;
	}

	if (!ViewExtension.IsValid() || !ViewExtension->HasObservation())
	{
		return true;
	}

	const uint32 CurrentSampleIndex = ViewExtension->GetSampleIndex();
	TargetSampleCount = ViewExtension->GetSampleCount();
	FinalSampleIndex = CurrentSampleIndex;

	if (State == EState::WaitingForSampleGrowth)
	{
		if (TargetSampleCount > 0 && CurrentSampleIndex >= InitialSampleIndex + 3)
		{
			bSampleGrowthObserved = true;
			SampleIndexBeforeRotation = CurrentSampleIndex;
			if (!RotateCamera())
			{
				Finish(false, TEXT("采样已增长，但没有可旋转的本地 PlayerController。"));
				return false;
			}
			State = EState::WaitingForReset;
		}
		return true;
	}

	if (State == EState::WaitingForReset)
	{
		SampleIndexAfterRotation = CurrentSampleIndex;
		if (CurrentSampleIndex < SampleIndexBeforeRotation)
		{
			bResetObserved = true;
			State = EState::WaitingForRegrowth;
		}
		return true;
	}

	if (State == EState::WaitingForRegrowth
		&& CurrentSampleIndex >= SampleIndexAfterRotation + 2)
	{
		bRegrowthObserved = true;
		SampleIndexAfterRegrowth = CurrentSampleIndex;
		if (!RestoreRealtimeMode())
		{
			Finish(false, TEXT("采样重新累积成功，但未能恢复实时 Lit 模式。"));
			return false;
		}
		Finish(true, FString());
		return false;
	}

	return true;
}

bool FPathTracingProbe::TryStartPathTracing()
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr || GEngine->GameViewport->GetWorld() == nullptr)
	{
		return false;
	}

#if RHI_RAYTRACING
	bCompiledWithRayTracing = true;
#endif
	bRHISupportsRayTracing = GRHISupportsRayTracing;
	bRHISupportsRayTracingShaders = GRHISupportsRayTracingShaders;
	bRayTracingEnabled = IsRayTracingEnabled();
	bPlatformSupportsPathTracing =
		FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(GMaxRHIShaderPlatform);

	if (!bCompiledWithRayTracing || !bRHISupportsRayTracing || !bRHISupportsRayTracingShaders
		|| !bRayTracingEnabled || !bPlatformSupportsPathTracing)
	{
		Finish(false, TEXT("当前构建、RHI、硬件或 Shader Platform 不满足 Path Tracing 条件。"));
		return true;
	}

	UGameViewportClient* GameViewport = GEngine->GameViewport;
	if (IConsoleVariable* SamplesPerPixel =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing.SamplesPerPixel")))
	{
		// 探针只需验证采样增长、重置和恢复，不需要等待产品默认的高样本数。
		SamplesPerPixel->Set(16, ECVF_SetByCode);
	}
	if (IConsoleVariable* ProgressDisplay =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing.ProgressDisplay")))
	{
		ProgressDisplay->Set(1, ECVF_SetByCode);
	}
	GameViewport->SetViewMode(VMI_PathTracing);
	bGuardedSetViewModeApplied = GameViewport->ViewModeIndex == VMI_PathTracing;

	if (!bGuardedSetViewModeApplied)
	{
		// GameViewport 的标准入口会在 Cooked Game/Shipping 中把 Debug ViewMode 回退为 Lit。
		// 这里继续验证公开 ViewModeIndex + ApplyViewMode 是否能作为项目自己的 Runtime 适配层。
		bDirectApplyViewModeAttempted = true;
		GameViewport->ViewModeIndex = VMI_PathTracing;
		ApplyViewMode(VMI_PathTracing, true, GameViewport->EngineShowFlags);
		bDirectApplyViewModeApplied =
			GameViewport->ViewModeIndex == VMI_PathTracing
			&& GameViewport->EngineShowFlags.PathTracing;
	}

	bViewModeApplied = bGuardedSetViewModeApplied || bDirectApplyViewModeApplied;
	if (!bViewModeApplied)
	{
		Finish(false, TEXT("标准 SetViewMode 被回退，公开 ApplyViewMode 适配路径也未能启用 Path Tracing。"));
		return true;
	}

	InitialSampleIndex = ViewExtension.IsValid() ? ViewExtension->GetSampleIndex() : 0;
	State = EState::WaitingForSampleGrowth;
	UE_LOG(LogPathTracingProbe, Display, TEXT("已切换到 Path Tracing，等待公开 ViewState 采样计数增长。"));
	return true;
}

bool FPathTracingProbe::RotateCamera()
{
	UWorld* World = GEngine != nullptr && GEngine->GameViewport != nullptr
		? GEngine->GameViewport->GetWorld()
		: nullptr;
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController == nullptr)
	{
		return false;
	}

	OriginalControlRotation = PlayerController->GetControlRotation();
	FRotator Rotated = OriginalControlRotation;
	Rotated.Yaw += 5.0f;
	PlayerController->SetControlRotation(Rotated);
	bCameraRotationApplied = true;
	UE_LOG(
		LogPathTracingProbe,
		Display,
		TEXT("采样增长到 %u，已将相机 Yaw 旋转 5 度以验证累积重置。"),
		SampleIndexBeforeRotation);
	return true;
}

bool FPathTracingProbe::RestoreRealtimeMode()
{
	UGameViewportClient* GameViewport = GEngine != nullptr ? GEngine->GameViewport : nullptr;
	if (GameViewport == nullptr)
	{
		return false;
	}

	GameViewport->SetViewMode(VMI_Lit);
	bRealtimeModeRestored =
		GameViewport->ViewModeIndex == VMI_Lit
		&& !GameViewport->EngineShowFlags.PathTracing;
	return bRealtimeModeRestored;
}

void FPathTracingProbe::Finish(const bool bSuccess, const FString& FailureReason)
{
	if (State == EState::Finished)
	{
		return;
	}
	State = EState::Finished;
	TickerHandle.Reset();

	TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetBoolField(TEXT("compiledWithRayTracing"), bCompiledWithRayTracing);
	Capabilities->SetBoolField(TEXT("gRHISupportsRayTracing"), bRHISupportsRayTracing);
	Capabilities->SetBoolField(TEXT("gRHISupportsRayTracingShaders"), bRHISupportsRayTracingShaders);
	Capabilities->SetBoolField(TEXT("isRayTracingEnabled"), bRayTracingEnabled);
	Capabilities->SetBoolField(TEXT("platformSupportsPathTracing"), bPlatformSupportsPathTracing);
	Capabilities->SetNumberField(TEXT("shaderPlatform"), static_cast<int32>(GMaxRHIShaderPlatform));

	TSharedRef<FJsonObject> ConsoleVariables = MakeShared<FJsonObject>();
	for (const TCHAR* Name : {
		TEXT("r.RayTracing"),
		TEXT("r.RayTracing.Enable"),
		TEXT("r.PathTracing"),
		TEXT("r.PathTracing.SamplesPerPixel"),
		TEXT("r.PathTracing.ProgressDisplay"),
		TEXT("r.SkinCache.CompileShaders") })
	{
		ConsoleVariables->SetObjectField(Name, PathTracingProbe::ReadConsoleVariable(Name));
	}

	TSharedRef<FJsonObject> Samples = MakeShared<FJsonObject>();
	Samples->SetBoolField(TEXT("publicViewStateObserved"), ViewExtension.IsValid() && ViewExtension->HasObservation());
	Samples->SetNumberField(TEXT("targetSampleCount"), TargetSampleCount);
	Samples->SetNumberField(TEXT("initialSampleIndex"), InitialSampleIndex);
	Samples->SetNumberField(TEXT("sampleIndexBeforeRotation"), SampleIndexBeforeRotation);
	Samples->SetNumberField(TEXT("sampleIndexAfterRotation"), SampleIndexAfterRotation);
	Samples->SetNumberField(TEXT("sampleIndexAfterRegrowth"), SampleIndexAfterRegrowth);
	Samples->SetNumberField(TEXT("finalSampleIndex"), FinalSampleIndex);
	Samples->SetBoolField(TEXT("growthObserved"), bSampleGrowthObserved);
	Samples->SetBoolField(TEXT("cameraRotationApplied"), bCameraRotationApplied);
	Samples->SetBoolField(TEXT("resetObserved"), bResetObserved);
	Samples->SetBoolField(TEXT("regrowthObserved"), bRegrowthObserved);
	Samples->SetBoolField(TEXT("realtimeModeRestored"), bRealtimeModeRestored);
	Samples->SetBoolField(
		TEXT("exactProgressApiAvailable"),
		ViewExtension.IsValid() && ViewExtension->HasObservation() && TargetSampleCount > 0);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("probe"), TEXT("RuntimePathTracing"));
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("buildConfiguration"), PathTracingProbe::GetBuildConfigurationName());
	Root->SetBoolField(TEXT("withEditor"), WITH_EDITOR != 0);
	Root->SetStringField(TEXT("graphicsRHI"), FApp::GetGraphicsRHI());
	Root->SetStringField(TEXT("gpuAdapterName"), GRHIAdapterName);
	Root->SetBoolField(TEXT("viewModeRequested"), true);
	Root->SetBoolField(TEXT("guardedSetViewModeApplied"), bGuardedSetViewModeApplied);
	Root->SetBoolField(TEXT("directApplyViewModeAttempted"), bDirectApplyViewModeAttempted);
	Root->SetBoolField(TEXT("directApplyViewModeApplied"), bDirectApplyViewModeApplied);
	Root->SetBoolField(TEXT("viewModeApplied"), bViewModeApplied);
	Root->SetObjectField(TEXT("capabilities"), Capabilities);
	Root->SetObjectField(TEXT("consoleVariables"), ConsoleVariables);
	Root->SetObjectField(TEXT("samples"), Samples);
	Root->SetBoolField(TEXT("passed"), bSuccess);
	Root->SetStringField(TEXT("failureReason"), FailureReason);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bSerialized = FJsonSerializer::Serialize(Root, Writer);
	const bool bDirectoryReady = IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	const bool bWritten = bSerialized && bDirectoryReady
		&& FFileHelper::SaveStringToFile(
			JsonText,
			*OutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bWritten)
	{
		UE_LOG(LogPathTracingProbe, Display, TEXT("Path Tracing 探针 JSON 已写入：%s"), *OutputPath);
	}
	else
	{
		UE_LOG(LogPathTracingProbe, Error, TEXT("无法写入 Path Tracing 探针 JSON：%s"), *OutputPath);
	}

	FPlatformMisc::RequestExitWithStatus(false, bWritten && bSuccess ? 0 : 2);
}
