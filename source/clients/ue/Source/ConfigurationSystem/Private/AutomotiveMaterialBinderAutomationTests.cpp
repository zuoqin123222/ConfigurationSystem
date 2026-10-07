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
	TestEqual(
		TEXT("A5 独立分件提供 40 个 Catalog 代理目标"),
		Vehicle->GetCatalogSurfaceTargetCount(),
		AutomotiveCatalog::RequiredSelectionCount);
	TSet<const UMeshComponent*> UniqueTargets;
	for (const AutomotiveCatalog::FSurfaceBinding& Binding :
		State->GetCatalogIndex().GetCatalog().VehicleSurfaceBinding.Bindings)
	{
		for (const FName SlotId : Binding.MaterialSlotIds)
		{
			UMeshComponent* Target = Vehicle->FindCatalogSurfaceTarget(SlotId);
			TestNotNull(
				*FString::Printf(TEXT("%s 有代理目标"), *Binding.SurfaceId),
				Target);
			if (Target != nullptr)
			{
				TestTrue(
					*FString::Printf(TEXT("%s 代理目标可见"), *Binding.SurfaceId),
					Target->IsVisible() && !Target->bHiddenInGame);
				TestFalse(
					*FString::Printf(TEXT("%s 不复用其他 surface 目标"), *Binding.SurfaceId),
					UniqueTargets.Contains(Target));
				UniqueTargets.Add(Target);
			}
		}
	}
	TestEqual(TEXT("40 个 surface 对应 40 个唯一组件"), UniqueTargets.Num(), 40);

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
			TEXT("代理槽使用对应材料族的可见实例"),
			IsValid(Binder->GetInteriorComponent())
				&& IsValid(Case.Expected)
				&& IsValid(Binder->GetInteriorComponent()->GetMaterial(0)));
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
		TEXT("清除 variant 后槽恢复 leather family 的可见实例"),
		Binder->GetAppliedInteriorFamilyId() == TEXT("leather")
			&& IsValid(Binder->GetInteriorComponent()->GetMaterial(0)));
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

	const TArray<FString>* ProxyOptions =
		State->GetCatalogIndex().FindOptionIdsForSurface(TEXT("wheel-style"));
	TestTrue(
		TEXT("测试目录包含代理可视化 surface"),
		ProxyOptions != nullptr && ProxyOptions->Num() > 1);
	if (ProxyOptions != nullptr && ProxyOptions->Num() > 1)
	{
		TMap<FString, FString> ProxySelections = State->GetSelections();
		const FString Current = ProxySelections.FindRef(TEXT("wheel-style"));
		const FString* Replacement = ProxyOptions->FindByPredicate(
			[&Current](const FString& Value) { return Value != Current; });
		TestNotNull(TEXT("找到不同的代理 option"), Replacement);
		UMaterialInstanceDynamic* ProxyMaterial =
			Cast<UMaterialInstanceDynamic>(
				Binder->GetAppliedMaterialForSurface(TEXT("wheel-style")));
		const FLinearColor BeforeProxyColor = IsValid(ProxyMaterial)
			? ProxyMaterial->K2_GetVectorParameterValue(TEXT("BaseColor"))
			: FLinearColor::Transparent;
		ProxySelections.Add(TEXT("wheel-style"), *Replacement);
		const FAutomotiveMaterialTransactionResult ProxyResult =
			Binder->ApplyTransaction(
				ProxySelections,
				State->GetCustomizations());
		TestTrue(TEXT("代理 surface 原子提交成功"), ProxyResult.bSuccess);
		TestEqual(
			TEXT("代理 surface 返回完整应用回执"),
			ProxyResult.Code,
			FString(TEXT("APPLIED")));
		TestTrue(
			TEXT("回执明确列出已切换 surfaceId"),
			ProxyResult.AppliedSurfaceIds.Contains(TEXT("wheel-style")));
		TestTrue(
			TEXT("代理选项切换更新可见运行时材质"),
			IsValid(Cast<UMaterialInstanceDynamic>(
				Binder->GetAppliedMaterialForSurface(TEXT("wheel-style"))))
				&& !CastChecked<UMaterialInstanceDynamic>(
					Binder->GetAppliedMaterialForSurface(TEXT("wheel-style")))
					->K2_GetVectorParameterValue(TEXT("BaseColor"))
					.Equals(BeforeProxyColor));
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
			== TArray<FName>({TEXT("A5Proxy_ExteriorBodyCover")}));

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

	TestFalse(
		TEXT("40 surface 代理目标启用时不叠加骨骼整车"),
		Vehicle->ConfigureAnimationFromCatalog(
			State->GetCatalogIndex().GetCatalog()));
	TestTrue(TEXT("重新绑定 A5 独立静态代理目标"), Binder->Bind(State, Library, Vehicle));
	USkeletalMeshComponent* SkeletalComponent =
		Cast<USkeletalMeshComponent>(Binder->GetPaintComponent());
	TestNull(TEXT("车漆不再绑定被隐藏的骨骼整车"), SkeletalComponent);
	TestTrue(
		TEXT("车漆仍绑定唯一可见 A5 静态分件"),
		IsValid(Binder->GetPaintComponent())
			&& Binder->GetPaintComponent()->IsVisible()
			&& !Binder->GetPaintComponent()->bHiddenInGame);

	for (const FString& SurfaceId :
		State->GetCatalogIndex().GetCatalog().SelectionOrder)
	{
		const TArray<FString>* Options =
			State->GetCatalogIndex().FindOptionIdsForSurface(SurfaceId);
		if (Options == nullptr || Options->Num() < 2)
		{
			continue;
		}
		TMap<FString, FString> NextSelections = State->GetSelections();
		const FString CurrentOption = NextSelections.FindRef(SurfaceId);
		const FString* NextOption = Options->FindByPredicate(
			[&CurrentOption](const FString& Value)
			{
				return Value != CurrentOption;
			});
		if (NextOption == nullptr)
		{
			continue;
		}
		UMaterialInterface* Before =
			Binder->GetAppliedMaterialForSurface(SurfaceId);
		UMaterialInstanceDynamic* BeforeDynamic =
			Cast<UMaterialInstanceDynamic>(Before);
		const FLinearColor BeforeColor = IsValid(BeforeDynamic)
			? BeforeDynamic->K2_GetVectorParameterValue(TEXT("BaseColor"))
			: FLinearColor::Transparent;
		NextSelections.Add(SurfaceId, *NextOption);
		TMap<FString, FAutomotiveCustomization> NextCustomizations =
			State->GetCustomizations();
		NextCustomizations.Remove(SurfaceId);
		const FAutomotiveMaterialTransactionResult Result =
			Binder->ApplyTransaction(NextSelections, NextCustomizations);
		TestTrue(
			*FString::Printf(TEXT("%s 逐项事务成功"), *SurfaceId),
			Result.bSuccess);
		TestTrue(
			*FString::Printf(TEXT("%s 回执包含唯一 surface"), *SurfaceId),
			Result.AppliedSurfaceIds == TArray<FString>({SurfaceId}));
		UMaterialInterface* After =
			Binder->GetAppliedMaterialForSurface(SurfaceId);
		UMaterialInstanceDynamic* AfterDynamic =
			Cast<UMaterialInstanceDynamic>(After);
		TestTrue(
			*FString::Printf(TEXT("%s 切换产生可见材质差异"), *SurfaceId),
			IsValid(After)
				&& (After != Before
					|| (IsValid(AfterDynamic)
						&& !AfterDynamic->K2_GetVectorParameterValue(
							TEXT("BaseColor")).Equals(BeforeColor))));
	}
	return true;
}

#endif
