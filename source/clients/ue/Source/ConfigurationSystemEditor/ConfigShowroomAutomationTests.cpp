#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ConfigShowroomGameMode.h"
#include "ConfiguratorVehicleActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/LevelStreaming.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ShowroomEnvironmentActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigShowroomMapAutomationTest,
	"ConfigurationSystem.Editor.Showroom.GeneratedMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ConfigShowroomAutomation
{
	template <typename ActorType>
	int32 CountByLabel(UWorld* World, const TCHAR* Label)
	{
		int32 Count = 0;
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			Count += It->GetActorLabel() == Label ? 1 : 0;
		}
		return Count;
	}

	template <typename ActorType>
	ActorType* FindByLabel(UWorld* World, const TCHAR* Label)
	{
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}

	template <typename ActorType>
	int32 CountByTag(UWorld* World, const FName Tag, TSet<const AActor*>& OutActors)
	{
		int32 Count = 0;
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				++Count;
				OutActors.Add(*It);
			}
		}
		return Count;
	}

	ACameraActor* FindCameraByTag(UWorld* World, const FName Tag)
	{
		for (TActorIterator<ACameraActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	template <typename ActorType>
	int32 CountInPersistentLevel(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			Count += It->GetLevel() == World->PersistentLevel ? 1 : 0;
		}
		return Count;
	}

	ULevelStreaming* FindStreamingLevel(UWorld* World, const FName PackageName)
	{
		for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
		{
			if (IsValid(StreamingLevel)
				&& StreamingLevel->GetWorldAssetPackageFName() == PackageName)
			{
				return StreamingLevel;
			}
		}
		return nullptr;
	}
}

bool FConfigShowroomMapAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString PackageName(TEXT("/Game/Maps/L_ConfigShowroom"));
	FString Filename;
	TestTrue(
		TEXT("L_ConfigShowroom 已生成"),
		FPackageName::DoesPackageExist(PackageName, &Filename));
	if (Filename.IsEmpty())
	{
		return false;
	}

	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Filename);
	TestNotNull(TEXT("可加载展厅地图"), World);
	if (World == nullptr)
	{
		return false;
	}

	TestTrue(
		TEXT("地图覆盖使用展厅 GameMode"),
		World->GetWorldSettings()->DefaultGameMode == AConfigShowroomGameMode::StaticClass());
	TestEqual(
		TEXT("占位车辆唯一"),
		ConfigShowroomAutomation::CountByLabel<AConfiguratorVehicleActor>(
			World, TEXT("ConfiguratorPlaceholderVehicle_TEMP")),
		1);
	TestEqual(
		TEXT("六个兼容机位与四个独立语义近景机位完整"),
		ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCamera"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraRear"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraLeft"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraRight"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraInterior"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(
				World, TEXT("ShowroomCameraInteriorPassenger"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(
				World, TEXT("ShowroomCameraSeats"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(
				World, TEXT("ShowroomCameraWheel"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(
				World, TEXT("ShowroomCameraRearWheel"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(
				World, TEXT("ShowroomCameraEngineBay")),
		10);
	TSet<const AActor*> TaggedCameras;
	for (int32 CameraIndex = 0; CameraIndex < 6; ++CameraIndex)
	{
		const FName CameraTag(*FString::Printf(
			TEXT("Configurator.Camera.%d"), CameraIndex));
		TestEqual(
			*FString::Printf(TEXT("机位 %d 标签唯一"), CameraIndex),
			ConfigShowroomAutomation::CountByTag<ACameraActor>(
				World, CameraTag, TaggedCameras),
			1);
	}
	TestEqual(TEXT("六个机位标签分别指向不同相机"), TaggedCameras.Num(), 6);
	const struct
	{
		const TCHAR* CameraId;
		int32 LegacyIndex;
		bool bInterior;
	} SemanticCameras[] = {
		{TEXT("exterior"), 0, false},
		{TEXT("wheel"), INDEX_NONE, false},
		{TEXT("rear-wheel"), INDEX_NONE, false},
		{TEXT("engine-bay"), INDEX_NONE, false},
		{TEXT("side"), 2, false},
		{TEXT("driver"), 4, true},
		{TEXT("front-cabin"), 5, true},
		{TEXT("seat"), INDEX_NONE, true}
	};
	for (const auto& Expected : SemanticCameras)
	{
		const FName SemanticTag(*FString::Printf(
			TEXT("Configurator.Camera.%s"), Expected.CameraId));
		ACameraActor* Camera =
			ConfigShowroomAutomation::FindCameraByTag(World, SemanticTag);
		if (!TestNotNull(
			*FString::Printf(TEXT("语义机位 %s 存在"), Expected.CameraId),
			Camera))
		{
			continue;
		}
		if (Expected.LegacyIndex != INDEX_NONE)
		{
			TestTrue(
				*FString::Printf(TEXT("语义机位 %s 保留数字绑定"), Expected.CameraId),
				Camera->ActorHasTag(FName(*FString::Printf(
					TEXT("Configurator.Camera.%d"), Expected.LegacyIndex))));
		}
		TestEqual(
			*FString::Printf(TEXT("语义机位 %s 的 Interior companion tag"), Expected.CameraId),
			Camera->ActorHasTag(TEXT("Configurator.Camera.Interior")),
			Expected.bInterior);
	}

	const TCHAR* ExpectedCameraLabels[] = {
		TEXT("ShowroomCamera"), TEXT("ShowroomCameraRear"), TEXT("ShowroomCameraLeft"),
		TEXT("ShowroomCameraRight"), TEXT("ShowroomCameraInterior"),
		TEXT("ShowroomCameraInteriorPassenger"), TEXT("ShowroomCameraSeats"),
		TEXT("ShowroomCameraWheel"), TEXT("ShowroomCameraRearWheel"),
		TEXT("ShowroomCameraEngineBay")
	};
	const FTransform ExpectedCameraTransforms[] = {
		FTransform(FRotator(-14.0, -150.0, 0.0), FVector(920.0, 520.0, 310.0)),
		FTransform(FRotator(-12.0, 28.0, 0.0), FVector(-900.0, -470.0, 285.0)),
		FTransform(FRotator(-10.0, -90.0, 0.0), FVector(0.0, 880.0, 250.0)),
		FTransform(FRotator(-10.0, 90.0, 0.0), FVector(0.0, -880.0, 250.0)),
		FTransform(FRotator(-4.0, 0.0, 0.0), FVector(-15.0, -42.0, 122.0)),
		FTransform(FRotator(-6.0, -28.0, 0.0), FVector(-15.0, 48.0, 126.0)),
		FTransform(FRotator(-5.0, 180.0, 0.0), FVector(185.0, 0.0, 138.0)),
		FTransform(FRotator(-7.0, -90.0, 0.0), FVector(155.0, 410.0, 92.0)),
		FTransform(FRotator(-7.0, -90.0, 0.0), FVector(-235.0, 410.0, 92.0)),
		FTransform(FRotator(-18.0, 180.0, 0.0), FVector(360.0, 0.0, 245.0))
	};
	const float ExpectedCameraFovs[] = {
		42.0f, 42.0f, 42.0f, 42.0f, 64.0f, 76.0f, 64.0f, 38.0f,
		38.0f, 46.0f
	};
	for (int32 CameraIndex = 0; CameraIndex < UE_ARRAY_COUNT(ExpectedCameraLabels);
		++CameraIndex)
	{
		ACameraActor* Camera = ConfigShowroomAutomation::FindByLabel<ACameraActor>(
			World, ExpectedCameraLabels[CameraIndex]);
		if (!TestNotNull(
			*FString::Printf(TEXT("机位 %d 可按标签定位"), CameraIndex),
			Camera))
		{
			continue;
		}
		TestEqual(
			*FString::Printf(TEXT("机位 %d Label 正确"), CameraIndex),
			Camera->GetActorLabel(),
			FString(ExpectedCameraLabels[CameraIndex]));
		TestTrue(
			*FString::Printf(TEXT("机位 %d Transform 正确"), CameraIndex),
			Camera->GetActorTransform().Equals(
				ExpectedCameraTransforms[CameraIndex],
				0.1f));
		TestTrue(
			*FString::Printf(TEXT("机位 %d FOV 正确"), CameraIndex),
			FMath::IsNearlyEqual(
				Camera->GetCameraComponent()->FieldOfView,
				ExpectedCameraFovs[CameraIndex],
				0.1f));
	}
	ACameraActor* DriverCamera = ConfigShowroomAutomation::FindCameraByTag(
		World, TEXT("Configurator.Camera.4"));
	ACameraActor* PassengerCamera = ConfigShowroomAutomation::FindCameraByTag(
		World, TEXT("Configurator.Camera.5"));
	if (DriverCamera != nullptr && PassengerCamera != nullptr)
	{
		TestTrue(TEXT("Audi 驾驶位位于左侧 Y<0"),
			DriverCamera->GetActorLocation().Y < 0.0);
		TestTrue(TEXT("副驾位位于右侧 Y>0"),
			PassengerCamera->GetActorLocation().Y > 0.0);
		TestTrue(TEXT("主驾机位朝 Audi +X 车头"),
			DriverCamera->GetActorForwardVector().X > 0.99);
		TestTrue(TEXT("副驾全景机位朝车头且明显转向 Y<0 主驾侧"),
			PassengerCamera->GetActorForwardVector().X > 0.8
				&& PassengerCamera->GetActorForwardVector().Y < -0.3);
	}
	TestEqual(
		TEXT("流式环境控制器唯一"),
		ConfigShowroomAutomation::CountByLabel<AShowroomEnvironmentActor>(
			World, TEXT("ShowroomEnvironmentStreamController")),
		1);
	TestEqual(
		TEXT("主关卡不再包含地面"),
		ConfigShowroomAutomation::CountInPersistentLevel<AStaticMeshActor>(World),
		0);
	TestEqual(
		TEXT("主关卡不再包含方向光"),
		ConfigShowroomAutomation::CountInPersistentLevel<ADirectionalLight>(World),
		0);
	TestEqual(
		TEXT("主关卡不再包含补光"),
		ConfigShowroomAutomation::CountInPersistentLevel<APointLight>(World),
		0);
	TestEqual(
		TEXT("主关卡不再包含天空光"),
		ConfigShowroomAutomation::CountInPersistentLevel<ASkyLight>(World),
		0);

	const FName StudioPackage(TEXT("/Game/Maps/L_Lighting_Studio"));
	const FName OutdoorPackage(TEXT("/Game/Maps/L_Lighting_Outdoor"));
	ULevelStreaming* StudioStreaming =
		ConfigShowroomAutomation::FindStreamingLevel(World, StudioPackage);
	ULevelStreaming* OutdoorStreaming =
		ConfigShowroomAutomation::FindStreamingLevel(World, OutdoorPackage);
	TestNotNull(TEXT("主关卡引用 Studio 灯光流式关卡"), StudioStreaming);
	TestNotNull(TEXT("主关卡引用 Outdoor 灯光流式关卡"), OutdoorStreaming);
	if (StudioStreaming != nullptr)
	{
		TestTrue(TEXT("Studio 灯光关卡默认加载"), StudioStreaming->ShouldBeLoaded());
		TestTrue(TEXT("Studio 灯光关卡默认可见"), StudioStreaming->ShouldBeVisible());
	}
	if (OutdoorStreaming != nullptr)
	{
		TestFalse(TEXT("Outdoor 灯光关卡默认不加载"), OutdoorStreaming->ShouldBeLoaded());
		TestFalse(TEXT("Outdoor 灯光关卡默认不可见"), OutdoorStreaming->ShouldBeVisible());
	}
	AShowroomEnvironmentActor* Environment =
		ConfigShowroomAutomation::FindByLabel<AShowroomEnvironmentActor>(
			World, TEXT("ShowroomEnvironmentStreamController"));
	if (Environment != nullptr && StudioStreaming != nullptr && OutdoorStreaming != nullptr)
	{
		Environment->SetEnvironmentIndex(1);
		TestEqual(TEXT("环境控制器记录 Outdoor 索引"), Environment->GetEnvironmentIndex(), 1);
		TestFalse(TEXT("切换后 Studio 请求卸载"), StudioStreaming->ShouldBeLoaded());
		TestTrue(TEXT("切换后 Outdoor 请求加载"), OutdoorStreaming->ShouldBeLoaded());
	}

	const struct
	{
		const TCHAR* PackageName;
		const TCHAR* Prefix;
	} LightingMaps[] = {
		{TEXT("/Game/Maps/L_Lighting_Studio"), TEXT("Studio")},
		{TEXT("/Game/Maps/L_Lighting_Outdoor"), TEXT("Outdoor")}
	};
	for (const auto& LightingMap : LightingMaps)
	{
		FString LightingFilename;
		TestTrue(
			*FString::Printf(TEXT("%s 灯光关卡已生成"), LightingMap.Prefix),
			FPackageName::DoesPackageExist(LightingMap.PackageName, &LightingFilename));
		UWorld* LightingWorld = LightingFilename.IsEmpty()
			? nullptr
			: UEditorLoadingAndSavingUtils::LoadMap(LightingFilename);
		if (!TestNotNull(
			*FString::Printf(TEXT("%s 灯光关卡可加载"), LightingMap.Prefix),
			LightingWorld))
		{
			continue;
		}
		const FString FloorLabel =
			FString::Printf(TEXT("%sFloor_TEMP"), LightingMap.Prefix);
		const AStaticMeshActor* Floor =
			ConfigShowroomAutomation::FindByLabel<AStaticMeshActor>(
				LightingWorld, *FloorLabel);
		TestNotNull(
			*FString::Printf(TEXT("%s 灯光关卡独立包含地面"), LightingMap.Prefix),
			Floor);
		if (Floor != nullptr)
		{
			TestTrue(
				*FString::Printf(TEXT("%s 地面顶面位于 Z=0"), LightingMap.Prefix),
				FMath::IsNearlyEqual(Floor->GetActorLocation().Z, -10.0, 0.01)
					&& FMath::IsNearlyEqual(Floor->GetActorScale3D().Z, 0.2, 0.001));
		}
		TestEqual(
			*FString::Printf(TEXT("%s 方向光唯一"), LightingMap.Prefix),
			ConfigShowroomAutomation::CountInPersistentLevel<ADirectionalLight>(
				LightingWorld),
			1);
		TestEqual(
			*FString::Printf(TEXT("%s 天空光唯一"), LightingMap.Prefix),
			ConfigShowroomAutomation::CountInPersistentLevel<ASkyLight>(
				LightingWorld),
			1);
		TestEqual(
			*FString::Printf(TEXT("%s 补光唯一"), LightingMap.Prefix),
			ConfigShowroomAutomation::CountInPersistentLevel<APointLight>(
				LightingWorld),
			1);
	}
	return true;
}

#endif
