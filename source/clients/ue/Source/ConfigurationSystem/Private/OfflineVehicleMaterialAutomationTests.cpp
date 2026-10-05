#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOfflineVehicleMaterialAutomationTest,
	"ConfigurationSystem.Content.VehicleMaterials.Offline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FOfflineVehicleMaterialAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TCHAR* ExpectedAssets[] = {
		TEXT("BodyMesh"),
		TEXT("DoorMesh_FL"),
		TEXT("DoorMesh_FR"),
		TEXT("FrameMesh"),
		TEXT("HoodMesh"),
		TEXT("InteriorMesh"),
		TEXT("TrunkMesh"),
		TEXT("WheelMesh_FL"),
		TEXT("WheelMesh_FR"),
		TEXT("WheelMesh_RL"),
		TEXT("WheelMesh_RR"),
	};
	for (const TCHAR* AssetName : ExpectedAssets)
	{
		const FString ObjectPath = FString::Printf(
			TEXT("/Game/Configurator/AuthorizedAudiA5/%s.%s"),
			AssetName,
			AssetName);
		TestNotNull(
			*FString::Printf(TEXT("主分支 A5 分件 %s 可加载"), AssetName),
			LoadObject<UObject>(nullptr, *ObjectPath));
	}

	const TCHAR* ExpectedMaterials[] = {
		TEXT("M_A5_Paint"),
		TEXT("M_A5_Metal"),
		TEXT("M_A5_Rubber"),
		TEXT("M_A5_Interior"),
		TEXT("M_A5_Plastic"),
		TEXT("M_A5_Glass"),
		TEXT("M_A5_LightClear"),
		TEXT("M_A5_LightRed"),
	};
	for (const TCHAR* MaterialName : ExpectedMaterials)
	{
		const FString ObjectPath = FString::Printf(
			TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/%s.%s"),
			MaterialName,
			MaterialName);
		TestNotNull(
			*FString::Printf(TEXT("主分支 A5 材质 %s 可加载"), MaterialName),
			LoadObject<UObject>(nullptr, *ObjectPath));
	}
	return true;
}

#endif
