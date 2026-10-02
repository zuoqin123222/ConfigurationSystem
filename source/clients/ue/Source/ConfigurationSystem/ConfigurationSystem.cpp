#include "ConfigurationSystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ConfigurationBatchBake.h"
#include "ContentPackMountService.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PackagingBoundaryProbe.h"
#include "PathTracingAlphaProbe.h"
#include "PathTracingProbe.h"
#include "PrimaryAssetProbeData.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "VehicleHierarchyProbe.h"
#include "ConfigurationStateProbe.h"

DEFINE_LOG_CATEGORY_STATIC(LogPrimaryAssetProbe, Log, All);

namespace PrimaryAssetProbe
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
}

IMPLEMENT_PRIMARY_GAME_MODULE(
	FConfigurationSystemModule,
	ConfigurationSystem,
	"ConfigurationSystem"
);

void FConfigurationSystemModule::StartupModule()
{
	FDefaultGameModuleImpl::StartupModule();

	if (FParse::Param(FCommandLine::Get(), TEXT("ConfigurationStateProbe")))
	{
		// 配置状态探针独占进程，完成纯数据检查并写出 JSON 后主动退出。
		ConfigurationStateProbe = NewObject<UConfigurationStateProbe>();
		ConfigurationStateProbe->Start();
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("VehicleHierarchyProbe")))
	{
		// P0-4 层级/执行器探针独占本次进程，并在写出 JSON 后主动退出。
		VehicleHierarchyProbe = MakeShared<FVehicleHierarchyProbe>();
		VehicleHierarchyProbe->Start();
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PackagingBoundaryProbe")))
	{
		// P0-5 探针独占进程，完成地图、Cook 和模块边界检查后写 JSON 退出。
		PackagingBoundaryProbe = MakeShared<FPackagingBoundaryProbe>();
		PackagingBoundaryProbe->Start();
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PathTracingAlphaProbe")))
	{
		// Alpha 探针与 P0-2 探针互斥，避免两个探针同时改写 ViewMode 与退出码。
		PathTracingAlphaProbe = MakeShared<FPathTracingAlphaProbe>();
		PathTracingAlphaProbe->Start();
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ConfigurationBatchBake")))
	{
		StartConfigurationBatchBake(true);
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("PathTracingProbe")))
	{
		// Path Tracing 探针拥有独立生命周期，不与资产探针共享状态或回调。
		PathTracingProbe = MakeShared<FPathTracingProbe>();
		PathTracingProbe->Start();
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("ContentPackProbe"))
		|| FParse::Param(FCommandLine::Get(), TEXT("ContentPackMountProbe")))
	{
		// 预检模式只读取真实 FPakFile 索引；挂载模式用于 Cooked 包集成验证。
		ContentPackProbeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FConfigurationSystemModule::TickContentPackProbe));
		return;
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("PrimaryAssetProbe")))
	{
		return;
	}

	// 不带探针开关时不注册任何回调，不改变正常启动路径。
	ProbeOutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("PrimaryAssetProbe"),
		TEXT("PrimaryAssetProbe.json"));
	FParse::Value(FCommandLine::Get(), TEXT("PrimaryAssetProbeOutput="), ProbeOutputPath);
	ProbeOutputPath = FPaths::ConvertRelativePathToFull(ProbeOutputPath);

	EngineInitCompleteHandle =
		FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(
			this,
			&FConfigurationSystemModule::SchedulePrimaryAssetProbe);
}

void FConfigurationSystemModule::ShutdownModule()
{
	if (ConfigurationStateProbe != nullptr)
	{
		// 探针在请求退出前自行解除 Root；此时 UObject 数组可能已开始销毁，
		// 模块关闭阶段不能再解引用该裸指针。
		ConfigurationStateProbe = nullptr;
	}

	if (PackagingBoundaryProbe.IsValid())
	{
		PackagingBoundaryProbe->Shutdown();
		PackagingBoundaryProbe.Reset();
	}

	if (VehicleHierarchyProbe.IsValid())
	{
		VehicleHierarchyProbe->Shutdown();
		VehicleHierarchyProbe.Reset();
	}

	if (PathTracingAlphaProbe.IsValid())
	{
		PathTracingAlphaProbe->Shutdown();
		PathTracingAlphaProbe.Reset();
	}

	if (ConfigurationBatchBake.IsValid())
	{
		ConfigurationBatchBake->Shutdown();
		ConfigurationBatchBake.Reset();
	}

	if (PathTracingProbe.IsValid())
	{
		PathTracingProbe->Shutdown();
		PathTracingProbe.Reset();
	}

	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}

	if (ProbeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ProbeTickerHandle);
		ProbeTickerHandle.Reset();
	}
	if (ContentPackProbeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ContentPackProbeTickerHandle);
		ContentPackProbeTickerHandle.Reset();
	}
	ContentPackProbeService.Reset();

	ProbeLoadHandle.Reset();
	FDefaultGameModuleImpl::ShutdownModule();
}

void FConfigurationSystemModule::StartConfigurationBatchBake(const bool bExitOnComplete)
{
	if (ConfigurationBatchBake.IsValid() && ConfigurationBatchBake->IsRunning())
	{
		UE_LOG(LogPrimaryAssetProbe, Warning, TEXT("批量 Bake 已在运行，忽略重复启动。"));
		return;
	}
	ConfigurationBatchBake = MakeShared<FConfigurationBatchBake>();
	ConfigurationBatchBake->Start(bExitOnComplete);
}

bool FConfigurationSystemModule::TickContentPackProbe(const float DeltaTime)
{
	(void)DeltaTime;
	ContentPackProbeTickerHandle.Reset();
	RunContentPackProbe();
	return false;
}

void FConfigurationSystemModule::RunContentPackProbe()
{
	FString ManifestPath;
	FString PakPath;
	FParse::Value(FCommandLine::Get(), TEXT("ContentPackManifest="), ManifestPath);
	FParse::Value(FCommandLine::Get(), TEXT("ContentPackPak="), PakPath);

	FContentPackMountPolicy Policy;
	Policy.CatalogVersion = TEXT("mvp-v1");
	Policy.EngineVersion = TEXT("5.8");
	Policy.Platform = TEXT("Win64");
	ContentPackProbeService = MakeUnique<FContentPackMountService>(MoveTemp(Policy));

	const bool bMountRequested =
		FParse::Param(FCommandLine::Get(), TEXT("ContentPackMountProbe"));
	const FContentPackMountResult Result = bMountRequested
		? ContentPackProbeService->PreflightAndMount(ManifestPath, PakPath)
		: ContentPackProbeService->Preflight(ManifestPath, PakPath);
	const bool bSucceeded =
		Result.bPreflightPassed && (!bMountRequested || Result.bMounted);
	if (bSucceeded)
	{
		UE_LOG(
			LogPrimaryAssetProbe,
			Display,
			TEXT("内容包真实 pak %s通过：%s@%s；mountPoint=%s；PrimaryAssetId=%d。"),
			bMountRequested ? TEXT("挂载") : TEXT("预检"),
			*Result.Manifest.PackId,
			*Result.Manifest.Version,
			*Result.Manifest.MountPoint,
			Result.Manifest.PrimaryAssetIds.Num());
	}
	else
	{
		UE_LOG(
			LogPrimaryAssetProbe,
			Error,
			TEXT("内容包真实 pak %s失败（manifest=%s，pak=%s）。"),
			bMountRequested ? TEXT("挂载") : TEXT("预检"),
			*ManifestPath,
			*PakPath);
		for (const FString& Error : Result.Errors)
		{
			UE_LOG(LogPrimaryAssetProbe, Error, TEXT("  %s"), *Error);
		}
	}

	FPlatformMisc::RequestExitWithStatus(false, bSucceeded ? 0 : 8);
}

void FConfigurationSystemModule::SchedulePrimaryAssetProbe()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}

	// 延迟到主循环 Tick，避免在 EditorInit 尚未完成时请求退出。
	ProbeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FConfigurationSystemModule::TickPrimaryAssetProbe));
}

bool FConfigurationSystemModule::TickPrimaryAssetProbe(const float DeltaTime)
{
	(void)DeltaTime;
	if (FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().IsLoadingAssets())
	{
		return true;
	}

	ProbeTickerHandle.Reset();
	RunPrimaryAssetProbe();
	return false;
}

void FConfigurationSystemModule::RunPrimaryAssetProbe()
{
	UAssetManager& AssetManager = UAssetManager::Get();
	const FPrimaryAssetType ProbeType(TEXT("PrimaryAssetProbe"));

	// 不主动补扫目录：这里专门验证 DefaultGame.ini 的启动扫描配置是否生效。
	AssetManager.GetPrimaryAssetIdList(ProbeType, ProbeAssetIds);
	ProbeAssetIds.Sort([](const FPrimaryAssetId& Left, const FPrimaryAssetId& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	UE_LOG(LogPrimaryAssetProbe, Display, TEXT("枚举到 %d 个 PrimaryAssetProbe 资产。"), ProbeAssetIds.Num());
	if (ProbeAssetIds.IsEmpty())
	{
		WriteProbeReportAndExit(true);
		return;
	}

	const TArray<FName> BundlesToLoad{TEXT("Probe")};
	ProbeLoadHandle = AssetManager.LoadPrimaryAssets(
		ProbeAssetIds,
		BundlesToLoad,
		FStreamableDelegate::CreateRaw(this, &FConfigurationSystemModule::FinishPrimaryAssetProbe));

	if (!ProbeLoadHandle.IsValid())
	{
		UE_LOG(LogPrimaryAssetProbe, Error, TEXT("AssetManager 未能创建异步加载句柄。"));
		WriteProbeReportAndExit(false);
	}
}

void FConfigurationSystemModule::FinishPrimaryAssetProbe()
{
	WriteProbeReportAndExit(true);
	ProbeLoadHandle.Reset();
}

void FConfigurationSystemModule::WriteProbeReportAndExit(const bool bLoadRequestCompleted)
{
	UAssetManager& AssetManager = UAssetManager::Get();
	TArray<TSharedPtr<FJsonValue>> AssetValues;
	bool bAllLoaded = bLoadRequestCompleted && !ProbeAssetIds.IsEmpty();

	for (const FPrimaryAssetId& AssetId : ProbeAssetIds)
	{
		const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
		UObject* LoadedObject = AssetManager.GetPrimaryAssetObject(AssetId);
		const UPrimaryAssetProbeData* ProbeData = Cast<UPrimaryAssetProbeData>(LoadedObject);

		TSharedRef<FJsonObject> AssetJson = MakeShared<FJsonObject>();
		AssetJson->SetStringField(TEXT("id"), AssetId.ToString());
		AssetJson->SetStringField(TEXT("path"), AssetPath.ToString());
		AssetJson->SetBoolField(TEXT("loaded"), LoadedObject != nullptr);
		AssetJson->SetStringField(
			TEXT("objectClass"),
			LoadedObject != nullptr ? LoadedObject->GetClass()->GetPathName() : FString());

		if (ProbeData != nullptr)
		{
			AssetJson->SetStringField(TEXT("probeTexture"), ProbeData->ProbeTexture.ToSoftObjectPath().ToString());
			AssetJson->SetBoolField(TEXT("probeTextureLoaded"), ProbeData->ProbeTexture.IsValid());
			bAllLoaded &= ProbeData->ProbeTexture.IsNull() || ProbeData->ProbeTexture.IsValid();
		}
		else
		{
			AssetJson->SetStringField(TEXT("probeTexture"), FString());
			AssetJson->SetBoolField(TEXT("probeTextureLoaded"), false);
		}

		bAllLoaded &= LoadedObject != nullptr;
		AssetValues.Add(MakeShared<FJsonValueObject>(AssetJson));
	}

	TSharedRef<FJsonObject> RootJson = MakeShared<FJsonObject>();
	RootJson->SetNumberField(TEXT("schemaVersion"), 1);
	RootJson->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	RootJson->SetStringField(TEXT("buildConfiguration"), PrimaryAssetProbe::GetBuildConfigurationName());
	RootJson->SetBoolField(TEXT("withEditor"), WITH_EDITOR != 0);
	RootJson->SetBoolField(TEXT("requiresCookedData"), FPlatformProperties::RequiresCookedData());
	RootJson->SetStringField(TEXT("primaryAssetType"), TEXT("PrimaryAssetProbe"));
	RootJson->SetNumberField(TEXT("requestedCount"), ProbeAssetIds.Num());
	RootJson->SetBoolField(TEXT("loadRequestCompleted"), bLoadRequestCompleted);
	RootJson->SetBoolField(TEXT("allLoaded"), bAllLoaded);
	RootJson->SetArrayField(TEXT("assets"), AssetValues);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bSerialized = FJsonSerializer::Serialize(RootJson, Writer);
	const bool bDirectoryReady = IFileManager::Get().MakeDirectory(*FPaths::GetPath(ProbeOutputPath), true);
	const bool bWritten = bSerialized && bDirectoryReady
		&& FFileHelper::SaveStringToFile(JsonText, *ProbeOutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bWritten)
	{
		UE_LOG(LogPrimaryAssetProbe, Display, TEXT("探针 JSON 已写入：%s"), *ProbeOutputPath);
	}
	else
	{
		UE_LOG(LogPrimaryAssetProbe, Error, TEXT("无法写入探针 JSON：%s"), *ProbeOutputPath);
	}

	const uint8 ExitCode = bWritten && bAllLoaded ? 0 : 2;
	FPlatformMisc::RequestExitWithStatus(false, ExitCode);
}
