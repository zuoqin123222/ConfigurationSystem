#include "ConfigurationSystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AutomotiveMaterialGuiProbe.h"
#include "ConfigurationBatchBake.h"
#include "ContentPackMountService.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformProperties.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "PackagingBoundaryProbe.h"
#include "PathTracingAlphaProbe.h"
#include "PathTracingProbe.h"
#include "PrimaryAssetProbeData.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "VehicleHierarchyProbe.h"
#include "ConfigurationStateProbe.h"

DEFINE_LOG_CATEGORY_STATIC(LogPrimaryAssetProbe, Log, All);

namespace PrimaryAssetProbe
{
	constexpr int32 RuntimeLogRetentionCount = 10;
	TUniquePtr<FArchive> RuntimeLogWriter;
	FCriticalSection RuntimeLogMutex;

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

	const TCHAR* GetRunMode()
	{
		const TCHAR* CommandLine = FCommandLine::Get();
		if (FParse::Param(CommandLine, TEXT("ConfigurationStateProbe")))
		{
			return TEXT("configuration-state-probe");
		}
		if (FParse::Param(CommandLine, TEXT("AutomotiveMaterialGuiProbe")))
		{
			return TEXT("automotive-material-gui-probe");
		}
		if (FParse::Param(CommandLine, TEXT("VehicleHierarchyProbe")))
		{
			return TEXT("vehicle-hierarchy-probe");
		}
		if (FParse::Param(CommandLine, TEXT("PackagingBoundaryProbe")))
		{
			return TEXT("packaging-boundary-probe");
		}
		if (FParse::Param(CommandLine, TEXT("PathTracingAlphaProbe")))
		{
			return TEXT("path-tracing-alpha-probe");
		}
		if (FParse::Param(CommandLine, TEXT("ConfigurationBatchBake")))
		{
			return TEXT("configuration-batch-bake");
		}
		if (FParse::Param(CommandLine, TEXT("PathTracingProbe")))
		{
			return TEXT("path-tracing-probe");
		}
		if (FParse::Param(CommandLine, TEXT("ContentPackProbe"))
			|| FParse::Param(CommandLine, TEXT("ContentPackMountProbe")))
		{
			return TEXT("content-pack-probe");
		}
		if (FParse::Param(CommandLine, TEXT("PrimaryAssetProbe")))
		{
			return TEXT("primary-asset-probe");
		}
		return TEXT("interactive");
	}

	void OpenRuntimeLog()
	{
		const FString LogDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("Diagnostics"));
		if (!IFileManager::Get().MakeDirectory(*LogDirectory, true))
		{
			return;
		}

		const FString LogName = FString::Printf(
			TEXT("ConfigurationSystem-runtime-%s-%u.log"),
			*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")),
			FPlatformProcess::GetCurrentProcessId());
		RuntimeLogWriter.Reset(IFileManager::Get().CreateFileWriter(
			*FPaths::Combine(LogDirectory, LogName),
			FILEWRITE_AllowRead));
	}

	void WriteRuntimeLog(const TCHAR* Level, const FString& Message)
	{
		FScopeLock Lock(&RuntimeLogMutex);
		if (!RuntimeLogWriter.IsValid())
		{
			return;
		}

		const FString Line = FString::Printf(
			TEXT("%s [%s] %s\r\n"),
			*FDateTime::UtcNow().ToIso8601(),
			Level,
			*Message);
		FTCHARToUTF8 Utf8(*Line);
		RuntimeLogWriter->Serialize(
			const_cast<ANSICHAR*>(Utf8.Get()),
			Utf8.Length());
		RuntimeLogWriter->Flush();
	}

	void CloseRuntimeLog()
	{
		FScopeLock Lock(&RuntimeLogMutex);
		if (RuntimeLogWriter.IsValid())
		{
			RuntimeLogWriter->Flush();
			RuntimeLogWriter.Reset();
		}
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
	PrimaryAssetProbe::OpenRuntimeLog();
	PruneOldRuntimeLogs();

	ApplicationWillTerminateHandle =
		FCoreDelegates::GetApplicationWillTerminateDelegate().AddRaw(
			this,
			&FConfigurationSystemModule::HandleApplicationWillTerminate);
	PreExitHandle = FCoreDelegates::OnPreExit.AddRaw(
		this,
		&FConfigurationSystemModule::HandlePreExit);
	SystemErrorHandle = FCoreDelegates::OnHandleSystemError.AddRaw(
		this,
		&FConfigurationSystemModule::HandleSystemError);
	SystemHangHandle = FCoreDelegates::OnHandleSystemHang.AddRaw(
		this,
		&FConfigurationSystemModule::HandleSystemHang);

	PrimaryAssetProbe::WriteRuntimeLog(TEXT("INFO"), FString::Printf(
		TEXT("进程启动：mode=%s，build=%s，engine=%s，buildVersion=%s，pid=%u。"),
		PrimaryAssetProbe::GetRunMode(),
		PrimaryAssetProbe::GetBuildConfigurationName(),
		*FEngineVersion::Current().ToString(),
		FApp::GetBuildVersion(),
		FPlatformProcess::GetCurrentProcessId()));

	if (FParse::Param(FCommandLine::Get(), TEXT("ConfigurationStateProbe")))
	{
		// 配置状态探针独占进程，完成纯数据检查并写出 JSON 后主动退出。
		ConfigurationStateProbe = NewObject<UConfigurationStateProbe>();
		ConfigurationStateProbe->Start();
		return;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("AutomotiveMaterialGuiProbe")))
	{
		AutomotiveMaterialGuiProbe = NewObject<UAutomotiveMaterialGuiProbe>();
		AutomotiveMaterialGuiProbe->Start();
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
	if (!bPreExitLogged)
	{
		PrimaryAssetProbe::WriteRuntimeLog(
			TEXT("WARNING"),
			TEXT("模块关闭时未观察到正常 OnPreExit 标记；可能是模块重载或非标准退出流程。"));
	}

	if (ApplicationWillTerminateHandle.IsValid())
	{
		FCoreDelegates::GetApplicationWillTerminateDelegate().Remove(ApplicationWillTerminateHandle);
		ApplicationWillTerminateHandle.Reset();
	}
	if (PreExitHandle.IsValid())
	{
		FCoreDelegates::OnPreExit.Remove(PreExitHandle);
		PreExitHandle.Reset();
	}
	if (SystemErrorHandle.IsValid())
	{
		FCoreDelegates::OnHandleSystemError.Remove(SystemErrorHandle);
		SystemErrorHandle.Reset();
	}
	if (SystemHangHandle.IsValid())
	{
		FCoreDelegates::OnHandleSystemHang.Remove(SystemHangHandle);
		SystemHangHandle.Reset();
	}

	if (ConfigurationStateProbe != nullptr)
	{
		// 探针在请求退出前自行解除 Root；此时 UObject 数组可能已开始销毁，
		// 模块关闭阶段不能再解引用该裸指针。
		ConfigurationStateProbe = nullptr;
	}
	if (AutomotiveMaterialGuiProbe != nullptr)
	{
		AutomotiveMaterialGuiProbe = nullptr;
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
	PrimaryAssetProbe::WriteRuntimeLog(TEXT("INFO"), TEXT("Runtime 模块关闭完成。"));
	PrimaryAssetProbe::CloseRuntimeLog();
	FDefaultGameModuleImpl::ShutdownModule();
}

void FConfigurationSystemModule::PruneOldRuntimeLogs()
{
	struct FRuntimeLogEntry
	{
		FString Path;
		FDateTime Timestamp;
	};

	const FString LogDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Diagnostics"));
	TArray<FString> LogNames;
	IFileManager::Get().FindFiles(
		LogNames,
		*FPaths::Combine(LogDirectory, TEXT("ConfigurationSystem-runtime-*.log")),
		true,
		false);

	TArray<FRuntimeLogEntry> LogEntries;
	LogEntries.Reserve(LogNames.Num());
	for (const FString& LogName : LogNames)
	{
		const FString RuntimeLogPath = FPaths::Combine(LogDirectory, LogName);
		LogEntries.Add({RuntimeLogPath, IFileManager::Get().GetTimeStamp(*RuntimeLogPath)});
	}
	LogEntries.Sort([](const FRuntimeLogEntry& Left, const FRuntimeLogEntry& Right)
	{
		return Left.Timestamp > Right.Timestamp;
	});

	int32 DeletedCount = 0;
	for (int32 Index = PrimaryAssetProbe::RuntimeLogRetentionCount; Index < LogEntries.Num(); ++Index)
	{
		if (IFileManager::Get().Delete(*LogEntries[Index].Path, false, true, true))
		{
			++DeletedCount;
		}
	}

	if (DeletedCount > 0)
	{
		PrimaryAssetProbe::WriteRuntimeLog(TEXT("INFO"), FString::Printf(
			TEXT("主日志留存清理完成：删除 %d 份。"),
			DeletedCount));
	}
}

void FConfigurationSystemModule::HandleApplicationWillTerminate()
{
	bApplicationWillTerminateReceived = true;
	PrimaryAssetProbe::WriteRuntimeLog(
		TEXT("INFO"),
		TEXT("收到平台 ApplicationWillTerminate 通知。"));
}

void FConfigurationSystemModule::HandlePreExit()
{
	if (bPreExitLogged)
	{
		return;
	}

	bPreExitLogged = true;
	PrimaryAssetProbe::WriteRuntimeLog(TEXT("INFO"), FString::Printf(
		TEXT("进入正常引擎退出流程：platformTerminate=%s，exitRequested=%s。"),
		bApplicationWillTerminateReceived ? TEXT("true") : TEXT("false"),
		IsEngineExitRequested() ? TEXT("true") : TEXT("false")));
}

void FConfigurationSystemModule::HandleSystemError()
{
	PrimaryAssetProbe::WriteRuntimeLog(
		TEXT("FATAL"),
		TEXT("收到 UE OnHandleSystemError 回调；请结合 CrashContext 或 Windows LocalDump 分析。"));
}

void FConfigurationSystemModule::HandleSystemHang()
{
	PrimaryAssetProbe::WriteRuntimeLog(
		TEXT("FATAL"),
		TEXT("收到 UE OnHandleSystemHang 回调；请结合 CrashContext 或 Windows LocalDump 分析。"));
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
