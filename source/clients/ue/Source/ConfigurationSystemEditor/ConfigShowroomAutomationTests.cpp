#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraActor.h"
#include "ConfigShowroomGameMode.h"
#include "ConfiguratorVehicleActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"

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
		TEXT("默认相机唯一"),
		ConfigShowroomAutomation::CountByLabel<ACameraActor>(
			World, TEXT("ShowroomCamera")),
		1);
	TestEqual(
		TEXT("临时地台唯一"),
		ConfigShowroomAutomation::CountByLabel<AStaticMeshActor>(
			World, TEXT("ShowroomFloor_TEMP")),
		1);
	TestEqual(
		TEXT("主光唯一"),
		ConfigShowroomAutomation::CountByLabel<ADirectionalLight>(
			World, TEXT("ShowroomKeyLight_TEMP")),
		1);
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
