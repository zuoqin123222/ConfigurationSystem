#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ConfiguratorVehicleActor.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOfflineVehicleMaterialAutomationTest,
	"ConfigurationSystem.Content.VehicleMaterials.Offline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FOfflineVehicleMaterialAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TPair<FName, FName> ExpectedMaterials[] = {
		{TEXT("CS_Validation_Paint"), TEXT("M_SC01_Vehicle_Paint")},
		{TEXT("CS_Validation_Metal"), TEXT("M_SC01_Vehicle_Metal")},
		{TEXT("CS_Validation_Rubber"), TEXT("M_SC01_Vehicle_Rubber")},
		{TEXT("CS_Validation_Interior"), TEXT("M_SC01_Vehicle_Interior")},
		{TEXT("CS_Validation_Plastic"), TEXT("M_SC01_Vehicle_Plastic")},
		{TEXT("CS_Validation_Glass"), TEXT("M_SC01_Vehicle_Glass")},
		{TEXT("CS_Validation_LightClear"), TEXT("M_SC01_Vehicle_LightClear")},
		{TEXT("CS_Validation_LightRed"), TEXT("M_SC01_Vehicle_LightRed")},
	};
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(
		nullptr,
		TEXT("/Game/Configurator/_ImportStaging/audi-a5-rigged-v2/"
			"automotive-configurator-audi-a5-rigged-v2."
			"automotive-configurator-audi-a5-rigged-v2"));
	if (!TestNotNull(TEXT("骨骼网格可离线加载"), Mesh))
	{
		return false;
	}
	for (const TPair<FName, FName>& Expected : ExpectedMaterials)
	{
		const int32 SlotIndex = Mesh->GetMaterials().IndexOfByPredicate(
			[&Expected](const FSkeletalMaterial& Material)
			{
				return Material.MaterialSlotName == Expected.Key;
			});
		if (!TestTrue(
			*FString::Printf(TEXT("存在命名槽 %s"), *Expected.Key.ToString()),
			SlotIndex != INDEX_NONE))
		{
			continue;
		}
		const UMaterialInterface* Material =
			Mesh->GetMaterials()[SlotIndex].MaterialInterface;
		TestTrue(
			*FString::Printf(TEXT("%s 绑定可追踪 SC01 材质"), *Expected.Key.ToString()),
			Material != nullptr
				&& Material->GetFName() == Expected.Value
				&& Material->GetPathName().StartsWith(
					TEXT("/Game/SC01/Materials/VehicleProxy/")));
		TestEqual(
			*FString::Printf(TEXT("%s 运行时映射名称一致"), *Expected.Key.ToString()),
			AConfiguratorVehicleActor::GetOfflineMaterialNameForSkeletalSlot(Expected.Key),
			Expected.Value);
	}
	return true;
}

#endif
