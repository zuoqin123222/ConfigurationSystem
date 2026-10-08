#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOfflineVehicleMaterialAutomationTest,
	"ConfigurationSystem.Content.VehicleMaterials.Offline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FOfflineVehicleMaterialAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestNotNull(
		TEXT("离线包可加载 v4 骨骼整车"),
		LoadObject<USkeletalMesh>(
			nullptr,
			TEXT("/Game/Configurator/_ImportStaging/a5-dcc-v4-paint-seat-zup/"
				"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup."
				"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup")));
	TestNotNull(
		TEXT("离线包可加载 v4 完整动画序列"),
		LoadObject<UAnimSequence>(
			nullptr,
			TEXT("/Game/Configurator/_ImportStaging/a5-dcc-v4-paint-seat-zup/"
				"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup_Anim."
				"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup_Anim")));

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
