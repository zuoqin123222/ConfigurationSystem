#include "PackagingBoundaryProbe.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PackagingProbeMarkerActor.h"
#include "PrimaryAssetProbeData.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogPackagingBoundaryProbe, Log, All);

namespace PackagingBoundaryProbe
{
	constexpr TCHAR ExpectedMap[] = TEXT("/Game/Maps/L_ConfigProbe");
	constexpr TCHAR ExpectedAssetId[] = TEXT("PrimaryAssetProbe:DA_ProbeUnreferenced");
	constexpr double TimeoutSeconds = 60.0;
}

void FPackagingBoundaryProbe::Start()
{
	OutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("PackagingBoundaryProbe"),
		TEXT("PackagingBoundaryProbe.json"));
	FParse::Value(FCommandLine::Get(), TEXT("PackagingBoundaryProbeOutput="), OutputPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);
	StartSeconds = FPlatformTime::Seconds();

	// 地图可能在模块启动后才完成切换，因此使用 CoreTicker 等待目标 World。
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FPackagingBoundaryProbe::Tick));
}

void FPackagingBoundaryProbe::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	LoadHandle.Reset();
}

bool FPackagingBoundaryProbe::Tick(const float DeltaTime)
{
	(void)DeltaTime;

	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (World == nullptr || World->WorldType != EWorldType::Game)
		{
			continue;
		}

		LoadedMap = World->GetPackage()->GetName();
		if (LoadedMap == PackagingBoundaryProbe::ExpectedMap)
		{
			TickerHandle.Reset();
			BeginAssetLoad(*World);
			return false;
		}
	}

	if (FPlatformTime::Seconds() - StartSeconds >= PackagingBoundaryProbe::TimeoutSeconds)
	{
		TickerHandle.Reset();
		WriteReportAndExit(false, TEXT("等待目标地图超时"));
		return false;
	}
	return true;
}

void FPackagingBoundaryProbe::BeginAssetLoad(UWorld& World)
{
	bRequiresCookedData = FPlatformProperties::RequiresCookedData();
	bEditorOnlyDataFilteredFromMap =
		World.GetPackage()->HasAnyPackageFlags(PKG_FilterEditorOnly);

	FModuleManager& ModuleManager = FModuleManager::Get();
	bEditorModuleExists = ModuleManager.ModuleExists(TEXT("ConfigurationSystemEditor"));
	bEditorModuleLoaded = ModuleManager.IsModuleLoaded(TEXT("ConfigurationSystemEditor"));

	for (TActorIterator<APackagingProbeMarkerActor> It(&World); It; ++It)
	{
		++MarkerCount;
		bMarkerValid |= It->Marker == 1;
		const UStaticMesh* Mesh = It->CubeComponent != nullptr ? It->CubeComponent->GetStaticMesh() : nullptr;
		bCubeHardReferenceValid |= Mesh != nullptr
			&& Mesh->GetPathName() == TEXT("/Engine/BasicShapes/Cube.Cube");
	}
	bMarkerValid = MarkerCount == 1 && bMarkerValid;

	FAssetRegistryModule& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (AssetRegistry.Get().IsLoadingAssets())
	{
		AssetRegistry.Get().WaitForCompletion();
	}

	UAssetManager& AssetManager = UAssetManager::Get();
	AssetManager.GetPrimaryAssetIdList(FPrimaryAssetType(TEXT("PrimaryAssetProbe")), AssetIds);
	AssetIds.Sort([](const FPrimaryAssetId& Left, const FPrimaryAssetId& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	const FPrimaryAssetId ExpectedId{
		FPrimaryAssetType(TEXT("PrimaryAssetProbe")),
		FName(TEXT("DA_ProbeUnreferenced"))};
	if (!AssetIds.Contains(ExpectedId))
	{
		WriteReportAndExit(false, TEXT("AssetManager 未枚举到 P0-1 DataAsset"));
		return;
	}

	// Probe Bundle 会把 DataAsset 的软纹理与 DataAsset 一并异步装入。
	LoadHandle = AssetManager.LoadPrimaryAssets(
		{ExpectedId},
		{TEXT("Probe")},
		FStreamableDelegate::CreateRaw(this, &FPackagingBoundaryProbe::FinishAssetLoad));
	if (!LoadHandle.IsValid())
	{
		WriteReportAndExit(false, TEXT("AssetManager 未创建异步加载句柄"));
	}
}

void FPackagingBoundaryProbe::FinishAssetLoad()
{
	WriteReportAndExit(true);
	LoadHandle.Reset();
}

void FPackagingBoundaryProbe::WriteReportAndExit(
	const bool bLoadRequestCompleted,
	const FString& FailureReason)
{
	UAssetManager& AssetManager = UAssetManager::Get();
	const FPrimaryAssetId ExpectedId{
		FPrimaryAssetType(TEXT("PrimaryAssetProbe")),
		FName(TEXT("DA_ProbeUnreferenced"))};
	const UPrimaryAssetProbeData* DataAsset =
		Cast<UPrimaryAssetProbeData>(AssetManager.GetPrimaryAssetObject(ExpectedId));
	const bool bDataAssetLoaded = DataAsset != nullptr;
	const bool bSoftTextureLoaded = DataAsset != nullptr
		&& !DataAsset->ProbeTexture.IsNull()
		&& DataAsset->ProbeTexture.IsValid();
	const FString SoftTexturePath =
		DataAsset != nullptr ? DataAsset->ProbeTexture.ToSoftObjectPath().ToString() : FString();
	const bool bMapLoadedFromCookedPackage =
		bRequiresCookedData && LoadedMap == PackagingBoundaryProbe::ExpectedMap;
	const bool bEditorBoundaryValid = !bEditorModuleExists && !bEditorModuleLoaded;
	const bool bSuccess = FailureReason.IsEmpty()
		&& LoadedMap == PackagingBoundaryProbe::ExpectedMap
		&& bMarkerValid
		&& bCubeHardReferenceValid
		&& bRequiresCookedData
		&& bMapLoadedFromCookedPackage
		&& bEditorBoundaryValid
		&& AssetIds.Contains(ExpectedId)
		&& bLoadRequestCompleted
		&& bDataAssetLoaded
		&& bSoftTextureLoaded;

	TArray<TSharedPtr<FJsonValue>> AssetIdValues;
	for (const FPrimaryAssetId& AssetId : AssetIds)
	{
		AssetIdValues.Add(MakeShared<FJsonValueString>(AssetId.ToString()));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 2);
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("buildConfiguration"), LexToString(FApp::GetBuildConfiguration()));
	Root->SetBoolField(TEXT("withEditor"), WITH_EDITOR != 0);
	Root->SetBoolField(TEXT("success"), bSuccess);
	Root->SetStringField(TEXT("failureReason"), FailureReason);
	Root->SetStringField(TEXT("expectedMap"), PackagingBoundaryProbe::ExpectedMap);
	Root->SetStringField(TEXT("loadedMap"), LoadedMap);
	Root->SetNumberField(TEXT("markerCount"), MarkerCount);
	Root->SetBoolField(TEXT("markerEqualsOne"), bMarkerValid);
	Root->SetBoolField(TEXT("cubeHardReferenceValid"), bCubeHardReferenceValid);
	Root->SetBoolField(TEXT("requiresCookedData"), bRequiresCookedData);
	Root->SetBoolField(TEXT("mapLoadedFromCookedPackage"), bMapLoadedFromCookedPackage);
	Root->SetBoolField(
		TEXT("editorOnlyDataFilteredFromMap"),
		bEditorOnlyDataFilteredFromMap);
	Root->SetBoolField(TEXT("editorModuleExists"), bEditorModuleExists);
	Root->SetBoolField(TEXT("editorModuleLoaded"), bEditorModuleLoaded);
	Root->SetBoolField(TEXT("editorBoundaryValid"), bEditorBoundaryValid);
	Root->SetStringField(TEXT("expectedPrimaryAssetId"), ExpectedId.ToString());
	Root->SetArrayField(TEXT("enumeratedPrimaryAssetIds"), AssetIdValues);
	Root->SetBoolField(TEXT("loadRequestCompleted"), bLoadRequestCompleted);
	Root->SetBoolField(TEXT("dataAssetLoaded"), bDataAssetLoaded);
	Root->SetStringField(TEXT("softTexturePath"), SoftTexturePath);
	Root->SetBoolField(TEXT("softTextureLoaded"), bSoftTextureLoaded);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bSerialized = FJsonSerializer::Serialize(Root, Writer);
	const bool bDirectoryReady =
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	const bool bWritten = bSerialized
		&& bDirectoryReady
		&& FFileHelper::SaveStringToFile(
			JsonText,
			*OutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bWritten && bSuccess)
	{
		UE_LOG(
			LogPackagingBoundaryProbe,
			Display,
			TEXT("打包边界探针通过，JSON=%s"),
			*OutputPath);
	}
	else
	{
		UE_LOG(
			LogPackagingBoundaryProbe,
			Error,
			TEXT("打包边界探针失败：%s，JSON=%s"),
			FailureReason.IsEmpty() ? TEXT("检查项未全部通过") : *FailureReason,
			*OutputPath);
	}
	FPlatformMisc::RequestExitWithStatus(false, bWritten && bSuccess ? 0 : 5);
}
