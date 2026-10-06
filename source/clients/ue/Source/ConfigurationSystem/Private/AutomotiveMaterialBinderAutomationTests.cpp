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
	const bool bBound = Binder->Bind(State, Library, Vehicle);
	TestTrue(TEXT("绑定明确车漆/内饰代理槽"), bBound);
	if (!bBound)
	{
		AddError(FString::Printf(
			TEXT("Binder 绑定失败：%s"),
			*Binder->GetLastError()));
		return false;
	}
	TestNotNull(TEXT("找到车漆代理槽"), Binder->GetPaintComponent());
	TestNotNull(TEXT("找到唯一内饰代理槽"), Binder->GetInteriorComponent());

	struct FInteriorCase
	{
		const TCHAR* OptionId;
		const TCHAR* FamilyId;
		UMaterialInterface* Expected;
	};
	const FInteriorCase InteriorCases[] = {
		{TEXT("door-middle-ultrasuede-black"), TEXT("ultrasuede"), Library->LoadInteriorMaterial(TEXT("ultrasuede"))},
		{TEXT("door-middle-alcantara"), TEXT("alcantara"), Library->LoadInteriorMaterial(TEXT("alcantara"))},
		{TEXT("door-middle-leather"), TEXT("leather"), Library->LoadInteriorMaterial(TEXT("leather"))},
		{TEXT("door-middle-microfiber"), TEXT("microfiber"), Library->LoadInteriorMaterial(TEXT("microfiber"))},
		{TEXT("door-middle-woven-wool"), TEXT("woven-wool"), Library->LoadInteriorMaterial(TEXT("woven-wool"))}
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
			TEXT("代理槽直接使用材质库中的对应 Master Material"),
			IsValid(Binder->GetInteriorComponent())
				&& Binder->GetInteriorComponent()->GetMaterial(0) == Case.Expected);
	}
	TMap<FString, FString> VariantSelections = State->GetSelections();
	VariantSelections.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		TEXT("door-middle-leather"));
	TMap<FString, FAutomotiveCustomization> VariantCustomizations =
		State->GetCustomizations();
	FAutomotiveCustomization VariantCustomization;
	VariantCustomization.Kind = EAutomotiveCustomizationKind::MaterialVariant;
	VariantCustomization.MaterialVariantId = TEXT("leather-p10-1217");
	VariantCustomizations.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		VariantCustomization);
	const FAutomotiveMaterialTransactionResult VariantResult =
		Binder->ApplyTransaction(VariantSelections, VariantCustomizations);
	TestTrue(TEXT("variantId 事务成功"), VariantResult.bSuccess);
	TestTrue(
		TEXT("variantId 直接命中阶段3物化 MI"),
		Binder->GetInteriorComponent()->GetMaterial(0)
			== Library->LoadVariantMaterial(TEXT("leather-p10-1217")));
	VariantCustomizations.Remove(UAutomotiveMaterialBinder::InteriorProxySurfaceId);
	TestTrue(
		TEXT("清除 variant 恢复 option 的材料族默认材质"),
		Binder->ApplyTransaction(
			VariantSelections,
			VariantCustomizations).bSuccess);
	TestTrue(
		TEXT("清除 variant 后槽恢复 leather family"),
		Binder->GetInteriorComponent()->GetMaterial(0)
			== Library->LoadInteriorMaterial(TEXT("leather")));
	const FString BeforeRejectedConfigurationId = State->GetConfigurationId();
	UMaterialInterface* BeforeRejectedMaterial =
		Binder->GetInteriorComponent()->GetMaterial(0);
	const TSoftObjectPtr<UMaterialInterface> SavedVariant =
		Library->Variants.FindRef(TEXT("leather-p10-1217"));
	Library->Variants.Remove(TEXT("leather-p10-1217"));
	TMap<FString, FAutomotiveCustomization> MissingAssetCustomizations =
		VariantCustomizations;
	MissingAssetCustomizations.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		VariantCustomization);
	const FAutomotiveMaterialTransactionResult MissingAssetResult =
		Binder->ApplyTransaction(VariantSelections, MissingAssetCustomizations);
	TestFalse(TEXT("缺失物化 MI 时拒绝整个事务"), MissingAssetResult.bSuccess);
	TestEqual(
		TEXT("缺失物化 MI 返回明确错误码"),
		MissingAssetResult.Code,
		FString(TEXT("MATERIAL_VARIANT_ASSET_MISSING")));
	TestEqual(
		TEXT("失败事务不修改配置身份"),
		State->GetConfigurationId(),
		BeforeRejectedConfigurationId);
	TestTrue(
		TEXT("失败事务不修改已显示材质"),
		Binder->GetInteriorComponent()->GetMaterial(0) == BeforeRejectedMaterial);
	Library->Variants.Add(TEXT("leather-p10-1217"), SavedVariant);

	const TArray<FString>* UnsupportedOptions =
		State->GetCatalogIndex().FindOptionIdsForSurface(TEXT("wheel-style"));
	TestTrue(
		TEXT("测试目录包含显式 capability 缺口 surface"),
		UnsupportedOptions != nullptr && UnsupportedOptions->Num() > 1);
	if (UnsupportedOptions != nullptr && UnsupportedOptions->Num() > 1)
	{
		TMap<FString, FString> UnsupportedSelections = State->GetSelections();
		const FString Current = UnsupportedSelections.FindRef(TEXT("wheel-style"));
		const FString* Replacement = UnsupportedOptions->FindByPredicate(
			[&Current](const FString& Value) { return Value != Current; });
		TestNotNull(TEXT("找到不同的未映射 option"), Replacement);
		UnsupportedSelections.Add(TEXT("wheel-style"), *Replacement);
		const FAutomotiveMaterialTransactionResult UnsupportedResult =
			Binder->ApplyTransaction(
				UnsupportedSelections,
				State->GetCustomizations());
		TestTrue(TEXT("显式缺口不破坏配置状态提交"), UnsupportedResult.bSuccess);
		TestEqual(
			TEXT("显式缺口返回稳定回执码"),
			UnsupportedResult.Code,
			FString(TEXT("APPLIED_WITH_UNSUPPORTED_SURFACES")));
		TestTrue(
			TEXT("回执明确列出未映射 surfaceId"),
			UnsupportedResult.UnsupportedSurfaceIds.Contains(TEXT("wheel-style")));
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
	const FAutomotiveMaterialTransactionResult PaintResult =
		Binder->ApplyTransaction(TransactionSelections, TransactionCustomizations);
	TestTrue(TEXT("Binder 原子提交 v2 selections/customizations"), PaintResult.bSuccess);
	TestTrue(
		TEXT("车漆事务回执包含 surface"),
		PaintResult.AppliedSurfaceIds
			== TArray<FString>({TEXT("exterior-body-cover")}));
	TestTrue(
		TEXT("车漆事务回执包含命名槽"),
		PaintResult.AppliedSlotIds
			== TArray<FName>({TEXT("CS_Validation_Paint")}));

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
	FAutomotivePaintCustomization RecoloredPaint = Paint;
	RecoloredPaint.ColorHex = TEXT("#112233");
	TransactionCustomizations.FindChecked(
		UAutomotiveMaterialBinder::PaintSurfaceId).Paint = RecoloredPaint;
	TestTrue(
		TEXT("同一车漆 parent 二次调色事务成功"),
		Binder->ApplyTransaction(
			TransactionSelections,
			TransactionCustomizations).bSuccess);
	TestTrue(
		TEXT("连续自定义车漆调色复用同一个 MID"),
		Binder->GetPaintMaterialInstance() == PaintInstance);

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
				TEXT("骨骼内饰槽使用已解析材质"),
				IsValid(SkeletalComponent->GetMaterial(InteriorIndex)));
		}
	}
	return true;
}

#endif
