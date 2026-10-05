#if WITH_DEV_AUTOMATION_TESTS

#include "AutomotiveMaterialBinder.h"

#include "ConfiguratorVehicleActor.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
			TEXT("代理槽动态实例使用材质库中的对应 Master Material"),
			IsValid(Binder->GetInteriorMaterialInstance())
				&& Binder->GetInteriorMaterialInstance()->Parent == Case.Expected);
	}

	UMaterialInstanceDynamic* PaintInstance = Binder->GetPaintMaterialInstance();
	TestNotNull(TEXT("默认车漆创建动态实例"), PaintInstance);
	if (PaintInstance != nullptr)
	{
		const FLinearColor Red =
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#A61D24")));
		TestTrue(
			TEXT("标准红色写入车漆 BaseColor"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(
				Red,
				0.001f));
	}
	TestTrue(
		TEXT("选择标准银色"),
		State->SelectOption(
			UAutomotiveMaterialBinder::PaintSurfaceId,
			TEXT("body-cover-silver")));
	if (PaintInstance != nullptr)
	{
		const FLinearColor Silver =
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#BFC3C7")));
		TestTrue(
			TEXT("标准银色写入车漆 BaseColor"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(
				Silver,
				0.001f));
	}

	const AutomotiveCatalog::FMaterialVariant* DisplayColorVariant =
		State->GetCatalogIndex().FindMaterialVariant(TEXT("alcantara-p2-2911"));
	TestTrue(
		TEXT("色卡 ui.sortColorHex 进入 UE 目录模型"),
		DisplayColorVariant != nullptr
			&& DisplayColorVariant->DisplayColorHex.IsSet()
			&& DisplayColorVariant->DisplayColorHex.GetValue() == TEXT("#DFDBBE"));

	FAutomotivePaintCustomization Paint;
	Paint.ColorHex = TEXT("#336699");
	Paint.Metallic = 0.45;
	Paint.Roughness = 0.25;
	Paint.ClearCoat = 0.9;
	Paint.OrangePeel = 0.12;
	Paint.FlakeIntensity = 0.3;
	TMap<FString, FString> TransactionSelections = State->GetSelections();
	TransactionSelections.Add(
		UAutomotiveMaterialBinder::PaintSurfaceId,
		TEXT("body-cover-custom"));
	TMap<FString, FAutomotiveCustomization> TransactionCustomizations =
		State->GetCustomizations();
	FAutomotiveCustomization PaintCustomization;
	PaintCustomization.Kind = EAutomotiveCustomizationKind::Paint;
	PaintCustomization.Paint = Paint;
	TransactionCustomizations.Add(
		UAutomotiveMaterialBinder::PaintSurfaceId,
		PaintCustomization);
	TestTrue(
		TEXT("ApplyTransaction 原子提交 v2 selections/customizations"),
		State->ApplyTransaction(TransactionSelections, TransactionCustomizations));

	PaintInstance = Binder->GetPaintMaterialInstance();
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

	TestTrue(
		TEXT("真实骨骼车辆可由目录完成初始化"),
		Vehicle->ConfigureAnimationFromCatalog(
			State->GetCatalogIndex().GetCatalog()));
	TestTrue(TEXT("重新绑定真实骨骼命名槽"), Binder->Bind(State, Library, Vehicle));
	USkeletalMeshComponent* SkeletalComponent =
		Cast<USkeletalMeshComponent>(Binder->GetPaintComponent());
	TestNotNull(TEXT("车漆绑定到可见骨骼车辆"), SkeletalComponent);
	TestTrue(
		TEXT("内饰与车漆绑定到同一骨骼车辆"),
		Binder->GetInteriorComponent() == SkeletalComponent);
	if (SkeletalComponent != nullptr)
	{
		const int32 PaintIndex =
			SkeletalComponent->GetMaterialIndex(TEXT("CS_Validation_Paint"));
		const int32 InteriorIndex =
			SkeletalComponent->GetMaterialIndex(TEXT("CS_Validation_Interior"));
		TestTrue(TEXT("骨骼车漆命名槽有效"), PaintIndex != INDEX_NONE);
		TestTrue(TEXT("骨骼内饰命名槽有效"), InteriorIndex != INDEX_NONE);
		if (PaintIndex != INDEX_NONE)
		{
			TestTrue(
				TEXT("骨骼车漆槽使用动态实例"),
				SkeletalComponent->GetMaterial(PaintIndex)
					== Binder->GetPaintMaterialInstance());
		}
		if (InteriorIndex != INDEX_NONE)
		{
			TestTrue(
				TEXT("骨骼内饰槽使用动态实例"),
				SkeletalComponent->GetMaterial(InteriorIndex)
					== Binder->GetInteriorMaterialInstance());
		}
	}
	return true;
}

#endif
