#include "ConfigurationSystemEditor.h"

#include "AdminImportPreflight.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ConfigShowroomGameMode.h"
#include "ConfigurationBatchBake.h"
#include "ConfiguratorVehicleActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "PackagingProbeMarkerActor.h"
#include "PrimaryAssetProbeData.h"
#include "SAdminImportPanel.h"
#include "ShowroomEnvironmentActor.h"
#include "ToolMenus.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Widgets/Docking/SDockTab.h"

DEFINE_LOG_CATEGORY_STATIC(LogPrimaryAssetProbeEditor, Log, All);

namespace PrimaryAssetProbeEditor
{
	const FName AdminImportTabName(TEXT("ConfigurationSystem.AdminImport"));
	constexpr TCHAR TexturePackageName[] = TEXT("/Game/PrimaryAssetProbe/T_ProbeUnreferenced");
	constexpr TCHAR TextureAssetName[] = TEXT("T_ProbeUnreferenced");
	constexpr TCHAR DataPackageName[] = TEXT("/Game/PrimaryAssetProbe/DA_ProbeUnreferenced");
	constexpr TCHAR DataAssetName[] = TEXT("DA_ProbeUnreferenced");
	constexpr TCHAR ProbeMapPackageName[] = TEXT("/Game/Maps/L_ConfigProbe");
	constexpr TCHAR ShowroomMapPackageName[] = TEXT("/Game/Maps/L_ConfigShowroom");
	constexpr TCHAR StudioLightingMapPackageName[] = TEXT("/Game/Maps/L_Lighting_Studio");
	constexpr TCHAR OutdoorLightingMapPackageName[] = TEXT("/Game/Maps/L_Lighting_Outdoor");

	template <typename AssetType>
	AssetType* LoadOrCreateAsset(const TCHAR* PackageName, const TCHAR* AssetName, bool& bWasCreated)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
		if (AssetType* ExistingAsset = LoadObject<AssetType>(nullptr, *ObjectPath))
		{
			bWasCreated = false;
			return ExistingAsset;
		}

		UPackage* Package = CreatePackage(PackageName);
		bWasCreated = true;
		return NewObject<AssetType>(Package, AssetName, RF_Public | RF_Standalone);
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}

	template <typename ActorType>
	ActorType* FindOrSpawnActor(
		UWorld* World,
		const TCHAR* StableLabel,
		const FTransform& Transform)
	{
		ActorType* Result = nullptr;
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			if (It->GetActorLabel() == StableLabel)
			{
				if (Result == nullptr)
				{
					Result = *It;
				}
				else
				{
					World->EditorDestroyActor(*It, false);
				}
			}
		}
		if (Result == nullptr)
		{
			Result = World->SpawnActor<ActorType>(
				ActorType::StaticClass(),
				Transform.GetLocation(),
				Transform.Rotator());
		}
		if (Result != nullptr)
		{
			Result->SetActorLabel(StableLabel);
			Result->SetActorTransform(Transform);
		}
		return Result;
	}

	bool CreateLightingMap(const TCHAR* PackageName, const bool bStudio)
	{
		UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
		if (World == nullptr)
		{
			return false;
		}

		const TCHAR* Prefix = bStudio ? TEXT("Studio") : TEXT("Outdoor");
		AStaticMeshActor* Floor = FindOrSpawnActor<AStaticMeshActor>(
			World,
			*FString::Printf(TEXT("%sFloor_TEMP"), Prefix),
			FTransform(
				FRotator::ZeroRotator,
				FVector(0.0, 0.0, -10.0),
				FVector(18.0, 18.0, 0.2)));
		if (Floor != nullptr)
		{
			Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(
				nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
			Floor->Tags.AddUnique(AConfiguratorVehicleActor::TemporaryResourceTag);
		}

		ADirectionalLight* KeyLight = FindOrSpawnActor<ADirectionalLight>(
			World,
			*FString::Printf(TEXT("%sDirectionalLight_TEMP"), Prefix),
			FTransform(
				bStudio ? FRotator(-38.0, -32.0, 0.0) : FRotator(-24.0, 145.0, 0.0),
				FVector::ZeroVector));
		if (KeyLight != nullptr)
		{
			KeyLight->GetLightComponent()->SetIntensity(bStudio ? 7.0f : 9.0f);
			KeyLight->GetLightComponent()->SetLightColor(
				bStudio
					? FLinearColor(1.0f, 0.92f, 0.8f)
					: FLinearColor(0.82f, 0.9f, 1.0f));
			KeyLight->Tags.AddUnique(AConfiguratorVehicleActor::TemporaryResourceTag);
		}

		ASkyLight* PresetSky = FindOrSpawnActor<ASkyLight>(
			World,
			*FString::Printf(TEXT("%sSkyLight_TEMP"), Prefix),
			FTransform::Identity);
		if (PresetSky != nullptr)
		{
			PresetSky->GetLightComponent()->SetIntensity(bStudio ? 0.8f : 1.35f);
			PresetSky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
			PresetSky->Tags.AddUnique(AConfiguratorVehicleActor::TemporaryResourceTag);
		}

		APointLight* FillLight = FindOrSpawnActor<APointLight>(
			World,
			*FString::Printf(TEXT("%sFillLight_TEMP"), Prefix),
			FTransform(FRotator::ZeroRotator, FVector(-250.0, -450.0, 380.0)));
		if (FillLight != nullptr)
		{
			FillLight->PointLightComponent->SetIntensity(6500.0f);
			FillLight->PointLightComponent->SetAttenuationRadius(1800.0f);
			FillLight->PointLightComponent->SetLightColor(
				FLinearColor(0.55f, 0.68f, 1.0f));
			FillLight->Tags.AddUnique(AConfiguratorVehicleActor::TemporaryResourceTag);
		}

		const bool bComplete =
			Floor != nullptr && KeyLight != nullptr && PresetSky != nullptr && FillLight != nullptr;
		return bComplete && UEditorLoadingAndSavingUtils::SaveMap(World, PackageName);
	}

	bool AddLightingStreamingLevel(
		UWorld* World,
		const TCHAR* PackageName,
		const bool bInitiallyLoaded)
	{
		ULevelStreamingDynamic* StreamingLevel =
			NewObject<ULevelStreamingDynamic>(World, NAME_None, RF_Transactional);
		if (StreamingLevel == nullptr)
		{
			return false;
		}
		StreamingLevel->SetWorldAssetByPackageName(FName(PackageName));
		StreamingLevel->bInitiallyLoaded = bInitiallyLoaded;
		StreamingLevel->bInitiallyVisible = bInitiallyLoaded;
		StreamingLevel->bShouldBlockOnLoad = true;
		StreamingLevel->SetShouldBeLoaded(bInitiallyLoaded);
		StreamingLevel->SetShouldBeVisible(bInitiallyLoaded);
		StreamingLevel->SetShouldBeVisibleInEditor(bInitiallyLoaded);
		World->AddStreamingLevel(StreamingLevel);
		return true;
	}
}

IMPLEMENT_MODULE(FConfigurationSystemEditorModule, ConfigurationSystemEditor);

void FConfigurationSystemEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		PrimaryAssetProbeEditor::AdminImportTabName,
		FOnSpawnTab::CreateRaw(this, &FConfigurationSystemEditorModule::SpawnAdminImportTab))
		.SetDisplayName(NSLOCTEXT(
			"ConfigurationSystemEditor",
			"AdminImportTab",
			"Configuration System 管理员导入"))
		.SetTooltipText(NSLOCTEXT(
			"ConfigurationSystemEditor",
			"AdminImportTabTooltip",
			"预检模型/动画导入，或预检并挂载外部材质内容包。"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this,
			&FConfigurationSystemEditorModule::RegisterMenus));
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		RegisterMenus();
	}

	CreateAssetsCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PrimaryAssetProbe.CreateTestAssets"),
		TEXT("创建或刷新未被地图硬引用的测试纹理和 Primary Data Asset。"),
		FConsoleCommandDelegate::CreateRaw(this, &FConfigurationSystemEditorModule::CreatePrimaryAssetProbeAssets),
		ECVF_Default);

	CreateProjectMapCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PackagingProbe.CreateProjectMap"),
		TEXT("幂等创建、刷新并保存 /Game/Maps/L_ConfigProbe，确保仅放置一个 Runtime marker。"),
		FConsoleCommandDelegate::CreateRaw(this, &FConfigurationSystemEditorModule::CreatePackagingProbeMap),
		ECVF_Default);

	CreateShowroomMapCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ConfigurationSystem.CreateShowroomMap"),
		TEXT("幂等创建/刷新主展厅与两套灯光流式关卡。"),
		FConsoleCommandDelegate::CreateRaw(
			this, &FConfigurationSystemEditorModule::RequestCreateConfigShowroomMap),
		ECVF_Default);

	AdminImportProbeCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ConfigurationSystem.AdminImport.Preflight"),
		TEXT("按 -AdminModelFbx/-AdminModelSidecar/-AdminAnimationFbx/-AdminAnimationSidecar 运行预检并写 JSON。"),
		FConsoleCommandDelegate::CreateRaw(this, &FConfigurationSystemEditorModule::RunAdminImportProbe),
		ECVF_Default);

	ConfigurationBatchBakeCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ConfigurationSystem.BakePublishedConfigurations"),
		TEXT("在 PIE/Game Viewport 中批量 Bake published-configurations，输出到 staging。"),
		FConsoleCommandDelegate::CreateRaw(
			this, &FConfigurationSystemEditorModule::StartConfigurationBatchBake),
		ECVF_Default);

	if (FParse::Param(FCommandLine::Get(), TEXT("AdminImportPreflightProbe")))
	{
		// Defer exit until the engine loop is live; requesting it from module startup
		// is not honored consistently by the desktop editor bootstrap.
		AdminImportProbeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(
				this,
				&FConfigurationSystemEditorModule::TickAdminImportProbe));
	}
}

void FConfigurationSystemEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(
		PrimaryAssetProbeEditor::AdminImportTabName);
	if (AdminImportProbeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(AdminImportProbeTickerHandle);
		AdminImportProbeTickerHandle.Reset();
	}
	if (CreateShowroomMapTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CreateShowroomMapTickerHandle);
		CreateShowroomMapTickerHandle.Reset();
	}

	if (CreateAssetsCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CreateAssetsCommand);
		CreateAssetsCommand = nullptr;
	}

	if (CreateProjectMapCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CreateProjectMapCommand);
		CreateProjectMapCommand = nullptr;
	}

	if (CreateShowroomMapCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CreateShowroomMapCommand);
		CreateShowroomMapCommand = nullptr;
	}

	if (AdminImportProbeCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(AdminImportProbeCommand);
		AdminImportProbeCommand = nullptr;
	}
	if (ConfigurationBatchBakeCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(ConfigurationBatchBakeCommand);
		ConfigurationBatchBakeCommand = nullptr;
	}
}

void FConfigurationSystemEditorModule::StartConfigurationBatchBake()
{
	StartConfigurationBatchBakeFromEditor();
}

void FConfigurationSystemEditorModule::RegisterMenus()
{
	if (bMenusRegistered)
	{
		return;
	}
	bMenusRegistered = true;

	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = ToolsMenu->AddSection(
		TEXT("ConfigurationSystem"),
		NSLOCTEXT("ConfigurationSystemEditor", "AdminImportSection", "Configuration System"),
		FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
	Section.AddMenuEntry(
		TEXT("ConfigurationSystemAdminImport"),
		NSLOCTEXT("ConfigurationSystemEditor", "AdminImportMenu", "Configuration System 管理员导入"),
		NSLOCTEXT(
			"ConfigurationSystemEditor",
			"AdminImportMenuTooltip",
			"打开 Editor-only FBX 暂存导入与材质内容包安全挂载工具。"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]
		{
			FGlobalTabmanager::Get()->TryInvokeTab(
				PrimaryAssetProbeEditor::AdminImportTabName);
		})));
	UToolMenus::Get()->RefreshMenuWidget(TEXT("LevelEditor.MainMenu.Tools"));
}

TSharedRef<SDockTab> FConfigurationSystemEditorModule::SpawnAdminImportTab(
	const FSpawnTabArgs& Args)
{
	(void)Args;
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAdminImportPanel)
		];
}

bool FConfigurationSystemEditorModule::TickAdminImportProbe(const float DeltaTime)
{
	(void)DeltaTime;
	AdminImportProbeTickerHandle.Reset();
	RunAdminImportProbe();
	return false;
}

void FConfigurationSystemEditorModule::RunAdminImportProbe()
{
	TArray<FAdminImportSelection> Selections;
	FString Fbx;
	FString Sidecar;
	const bool bHasModelFbx =
		FParse::Value(FCommandLine::Get(), TEXT("AdminModelFbx="), Fbx);
	const bool bHasModelSidecar =
		FParse::Value(FCommandLine::Get(), TEXT("AdminModelSidecar="), Sidecar);
	if (bHasModelFbx || bHasModelSidecar)
	{
		FAdminImportSelection& Selection = Selections.AddDefaulted_GetRef();
		Selection.Kind = EAdminImportAssetKind::Model;
		Selection.FbxFile = Fbx;
		Selection.SidecarFile = Sidecar;
	}

	Fbx.Reset();
	Sidecar.Reset();
	const bool bHasAnimationFbx =
		FParse::Value(FCommandLine::Get(), TEXT("AdminAnimationFbx="), Fbx);
	const bool bHasAnimationSidecar =
		FParse::Value(FCommandLine::Get(), TEXT("AdminAnimationSidecar="), Sidecar);
	if (bHasAnimationFbx || bHasAnimationSidecar)
	{
		FAdminImportSelection& Selection = Selections.AddDefaulted_GetRef();
		Selection.Kind = EAdminImportAssetKind::Animation;
		Selection.FbxFile = Fbx;
		Selection.SidecarFile = Sidecar;
	}

	FAdminImportPreflightResult Result = FAdminImportPreflight::Run(Selections);
	if (Result.bPassed)
	{
		UE_LOG(
			LogPrimaryAssetProbeEditor,
			Display,
			TEXT("管理员导入预检通过：%s"),
			*Result.ReportPath);
	}
	else
	{
		UE_LOG(
			LogPrimaryAssetProbeEditor,
			Error,
			TEXT("管理员导入预检失败：%s"),
			*Result.ReportPath);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AdminImportPreflightProbe")))
	{
		FPlatformMisc::RequestExitWithStatus(false, Result.bPassed ? 0 : 7);
	}
}

void FConfigurationSystemEditorModule::CreatePrimaryAssetProbeAssets()
{
	using namespace PrimaryAssetProbeEditor;

	bool bTextureCreated = false;
	UTexture2D* Texture = LoadOrCreateAsset<UTexture2D>(
		TexturePackageName,
		TextureAssetName,
		bTextureCreated);

	// 固定 2x2 BGRA 像素，重复运行会得到同样的资产内容。
	const uint8 Pixels[] =
	{
		0, 0, 255, 255,       0, 255, 0, 255,
		255, 0, 0, 255,       255, 255, 255, 255
	};
	Texture->Source.Init(2, 2, 1, 1, TSF_BGRA8, Pixels);
	Texture->SRGB = true;
	Texture->NeverStream = true;
	Texture->PostEditChange();

	if (bTextureCreated)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}

	bool bDataAssetCreated = false;
	UPrimaryAssetProbeData* DataAsset = LoadOrCreateAsset<UPrimaryAssetProbeData>(
		DataPackageName,
		DataAssetName,
		bDataAssetCreated);
	DataAsset->ProbeTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Texture));
	DataAsset->PostEditChange();

	if (bDataAssetCreated)
	{
		FAssetRegistryModule::AssetCreated(DataAsset);
	}

	const bool bTextureSaved = SaveAsset(Texture);
	const bool bDataAssetSaved = SaveAsset(DataAsset);
	if (bTextureSaved && bDataAssetSaved)
	{
		UE_LOG(
			LogPrimaryAssetProbeEditor,
			Display,
			TEXT("测试资产已创建/刷新：%s，%s"),
			TexturePackageName,
			DataPackageName);
	}
	else
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("测试资产保存失败，请检查 Content 目录是否可写。"));
	}
}

void FConfigurationSystemEditorModule::CreatePackagingProbeMap()
{
	using namespace PrimaryAssetProbeEditor;

	UWorld* World = nullptr;
	FString ExistingFilename;
	if (FPackageName::DoesPackageExist(ProbeMapPackageName, &ExistingFilename))
	{
		World = UEditorLoadingAndSavingUtils::LoadMap(ExistingFilename);
	}
	else
	{
		World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	}

	if (World == nullptr)
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("无法创建或载入探针地图。"));
		return;
	}

	APackagingProbeMarkerActor* MarkerToKeep = nullptr;
	for (TActorIterator<APackagingProbeMarkerActor> It(World); It; ++It)
	{
		if (MarkerToKeep == nullptr)
		{
			MarkerToKeep = *It;
		}
		else
		{
			// 删除重复标记，保证命令可安全重复执行。
			World->EditorDestroyActor(*It, false);
		}
	}

	if (MarkerToKeep == nullptr)
	{
		MarkerToKeep = World->SpawnActor<APackagingProbeMarkerActor>(
			APackagingProbeMarkerActor::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator);
	}

	if (MarkerToKeep == nullptr)
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("无法在探针地图中生成 Runtime marker。"));
		return;
	}

	MarkerToKeep->Marker = 1;
	MarkerToKeep->SetActorLabel(TEXT("PackagingProbeMarker"));
	MarkerToKeep->MarkPackageDirty();

	if (UEditorLoadingAndSavingUtils::SaveMap(World, ProbeMapPackageName))
	{
		UE_LOG(
			LogPrimaryAssetProbeEditor,
			Display,
			TEXT("打包探针地图已创建/刷新：%s（marker=1）。"),
			ProbeMapPackageName);
	}
	else
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("打包探针地图保存失败。"));
	}
}

void FConfigurationSystemEditorModule::RequestCreateConfigShowroomMap()
{
	if (!CreateShowroomMapTickerHandle.IsValid())
	{
		CreateShowroomMapTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(
				this, &FConfigurationSystemEditorModule::TickCreateConfigShowroomMap));
	}
}

bool FConfigurationSystemEditorModule::TickCreateConfigShowroomMap(const float DeltaTime)
{
	(void)DeltaTime;
	CreateShowroomMapTickerHandle.Reset();
	CreateConfigShowroomMap();
	return false;
}

void FConfigurationSystemEditorModule::CreateConfigShowroomMap()
{
	using namespace PrimaryAssetProbeEditor;

	const bool bStudioMapSaved = CreateLightingMap(StudioLightingMapPackageName, true);
	const bool bOutdoorMapSaved = CreateLightingMap(OutdoorLightingMapPackageName, false);
	if (!bStudioMapSaved || !bOutdoorMapSaved)
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("展厅灯光流式关卡创建或保存失败。"));
		if (FApp::IsUnattended())
		{
			FPlatformMisc::RequestExitWithStatus(false, 9);
		}
		return;
	}

	// 该地图完全由代码拥有；从空世界重建比在当前默认地图上原地保存更稳定，
	// 且避免命令行启动时递归加载正在打开的同名地图。
	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (World == nullptr)
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("无法创建或载入展厅地图。"));
		return;
	}

	World->GetWorldSettings()->DefaultGameMode = AConfigShowroomGameMode::StaticClass();

	AConfiguratorVehicleActor* Vehicle = FindOrSpawnActor<AConfiguratorVehicleActor>(
		World,
		TEXT("ConfiguratorPlaceholderVehicle_TEMP"),
		FTransform(FRotator::ZeroRotator, FVector::ZeroVector));

	const TCHAR* CameraLabels[] = {
		TEXT("ShowroomCamera"), TEXT("ShowroomCameraRear"), TEXT("ShowroomCameraLeft"),
		TEXT("ShowroomCameraRight"), TEXT("ShowroomCameraInterior"),
		TEXT("ShowroomCameraInteriorPassenger")
	};
	const FTransform CameraTransforms[] = {
		FTransform(FRotator(-14.0, -150.0, 0.0), FVector(920.0, 520.0, 310.0)),
		FTransform(FRotator(-12.0, 28.0, 0.0), FVector(-900.0, -470.0, 285.0)),
		FTransform(FRotator(-10.0, -90.0, 0.0), FVector(0.0, 880.0, 250.0)),
		FTransform(FRotator(-10.0, 90.0, 0.0), FVector(0.0, -880.0, 250.0)),
		// Audi 资产约定 +X 为车头、左驾位于 Y<0；机位放在真实眼点并朝前。
		FTransform(FRotator(-4.0, 0.0, 0.0), FVector(-15.0, -42.0, 122.0)),
		FTransform(FRotator(-4.0, 0.0, 0.0), FVector(-15.0, 42.0, 122.0))
	};
	bool bAllCamerasCreated = true;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CameraLabels); ++Index)
	{
		ACameraActor* Camera = FindOrSpawnActor<ACameraActor>(
			World, CameraLabels[Index], CameraTransforms[Index]);
		bAllCamerasCreated &= Camera != nullptr;
		if (Camera != nullptr)
		{
			Camera->GetCameraComponent()->SetFieldOfView(Index >= 4 ? 64.0f : 42.0f);
			Camera->Tags.AddUnique(FName(
				*FString::Printf(TEXT("Configurator.Camera.%d"), Index)));
			Camera->Tags.AddUnique(AConfiguratorVehicleActor::TemporaryResourceTag);
		}
	}

	AShowroomEnvironmentActor* Environment =
		FindOrSpawnActor<AShowroomEnvironmentActor>(
			World, TEXT("ShowroomEnvironmentStreamController"), FTransform::Identity);
	const bool bStudioStreamingAdded =
		AddLightingStreamingLevel(World, StudioLightingMapPackageName, true);
	const bool bOutdoorStreamingAdded =
		AddLightingStreamingLevel(World, OutdoorLightingMapPackageName, false);

	const bool bComplete = Vehicle != nullptr
		&& bAllCamerasCreated
		&& Environment != nullptr
		&& bStudioStreamingAdded
		&& bOutdoorStreamingAdded;
	const bool bSaved =
		bComplete && UEditorLoadingAndSavingUtils::SaveMap(World, ShowroomMapPackageName);
	if (bSaved)
	{
		UE_LOG(
			LogPrimaryAssetProbeEditor,
			Display,
			TEXT("展厅主关卡与双灯光流式关卡已幂等创建/刷新：%s。"),
			ShowroomMapPackageName);
	}
	else
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("展厅地图创建或保存失败。"));
	}
	if (FApp::IsUnattended())
	{
		FPlatformMisc::RequestExitWithStatus(false, bSaved ? 0 : 9);
	}
}
