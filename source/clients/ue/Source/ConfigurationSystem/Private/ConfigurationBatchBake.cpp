#include "ConfigurationBatchBake.h"

#include "Algo/AnyOf.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "ContentPackMountService.h"
#include "ConfiguratorVehicleActor.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogConfigurationBatchBake, Log, All);

namespace ConfigurationBatchBake
{
	constexpr uint8 TransparentThreshold = 1;
	const TCHAR* RequiredViews[] = {TEXT("front"), TEXT("front-left"), TEXT("side"), TEXT("rear-right")};

	void SetIntCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	TArray<FConfigurationBakeCamera> MakeCameras()
	{
		const FVector Locations[] = {
			FVector(900.0, 0.0, 260.0),
			FVector(720.0, -720.0, 280.0),
			FVector(0.0, -980.0, 250.0),
			FVector(-720.0, 720.0, 280.0)
		};
		TArray<FConfigurationBakeCamera> Result;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(RequiredViews); ++Index)
		{
			FConfigurationBakeCamera& Camera = Result.AddDefaulted_GetRef();
			Camera.RenderViewId = RequiredViews[Index];
			Camera.ActorTag = FName(*FString::Printf(TEXT("RenderView.%s"), RequiredViews[Index]));
			Camera.Transform = FTransform(
				(FVector(0.0, 0.0, 105.0) - Locations[Index]).Rotation(),
				Locations[Index]);
		}
		return Result;
	}

	bool ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FString& Out, TArray<FString>& Errors)
	{
		if (!Object.IsValid() || !Object->TryGetStringField(Field, Out) || Out.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("字段 %s 缺失或为空。"), Field));
			return false;
		}
		return true;
	}

}

class FConfigurationBatchBakeViewExtension final : public FSceneViewExtensionBase
{
public:
	FConfigurationBatchBakeViewExtension(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) {}
	virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override
	{
#if RHI_RAYTRACING
		if (Family.EngineShowFlags.PathTracing && View.State != nullptr)
		{
			SampleIndex = View.State->GetPathTracingSampleIndex();
			bSeen = true;
		}
#endif
	}
	bool HasSeen() const { return bSeen; }
	uint32 GetSampleIndex() const { return SampleIndex; }
private:
	bool bSeen = false;
	uint32 SampleIndex = 0;
};

bool FConfigurationBakePlan::Load(const FString& JsonPath, FConfigurationBakePlan& OutPlan, TArray<FString>& OutErrors)
{
	OutPlan = FConfigurationBakePlan();
	OutErrors.Reset();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *JsonPath))
	{
		OutErrors.Add(FString::Printf(TEXT("无法读取 published configurations：%s"), *JsonPath));
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("published configurations 不是有效 JSON。"));
		return false;
	}
	using namespace ConfigurationBatchBake;
	ReadString(Root, TEXT("schemaVersion"), OutPlan.SchemaVersion, OutErrors);
	ReadString(Root, TEXT("publicationVersion"), OutPlan.PublicationVersion, OutErrors);
	ReadString(Root, TEXT("catalogVersion"), OutPlan.CatalogVersion, OutErrors);
	ReadString(Root, TEXT("vehicleId"), OutPlan.VehicleId, OutErrors);
	OutPlan.ExpectedRenderCount = Root->GetIntegerField(TEXT("expectedRenderCount"));
	const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
	if (Root->TryGetArrayField(TEXT("renderViewIds"), Views))
	{
		for (const TSharedPtr<FJsonValue>& View : *Views) { OutPlan.RenderViewIds.Add(View->AsString()); }
	}
	const TArray<TSharedPtr<FJsonValue>>* Configurations = nullptr;
	if (!Root->TryGetArrayField(TEXT("configurations"), Configurations))
	{
		OutErrors.Add(TEXT("configurations 数组缺失。"));
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Configurations)
	{
		const TSharedPtr<FJsonObject> Config = Value->AsObject();
		FString Key;
		if (!ReadString(Config, TEXT("configurationKey"), Key, OutErrors)) { continue; }
		const TSharedPtr<FJsonObject>* Selections = nullptr;
		if (!Config->TryGetObjectField(TEXT("selections"), Selections)) { OutErrors.Add(Key + TEXT(" 缺少 selections。")); continue; }
		FCarConfigurationSelection Selection;
		ReadString(*Selections, TEXT("paint"), Selection.Paint, OutErrors);
		ReadString(*Selections, TEXT("wheel"), Selection.Wheel, OutErrors);
		ReadString(*Selections, TEXT("interior"), Selection.Interior, OutErrors);
		ReadString(*Selections, TEXT("frame"), Selection.Frame, OutErrors);
		for (const FString& ViewId : OutPlan.RenderViewIds)
		{
			FConfigurationBakeTask& Task = OutPlan.Tasks.AddDefaulted_GetRef();
			Task.ConfigurationKey = Key;
			Task.Selection = Selection;
			Task.RenderViewId = ViewId;
			Task.RelativePath = FString::Printf(
				TEXT("renders/%s/%s/%s/%s.png"),
				*OutPlan.PublicationVersion, *OutPlan.VehicleId, *Key, *ViewId);
		}
	}
	OutPlan.Cameras = MakeCameras();
	return OutErrors.IsEmpty() && OutPlan.Validate(OutErrors);
}

bool FConfigurationBakePlan::Validate(TArray<FString>& OutErrors) const
{
	using namespace ConfigurationBatchBake;
	if (SchemaVersion != TEXT("1.0.0")) { OutErrors.Add(TEXT("schemaVersion 必须为 1.0.0。")); }
	if (RenderViewIds.Num() != 4) { OutErrors.Add(TEXT("必须恰好声明四个 RenderView。")); }
	TSet<FString> Required; for (const TCHAR* View : RequiredViews) { Required.Add(View); }
	TSet<FString> Actual; for (const FString& View : RenderViewIds) { Actual.Add(View); }
	bool bViewsMatch=Actual.Num()==Required.Num();
	for (const FString& View : Required) { bViewsMatch&=Actual.Contains(View); }
	if (!bViewsMatch) { OutErrors.Add(TEXT("RenderView 必须为 front/front-left/side/rear-right 且不重复。")); }
	if (Tasks.Num() != 64 || ExpectedRenderCount != 64) { OutErrors.Add(TEXT("Bake 计划必须恰好包含 64 项。")); }
	TSet<FString> TaskKeys;
	for (const FConfigurationBakeTask& Task : Tasks) { TaskKeys.Add(Task.ConfigurationKey + TEXT("|") + Task.RenderViewId); }
	if (TaskKeys.Num() != Tasks.Num()) { OutErrors.Add(TEXT("配置与 RenderView 任务组合不唯一。")); }
	TSet<FString> CameraViews; TSet<FName> CameraTags; TSet<FString> CameraTransforms;
	for (const FConfigurationBakeCamera& Camera : Cameras)
	{
		CameraViews.Add(Camera.RenderViewId); CameraTags.Add(Camera.ActorTag);
		CameraTransforms.Add(Camera.Transform.ToHumanReadableString());
	}
	if (Cameras.Num()!=4 || CameraViews.Num()!=4 || CameraTags.Num()!=4 || CameraTransforms.Num()!=4) { OutErrors.Add(TEXT("四个 RenderView 必须分别对应唯一标签与唯一相机 Transform。")); }
	return OutErrors.IsEmpty();
}

bool FConfigurationBatchBake::ComputeFileSha256(
	const FString& Filename,
	FString& OutSha256,
	FString& OutError)
{
	return FContentPackMountService::ComputeFileSha256(Filename, OutSha256, OutError);
}

void FConfigurationBatchBake::Start(bool bInExitOnComplete)
{
	if (IsRunning()) { return; }
	bExitOnComplete = bInExitOnComplete;
	InputPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../../contracts/fixtures/published-configurations.mvp.json")));
	StagingDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../../staging")));
	FParse::Value(FCommandLine::Get(), TEXT("ConfigurationBakeInput="), InputPath);
	FParse::Value(FCommandLine::Get(), TEXT("ConfigurationBakeStaging="), StagingDirectory);
	int32 ParsedSamples = SamplesPerPixel;
	if (FParse::Value(FCommandLine::Get(), TEXT("ConfigurationBakeSamples="), ParsedSamples)) { SamplesPerPixel = FMath::Clamp(ParsedSamples, 1, 4096); }
	FParse::Value(FCommandLine::Get(), TEXT("ConfigurationBakeTaskTimeout="), TaskTimeoutSeconds);
	TArray<FString> Errors;
	if (!FConfigurationBakePlan::Load(InputPath, Plan, Errors))
	{
		Finish(FString::Join(Errors, TEXT(" ")));
		return;
	}
	RunStartedAt = FPlatformTime::Seconds();
	if (GEngine != nullptr) { OnEngineLoopInitComplete(); }
	else { EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FConfigurationBatchBake::OnEngineLoopInitComplete); }
}

void FConfigurationBatchBake::Shutdown()
{
	if (EngineInitCompleteHandle.IsValid()) { FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle); EngineInitCompleteHandle.Reset(); }
	if (TickerHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle); TickerHandle.Reset(); }
	if (ViewportRenderedHandle.IsValid()) { UGameViewportClient::OnViewportRendered().Remove(ViewportRenderedHandle); ViewportRenderedHandle.Reset(); }
	ViewExtension.Reset();
}

bool FConfigurationBatchBake::IsRunning() const { return State != EState::Finished && (TickerHandle.IsValid() || EngineInitCompleteHandle.IsValid()); }

void FConfigurationBatchBake::OnEngineLoopInitComplete()
{
	if (EngineInitCompleteHandle.IsValid()) { FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle); EngineInitCompleteHandle.Reset(); }
	ViewExtension = FSceneViewExtensions::NewExtension<FConfigurationBatchBakeViewExtension>();
	ViewportRenderedHandle = UGameViewportClient::OnViewportRendered().AddRaw(this, &FConfigurationBatchBake::OnViewportRendered);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FConfigurationBatchBake::Tick));
}

bool FConfigurationBatchBake::Tick(float DeltaTime)
{
	(void)DeltaTime;
	if (State == EState::WaitingForViewport)
	{
		FString Error;
		if (SetupScene(Error)) { State = EState::PreparingTask; }
		else if (!Error.IsEmpty()) { Finish(Error); return false; }
		return true;
	}
	if (State == EState::PreparingTask) { PrepareCurrentTask(); return true; }
	if (FPlatformTime::Seconds() - TaskStartedAt > TaskTimeoutSeconds)
	{
		CompleteCurrentTask(false, TEXT("等待 Path Tracing 样本超时。"));
		return State != EState::Finished;
	}
	if (!ViewExtension.IsValid() || !ViewExtension->HasSeen()) { return true; }
	const uint32 Index = ViewExtension->GetSampleIndex();
	if (State == EState::WaitingForReset)
	{
		if (Index < SamplesPerPixel) { bObservedReset = true; State = EState::WaitingForSamples; }
		return true;
	}
	if (State == EState::WaitingForSamples && bObservedReset && Index >= SamplesPerPixel) { bCapturePending = true; }
	return true;
}

bool FConfigurationBatchBake::SetupScene(FString& OutError)
{
	if (GEngine==nullptr || GEngine->GameViewport==nullptr || GEngine->GameViewport->Viewport==nullptr || GEngine->GameViewport->GetWorld()==nullptr) { return false; }
#if !RHI_RAYTRACING
	OutError = TEXT("当前构建未包含 RHI_RAYTRACING。"); return false;
#else
	if (!GRHISupportsRayTracing || !GRHISupportsRayTracingShaders || !IsRayTracingEnabled() || !FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(GMaxRHIShaderPlatform)) { OutError=TEXT("当前 RHI/硬件不支持 Path Tracing。"); return false; }
#endif
	UWorld* World=GEngine->GameViewport->GetWorld(); APlayerController* PC=World->GetFirstPlayerController();
	if (PC==nullptr) { OutError=TEXT("无法获得 PlayerController。"); return false; }
	AConfiguratorVehicleActor* SpawnedVehicle=World->SpawnActor<AConfiguratorVehicleActor>(FVector::ZeroVector,FRotator::ZeroRotator);
	if (SpawnedVehicle==nullptr) { OutError=TEXT("无法生成占位车辆。"); return false; }
	Vehicle=SpawnedVehicle; SpawnedActors.Add(SpawnedVehicle);
	for (const FConfigurationBakeCamera& Definition : Plan.Cameras)
	{
		ACameraActor* Camera=World->SpawnActor<ACameraActor>(Definition.Transform.GetLocation(),Definition.Transform.Rotator());
		if (Camera==nullptr) { OutError=TEXT("无法生成四个 Bake 相机。"); return false; }
		Camera->Tags.Add(Definition.ActorTag); Camera->GetCameraComponent()->SetFieldOfView(42.0f); Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		CamerasByViewId.Add(Definition.RenderViewId,Camera); SpawnedActors.Add(Camera);
	}
	APointLight* Light=World->SpawnActor<APointLight>(FVector(250.0,-350.0,600.0),FRotator::ZeroRotator);
	if (Light==nullptr) { OutError=TEXT("无法生成 Bake 灯光。"); return false; }
	Light->PointLightComponent->SetIntensity(1000000.0f); Light->PointLightComponent->SetAttenuationRadius(2200.0f); SpawnedActors.Add(Light);
	ConfigurationBatchBake::SetIntCVar(TEXT("r.PathTracing.SamplesPerPixel"),SamplesPerPixel);
	ConfigurationBatchBake::SetIntCVar(TEXT("r.PathTracing.ProgressDisplay"),0); ConfigurationBatchBake::SetIntCVar(TEXT("r.PathTracing.BackgroundAlpha"),0); ConfigurationBatchBake::SetIntCVar(TEXT("r.PathTracing.Denoiser"),0);
	GAreScreenMessagesEnabled=false; GEngine->bEnableOnScreenDebugMessages=false; GEngine->bEnableOnScreenDebugMessagesDisplay=false;
	GEngine->GameViewport->SetViewMode(VMI_PathTracing);
	if (GEngine->GameViewport->ViewModeIndex!=VMI_PathTracing) { GEngine->GameViewport->ViewModeIndex=VMI_PathTracing; ApplyViewMode(VMI_PathTracing,true,GEngine->GameViewport->EngineShowFlags); }
	if (!GEngine->GameViewport->EngineShowFlags.PathTracing) { OutError=TEXT("无法启用 Path Tracing ViewMode。"); return false; }
	return true;
}

void FConfigurationBatchBake::PrepareCurrentTask()
{
	if (!Plan.Tasks.IsValidIndex(CurrentTaskIndex)) { Finish(FString()); return; }
	const FConfigurationBakeTask& Task=Plan.Tasks[CurrentTaskIndex];
	ACameraActor* Camera=CamerasByViewId.FindRef(Task.RenderViewId).Get();
	if (!Vehicle.IsValid() || Camera==nullptr) { CompleteCurrentTask(false,TEXT("车辆或 RenderView 相机失效。")); return; }
	Vehicle->ApplyConfiguration(Task.Selection);
	GEngine->GameViewport->GetWorld()->GetFirstPlayerController()->SetViewTarget(Camera);
	ConfigurationBatchBake::SetIntCVar(TEXT("r.PathTracing.SamplesPerPixel"),SamplesPerPixel+1);
	ConfigurationChangedAt=FPlatformTime::Seconds(); TaskStartedAt=ConfigurationChangedAt; bObservedReset=false; bCapturePending=false;
	ConfigurationBatchBake::SetIntCVar(TEXT("r.PathTracing.SamplesPerPixel"),SamplesPerPixel);
	State=EState::WaitingForReset;
}

void FConfigurationBatchBake::OnViewportRendered(FViewport* Viewport)
{
	if (!bCapturePending || Viewport==nullptr || GEngine==nullptr || GEngine->GameViewport==nullptr || Viewport!=GEngine->GameViewport->Viewport) { return; }
	bCapturePending=false; FString Error; CompleteCurrentTask(CaptureCurrentTask(*Viewport,Error),Error);
}

bool FConfigurationBatchBake::CaptureCurrentTask(FViewport& Viewport, FString& OutError)
{
	const FIntPoint Size=Viewport.GetSizeXY(); TArray<FColor> Pixels;
	if (Size.X<=0 || Size.Y<=0 || !Viewport.ReadPixels(Pixels) || Pixels.Num()!=static_cast<int64>(Size.X)*Size.Y) { OutError=TEXT("Viewport BGRA8 回读失败。"); return false; }
	const uint32 CornerSum=Pixels[0].A+Pixels[Size.X-1].A+Pixels[(Size.Y-1)*Size.X].A+Pixels.Last().A;
	const bool bInverted=(CornerSum/4)>127; int64 GlowRecovered=0; bool bNormalizationRequired=bInverted;
	for (FColor& Pixel : Pixels)
	{
		const uint8 Coverage=bInverted ? 255-Pixel.A : Pixel.A; const uint8 MaxRgb=FMath::Max3(Pixel.R,Pixel.G,Pixel.B);
		if (Coverage<=ConfigurationBatchBake::TransparentThreshold && MaxRgb>ConfigurationBatchBake::TransparentThreshold)
		{
			Pixel.R=static_cast<uint8>((static_cast<uint32>(Pixel.R)*255+MaxRgb/2)/MaxRgb); Pixel.G=static_cast<uint8>((static_cast<uint32>(Pixel.G)*255+MaxRgb/2)/MaxRgb); Pixel.B=static_cast<uint8>((static_cast<uint32>(Pixel.B)*255+MaxRgb/2)/MaxRgb); Pixel.A=MaxRgb; ++GlowRecovered; bNormalizationRequired=true;
		}
		else { Pixel.A=Coverage; if (Coverage<=ConfigurationBatchBake::TransparentThreshold) { bNormalizationRequired|=MaxRgb>ConfigurationBatchBake::TransparentThreshold; Pixel.R=Pixel.G=Pixel.B=0; } }
	}
	const FConfigurationBakeTask& Task=Plan.Tasks[CurrentTaskIndex];
	const FString AbsolutePath=FPaths::Combine(StagingDirectory,Task.RelativePath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath),true);
	if (!FImageUtils::SaveImageByExtension(*AbsolutePath,FImageView(Pixels.GetData(),Size.X,Size.Y,EGammaSpace::sRGB))) { OutError=TEXT("写入 straight-alpha PNG 失败。"); return false; }
	FString Hash;
	FString HashError;
	if (!ComputeFileSha256(AbsolutePath, Hash, HashError))
	{
		OutError = FString::Printf(TEXT("计算 PNG SHA-256 失败：%s"), *HashError);
		return false;
	}
	TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("configurationKey"),Task.ConfigurationKey); Json->SetStringField(TEXT("renderViewId"),Task.RenderViewId); Json->SetStringField(TEXT("path"),Task.RelativePath.Replace(TEXT("\\"),TEXT("/")));
	Json->SetNumberField(TEXT("width"),Size.X); Json->SetNumberField(TEXT("height"),Size.Y); Json->SetStringField(TEXT("format"),TEXT("png")); Json->SetStringField(TEXT("colorSpace"),TEXT("sRGB")); Json->SetStringField(TEXT("alphaMode"),TEXT("straight"));
	Json->SetBoolField(TEXT("coverageInverted"),bInverted); Json->SetBoolField(TEXT("normalizationRequired"),bNormalizationRequired); Json->SetNumberField(TEXT("glowRecoveredPixels"),static_cast<double>(GlowRecovered)); Json->SetStringField(TEXT("sha256"),Hash); Json->SetStringField(TEXT("status"),TEXT("ready"));
	RenderResults.Add(MakeShared<FJsonValueObject>(Json));
	return true;
}

void FConfigurationBatchBake::CompleteCurrentTask(bool bSuccess, const FString& Error)
{
	if (!bSuccess)
	{
		const FConfigurationBakeTask& Task=Plan.Tasks[CurrentTaskIndex]; TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("configurationKey"),Task.ConfigurationKey); Json->SetStringField(TEXT("renderViewId"),Task.RenderViewId); Json->SetStringField(TEXT("path"),Task.RelativePath.Replace(TEXT("\\"),TEXT("/"))); Json->SetStringField(TEXT("status"),TEXT("failed")); Json->SetStringField(TEXT("error"),Error.IsEmpty()?TEXT("未知 Bake 错误。"):Error);
		RenderResults.Add(MakeShared<FJsonValueObject>(Json));
	}
	++CurrentTaskIndex; State=CurrentTaskIndex<Plan.Tasks.Num()?EState::PreparingTask:EState::Finished;
	if (State==EState::Finished) { Finish(FString()); }
}

bool FConfigurationBatchBake::WriteManifest(const FString& FatalError)
{
	while (RenderResults.Num()<Plan.Tasks.Num())
	{
		const FConfigurationBakeTask& Task=Plan.Tasks[RenderResults.Num()]; TSharedRef<FJsonObject> Item=MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("configurationKey"),Task.ConfigurationKey); Item->SetStringField(TEXT("renderViewId"),Task.RenderViewId); Item->SetStringField(TEXT("path"),Task.RelativePath); Item->SetStringField(TEXT("status"),TEXT("failed")); Item->SetStringField(TEXT("error"),FatalError.IsEmpty()?TEXT("Bake 未执行。"):FatalError); RenderResults.Add(MakeShared<FJsonValueObject>(Item));
	}
	TSharedRef<FJsonObject> Renderer=MakeShared<FJsonObject>(); Renderer->SetStringField(TEXT("engineVersion"),FEngineVersion::Current().ToString()); Renderer->SetStringField(TEXT("mode"),TEXT("path-tracing")); Renderer->SetNumberField(TEXT("samplesPerPixel"),SamplesPerPixel);
	TSharedRef<FJsonObject> Alpha=MakeShared<FJsonObject>(); Alpha->SetStringField(TEXT("alphaMode"),TEXT("straight")); Alpha->SetBoolField(TEXT("autoDetectCoverageInversion"),true); Alpha->SetBoolField(TEXT("clearTransparentRgb"),true); Alpha->SetStringField(TEXT("glowPolicy"),TEXT("synthetic-alpha"));
	TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>(); Root->SetStringField(TEXT("schemaVersion"),TEXT("1.0.0")); Root->SetStringField(TEXT("manifestVersion"),Plan.PublicationVersion); Root->SetStringField(TEXT("catalogVersion"),Plan.CatalogVersion); Root->SetStringField(TEXT("publicationVersion"),Plan.PublicationVersion); Root->SetStringField(TEXT("vehicleId"),Plan.VehicleId); Root->SetStringField(TEXT("generatedAt"),FDateTime::UtcNow().ToIso8601()); Root->SetObjectField(TEXT("renderer"),Renderer); Root->SetObjectField(TEXT("alphaProcessing"),Alpha); Root->SetArrayField(TEXT("renders"),RenderResults);
	FString Text; const bool bSerialized=FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text)); IFileManager::Get().MakeDirectory(*StagingDirectory,true);
	return bSerialized && FFileHelper::SaveStringToFile(Text,*FPaths::Combine(StagingDirectory,TEXT("bake-manifest.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FConfigurationBatchBake::Finish(const FString& FatalError)
{
	if (State==EState::Finished && RenderResults.Num()==Plan.Tasks.Num() && !TickerHandle.IsValid()) { return; }
	State=EState::Finished; Shutdown(); const bool bWritten=WriteManifest(FatalError);
	const bool bAllReady=FatalError.IsEmpty() && RenderResults.Num()==64 && !Algo::AnyOf(RenderResults,[](const TSharedPtr<FJsonValue>& V){return V->AsObject()->GetStringField(TEXT("status"))!=TEXT("ready");});
	if (bWritten && bAllReady)
	{
		UE_LOG(LogConfigurationBatchBake,Display,TEXT("批量 Bake 完成，manifest：%s"),*FPaths::Combine(StagingDirectory,TEXT("bake-manifest.json")));
	}
	else
	{
		UE_LOG(LogConfigurationBatchBake,Error,TEXT("批量 Bake 失败，manifest：%s"),*FPaths::Combine(StagingDirectory,TEXT("bake-manifest.json")));
	}
	if (bExitOnComplete) { FPlatformMisc::RequestExitWithStatus(false,bWritten&&bAllReady?0:10); }
}

void StartConfigurationBatchBakeFromEditor()
{
	static TSharedPtr<FConfigurationBatchBake> EditorBake;
	if (EditorBake.IsValid() && EditorBake->IsRunning())
	{
		UE_LOG(LogConfigurationBatchBake, Warning, TEXT("批量 Bake 已在运行，忽略重复启动。"));
		return;
	}
	EditorBake = MakeShared<FConfigurationBatchBake>();
	EditorBake->Start(false);
}
