#if WITH_DEV_AUTOMATION_TESTS

#include "AutomotiveMaterialAssetGenerator.h"

#include "AutomotiveMaterialLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "UObject/MetaData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveMaterialAssetGenerationAutomationTest,
	"ConfigurationSystem.Editor.AutomotiveMaterials.MaterializeCatalogVariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveVehicleMaterialAssignmentAutomationTest,
	"ConfigurationSystem.Editor.AutomotiveMaterials.VehicleHasNoEngineDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveMaterialAssetGenerationAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	FAutomotiveMaterialGenerationResult First;
	TestTrue(
		TEXT("UE5.8 从仓库 SubstrateMaterials 物化 352 个 MI"),
		FAutomotiveMaterialAssetGenerator::Generate(First));
	for (const FString& Error : First.Errors)
	{
		AddError(Error);
	}
	TestEqual(TEXT("材料族母材质映射数"), First.FamilyParentPaths.Num(), 17);
	TestEqual(TEXT("catalog MI 数量"), First.Variants.Num(), 352);
	TestNotNull(TEXT("生成可 Cook 材质库"), First.Library);
	if (!First.Succeeded())
	{
		return false;
	}
	for (const TPair<FString, FString>& Pair : First.FamilyParentPaths)
	{
		TestTrue(
			*FString::Printf(TEXT("%s 仅复用仓库 SubstrateMaterials"), *Pair.Key),
			Pair.Value.StartsWith(TEXT("/Game/SubstrateMaterials/")));
	}
	TestEqual(TEXT("材质库包含 17 个母材质"), First.Library->FamilyParents.Num(), 17);
	TestEqual(TEXT("材质库包含 352 个 MI"), First.Library->Variants.Num(), 352);

	int32 WovenWoolCount = 0;
	for (UMaterialInstanceConstant* Variant : First.Variants)
	{
		TestNotNull(TEXT("MI 有 Parent"), Variant != nullptr ? Variant->Parent.Get() : nullptr);
		TestTrue(
			TEXT("资产类型是 Material Instance 而不是 Master Material"),
			Variant != nullptr
				&& Variant->GetPathName().StartsWith(
					TEXT("/Game/SC01/Materials/Variants/")));
		if (Variant != nullptr
			&& Variant->GetPathName().Contains(TEXT("/woven-wool/")))
		{
			++WovenWoolCount;
			const FString Scale = Variant->GetOutermost()->GetMetaData().GetValue(
				Variant, TEXT("SC01.PatternScale"));
			const FString Rotation = Variant->GetOutermost()->GetMetaData().GetValue(
				Variant, TEXT("SC01.PatternRotationDegrees"));
			TestFalse(TEXT("羊毛记录花纹尺度"), Scale.IsEmpty());
			TestEqual(TEXT("羊毛按缩略图方向不旋转"), Rotation, FString(TEXT("0")));
		}
	}
	TestEqual(TEXT("羊毛花纹 MI 数量"), WovenWoolCount, 16);

	const int32 FirstCreatedCount = First.CreatedAssetCount;
	FAutomotiveMaterialGenerationResult Second;
	TestTrue(
		TEXT("重复物化成功"),
		FAutomotiveMaterialAssetGenerator::Generate(Second));
	for (const FString& Error : Second.Errors)
	{
		AddError(Error);
	}
	TestEqual(TEXT("重复物化不创建新资产"), Second.CreatedAssetCount, 0);
	TestEqual(TEXT("重复物化仍为 352 个 MI"), Second.Variants.Num(), 352);
	TestTrue(TEXT("第一次确实创建了资产或刷新已有资产"),
		FirstCreatedCount > 0 || First.UpdatedAssetCount > 0);

	const FString AuditPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir(),
		TEXT("MaterialAudit/sc01-material-stage3-audit.json"));
	TestTrue(
		TEXT("写出 UE5.8 母材质审计证据"),
		FAutomotiveMaterialAssetGenerator::WriteAuditReport(AuditPath, Second));
	TestTrue(TEXT("审计报告存在"), FPaths::FileExists(AuditPath));
	return true;
}

bool FAutomotiveVehicleMaterialAssignmentAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	USkeletalMesh* Vehicle = LoadObject<USkeletalMesh>(
		nullptr,
		TEXT("/Game/Configurator/_ImportStaging/a5-dcc-v4-paint-seat-zup/"
			"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup."
			"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup"));
	TestNotNull(TEXT("加载当前完整骨骼车辆"), Vehicle);
	if (!IsValid(Vehicle))
	{
		return false;
	}

	const TArray<FSkeletalMaterial>& Materials = Vehicle->GetMaterials();
	TestEqual(TEXT("骨骼车辆固定包含 48 个材质槽"), Materials.Num(), 48);
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		const FSkeletalMaterial& Material = Materials[Index];
		const FString Path = IsValid(Material.MaterialInterface)
			? Material.MaterialInterface->GetPathName()
			: FString();
		TestTrue(
			*FString::Printf(
				TEXT("槽 %d/%s 使用项目材质"),
				Index,
				*Material.MaterialSlotName.ToString()),
			IsValid(Material.MaterialInterface)
				&& !Path.Contains(TEXT("DefaultMaterial"))
				&& !Path.Contains(TEXT("WorldGridMaterial")));
	}

	const auto FindMaterial = [&Materials](const FName SlotName)
		-> UMaterialInterface*
	{
		const FSkeletalMaterial* Match = Materials.FindByPredicate(
			[SlotName](const FSkeletalMaterial& Material)
			{
				return Material.MaterialSlotName == SlotName;
			});
		return Match != nullptr ? Match->MaterialInterface : nullptr;
	};
	UMaterialInterface* ConfigurablePaint =
		FindMaterial(TEXT("sc01_exterior_body_cover"));
	UMaterialInterface* BasePaint =
		FindMaterial(TEXT("CS_Validation_Paint"));
	TestTrue(
		TEXT("两个车漆槽均以官方 A5 车漆为资产基线"),
		IsValid(ConfigurablePaint)
			&& IsValid(BasePaint)
			&& ConfigurablePaint->GetFName() == TEXT("MI_CarPaint_Cherry")
			&& BasePaint->GetFName() == TEXT("MI_CarPaint_Cherry"));
	return true;
}

#endif
