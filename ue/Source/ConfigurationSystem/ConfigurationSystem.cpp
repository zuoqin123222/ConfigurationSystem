#include "ConfigurationSystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
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

	if (FParse::Param(FCommandLine::Get(), TEXT("PathTracingProbe")))
	{
		// Path Tracing 探针拥有独立生命周期，不与资产探针共享状态或回调。
		PathTracingProbe = MakeShared<FPathTracingProbe>();
		PathTracingProbe->Start();
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

	ProbeLoadHandle.Reset();
	FDefaultGameModuleImpl::ShutdownModule();
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
