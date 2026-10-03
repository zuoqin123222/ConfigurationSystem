#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ConfigShowroomGameMode.h"
#include "ConfiguratorVehicleActor.h"
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
		TEXT("四个车外与驾驶位、副驾位六个产品机位完整"),
		ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCamera"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraRear"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraLeft"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraRight"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(World, TEXT("ShowroomCameraInterior"))
			+ ConfigShowroomAutomation::CountByLabel<ACameraActor>(
				World, TEXT("ShowroomCameraInteriorPassenger")),
		6);
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

	const TCHAR* ExpectedCameraLabels[] = {
		TEXT("ShowroomCamera"), TEXT("ShowroomCameraRear"), TEXT("ShowroomCameraLeft"),
		TEXT("ShowroomCameraRight"), TEXT("ShowroomCameraInterior"),
		TEXT("ShowroomCameraInteriorPassenger")
	};
	const FTransform ExpectedCameraTransforms[] = {
		FTransform(FRotator(-14.0, -150.0, 0.0), FVector(920.0, 520.0, 310.0)),
		FTransform(FRotator(-12.0, 28.0, 0.0), FVector(-900.0, -470.0, 285.0)),
		FTransform(FRotator(-10.0, -90.0, 0.0), FVector(0.0, 880.0, 250.0)),
		FTransform(FRotator(-10.0, 90.0, 0.0), FVector(0.0, -880.0, 250.0)),
		FTransform(FRotator(-4.0, 0.0, 0.0), FVector(-15.0, -42.0, 122.0)),
		FTransform(FRotator(-4.0, 0.0, 0.0), FVector(-15.0, 42.0, 122.0))
	};
	for (int32 CameraIndex = 0; CameraIndex < UE_ARRAY_COUNT(ExpectedCameraLabels);
		++CameraIndex)
	{
		const FName CameraTag(*FString::Printf(
			TEXT("Configurator.Camera.%d"), CameraIndex));
		ACameraActor* Camera =
			ConfigShowroomAutomation::FindCameraByTag(World, CameraTag);
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
				CameraIndex >= 4 ? 64.0f : 42.0f,
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
		TestTrue(TEXT("主副驾目视方向均朝 Audi +X 车头"),
			DriverCamera->GetActorForwardVector().X > 0.99
				&& PassengerCamera->GetActorForwardVector().X > 0.99);
	}
	TestEqual(
		TEXT("双环境控制器唯一"),
		ConfigShowroomAutomation::CountByLabel<AShowroomEnvironmentActor>(
			World, TEXT("ShowroomDualEnvironment_TEMP")),
		1);
	TestEqual(
		TEXT("临时地台唯一"),
		ConfigShowroomAutomation::CountByLabel<AStaticMeshActor>(
			World, TEXT("ShowroomFloor_TEMP")),
		1);
	if (const AStaticMeshActor* Floor =
		ConfigShowroomAutomation::FindByLabel<AStaticMeshActor>(
			World, TEXT("ShowroomFloor_TEMP")))
	{
		TestTrue(TEXT("地台顶面与车辆轮胎落地点同为 Z=0"),
			FMath::IsNearlyEqual(Floor->GetActorLocation().Z, -10.0, 0.01)
				&& FMath::IsNearlyEqual(Floor->GetActorScale3D().Z, 0.2, 0.001));
	}
	TestEqual(
		TEXT("补光唯一"),
		ConfigShowroomAutomation::CountByLabel<APointLight>(
			World, TEXT("ShowroomFillLight_TEMP")),
		1);
	TestEqual(
		TEXT("天空光唯一"),
		ConfigShowroomAutomation::CountByLabel<ASkyLight>(
			World, TEXT("ShowroomSkyLight_TEMP")),
		1);
	return true;
}

#endif
