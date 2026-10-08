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
		TEXT("/Game/References/AutomotiveMats/Materials/Exterior/CarPaint/MI_CarPaint_Cherry.MI_CarPaint_Cherry"),
		TEXT("/Game/References/AutomotiveMats/Materials/Exterior/Metal/MI_Metal_Chrome_01.MI_Metal_Chrome_01"),
		TEXT("/Game/References/AutomotiveMats/Materials/Exterior/Rubber/MI_Rubber_Rough.MI_Rubber_Rough"),
		TEXT("/Game/References/AutomotiveMats/Materials/Interior/Leather/MI_Leather_Grey.MI_Leather_Grey"),
		TEXT("/Game/References/AutomotiveMats/Materials/Exterior/Plastic/MI_Plastic_Satin.MI_Plastic_Satin"),
		TEXT("/Game/References/AutomotiveMats/Materials/Exterior/Glass/MI_Glass_Windows.MI_Glass_Windows"),
		TEXT("/Game/References/AutomotiveMats/Materials/Exterior/Glass/MI_Glass_Headlights.MI_Glass_Headlights"),
		TEXT("/Game/References/AutomotiveMats/Materials/Exterior/Glass/MI_Glass_Tailights.MI_Glass_Tailights"),
	};
	for (const TCHAR* MaterialPath : ExpectedMaterials)
	{
		TestNotNull(
			*FString::Printf(TEXT("授权官方 A5 材质 %s 可加载"), MaterialPath),
			LoadObject<UObject>(nullptr, MaterialPath));
	}
	return true;
}

#endif
