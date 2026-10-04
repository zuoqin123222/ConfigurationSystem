#if WITH_DEV_AUTOMATION_TESTS

#include "AutomotiveMaterialBinder.h"

#include "ConfiguratorVehicleActor.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "AutomotiveMaterialLibrary.h"
#include "AutomotiveCatalogData.h"
#include "AutomotiveConfigurationState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveMaterialBinderAutomationTest,
	"ConfigurationSystem.Runtime.AutomotiveMaterials.Binder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveMaterialBinderAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UAutomotiveCatalogData* Catalog = LoadObject<UAutomotiveCatalogData>(
		nullptr,
		TEXT("/Game/SC01/DA_SC01Catalog.DA_SC01Catalog"));
	UAutomotiveMaterialLibrary* Library = LoadObject<UAutomotiveMaterialLibrary>(
		nullptr,
		TEXT("/Game/SC01/Materials/DA_SC01MaterialLibrary.DA_SC01MaterialLibrary"));
	TestNotNull(TEXT("加载 v2 catalog"), Catalog);
	TestNotNull(TEXT("加载 AutomotiveMaterialLibrary"), Library);
	if (Catalog == nullptr || Library == nullptr)
	{
		return false;
	}

	UAutomotiveConfigurationState* State =
		NewObject<UAutomotiveConfigurationState>(GetTransientPackage());
	TestTrue(TEXT("初始化 v2 状态"), State->Initialize(Catalog));
	AConfiguratorVehicleActor* Vehicle =
		NewObject<AConfiguratorVehicleActor>(GetTransientPackage());
	UAutomotiveMaterialBinder* Binder =
		NewObject<UAutomotiveMaterialBinder>(GetTransientPackage());
	TestTrue(TEXT("绑定明确车漆/内饰代理槽"), Binder->Bind(State, Library, Vehicle));
	TestNotNull(TEXT("找到车漆代理槽"), Binder->GetPaintComponent());
	TestNotNull(TEXT("找到唯一内饰代理槽"), Binder->GetInteriorComponent());

	struct FInteriorCase
	{
		const TCHAR* OptionId;
		const TCHAR* FamilyId;
		UMaterialInterface* Expected;
	};
	const FInteriorCase InteriorCases[] = {
		{TEXT("door-middle-ultrasuede-black"), TEXT("ultrasuede"), Library->Ultrasuede.LoadSynchronous()},
		{TEXT("door-middle-alcantara"), TEXT("alcantara"), Library->Alcantara.LoadSynchronous()},
		{TEXT("door-middle-leather"), TEXT("leather"), Library->Leather.LoadSynchronous()},
		{TEXT("door-middle-microfiber"), TEXT("microfiber"), Library->Microfiber.LoadSynchronous()},
		{TEXT("door-middle-woven-wool"), TEXT("woven-wool"), Library->WovenWool.LoadSynchronous()}
	};
	for (const FInteriorCase& Case : InteriorCases)
	{
		TestTrue(
			*FString::Printf(TEXT("选择 %s"), Case.OptionId),
			State->SelectOption(UAutomotiveMaterialBinder::InteriorProxySurfaceId, Case.OptionId));
		TestEqual(
			*FString::Printf(TEXT("代理槽切换到 %s"), Case.FamilyId),
			Binder->GetAppliedInteriorFamilyId(),
			FString(Case.FamilyId));
		TestTrue(
			TEXT("代理槽使用材质库中的对应 Master Material"),
			Binder->GetInteriorComponent()->GetMaterial(0) == Case.Expected);
	}

	TestTrue(
		TEXT("选择自定义车漆 option"),
		State->SelectOption(
			UAutomotiveMaterialBinder::PaintSurfaceId,
			TEXT("body-cover-custom")));
	FAutomotivePaintCustomization Paint;
	Paint.ColorHex = TEXT("#336699");
	Paint.Metallic = 0.45;
	Paint.Roughness = 0.25;
	Paint.ClearCoat = 0.9;
	Paint.OrangePeel = 0.12;
	Paint.FlakeIntensity = 0.3;
	TestTrue(
		TEXT("提交 v2 自定义车漆"),
		State->SetPaintCustomization(UAutomotiveMaterialBinder::PaintSurfaceId, Paint));

	UMaterialInstanceDynamic* PaintInstance = Binder->GetPaintMaterialInstance();
	TestNotNull(TEXT("车身代理使用动态车漆实例"), PaintInstance);
	if (PaintInstance != nullptr)
	{
		const FLinearColor Expected =
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#336699")));
		TestTrue(
			TEXT("BaseColor 由 v2 自定义颜色驱动"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(
				Expected,
				0.001f));
		TestTrue(TEXT("Metallic 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("Metallic")),
				0.45f));
		TestTrue(TEXT("Roughness 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("Roughness")),
				0.25f));
		TestTrue(TEXT("ClearCoat 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("ClearCoat")),
				0.9f));
		TestTrue(TEXT("OrangePeel 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("OrangePeel")),
				0.12f));
		TestTrue(TEXT("FlakeIntensity 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("FlakeIntensity")),
				0.3f));
	}
	return true;
}

#endif
