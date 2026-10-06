#if WITH_DEV_AUTOMATION_TESTS

#include "AutomotiveMaterialAssetGenerator.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "AutomotiveMaterialLibrary.h"

namespace AutomotiveMaterialAssetAutomation
{
	TSet<FName> ParameterNames(const UMaterial* Material)
	{
		TSet<FName> Result;
		for (UMaterialExpression* Expression :
			Material->GetExpressionCollection().Expressions)
		{
			if (const UMaterialExpressionScalarParameter* Scalar =
				Cast<UMaterialExpressionScalarParameter>(Expression))
			{
				Result.Add(Scalar->ParameterName);
			}
			else if (const UMaterialExpressionVectorParameter* Vector =
				Cast<UMaterialExpressionVectorParameter>(Expression))
			{
				Result.Add(Vector->ParameterName);
			}
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveMaterialAssetGenerationAutomationTest,
	"ConfigurationSystem.Editor.AutomotiveMaterials.GenerateIdempotently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveMaterialAssetGenerationAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace AutomotiveMaterialAssetAutomation;

	FAutomotiveMaterialGenerationResult First;
	TestTrue(TEXT("第一次生成六个 Master Material 与材质库"), FAutomotiveMaterialAssetGenerator::Generate(First));
	for (const FString& Error : First.Errors)
	{
		AddError(Error);
	}
	TestEqual(TEXT("Master Material 数量"), First.Materials.Num(), 6);
	TestNotNull(TEXT("生成 AutomotiveMaterialLibrary"), First.Library);
	if (!First.Succeeded())
	{
		return false;
	}

	TMap<FString, int32> ExpressionCounts;
	for (UMaterial* Material : First.Materials)
	{
		ExpressionCounts.Add(
			Material->GetPathName(),
			UMaterialEditingLibrary::GetNumMaterialExpressions(Material));
	}

	FAutomotiveMaterialGenerationResult Second;
	TestTrue(TEXT("第二次生成成功"), FAutomotiveMaterialAssetGenerator::Generate(Second));
	TestEqual(TEXT("重复生成不创建新资产"), Second.CreatedAssetCount, 0);
	TestEqual(TEXT("重复生成原位刷新七个资产"), Second.UpdatedAssetCount, 7);
	TestEqual(TEXT("重复生成仍恰好六个材质"), Second.Materials.Num(), 6);
	for (UMaterial* Material : Second.Materials)
	{
		TestEqual(
			*FString::Printf(TEXT("%s 节点数不累积"), *Material->GetName()),
			UMaterialEditingLibrary::GetNumMaterialExpressions(Material),
			ExpressionCounts.FindRef(Material->GetPathName()));
	}

	const TSet<FName> PaintParameters = ParameterNames(Second.Materials[0]);
	for (const FName Required : {
		FName(TEXT("BaseColor")),
		FName(TEXT("Metallic")),
		FName(TEXT("Roughness")),
		FName(TEXT("ClearCoat")),
		FName(TEXT("ClearCoatRoughness")),
		FName(TEXT("OrangePeel")),
		FName(TEXT("FlakeIntensity")) })
	{
		TestTrue(
			*FString::Printf(TEXT("车漆包含参数 %s"), *Required.ToString()),
			PaintParameters.Contains(Required));
	}

	for (int32 Index = 1; Index < Second.Materials.Num(); ++Index)
	{
		const TSet<FName> InteriorParameters = ParameterNames(Second.Materials[Index]);
		for (const FName Required : {
			FName(TEXT("BaseColor")),
			FName(TEXT("Roughness")),
			FName(TEXT("MicrostructureScale")),
			FName(TEXT("MicrostructureStrength")),
			FName(TEXT("FuzzAmount")),
			FName(TEXT("FuzzExponent")) })
		{
			TestTrue(
				*FString::Printf(
					TEXT("%s 包含程序微结构/绒毛参数 %s"),
					*Second.Materials[Index]->GetName(),
					*Required.ToString()),
				InteriorParameters.Contains(Required));
		}
	}

	TestEqual(
		TEXT("材质库 Primary Asset 类型"),
		Second.Library->GetPrimaryAssetId().PrimaryAssetType,
		UAutomotiveMaterialLibrary::PrimaryAssetType);
	TestEqual(
		TEXT("材质库 Primary Asset 名称"),
		Second.Library->GetPrimaryAssetId().PrimaryAssetName,
		UAutomotiveMaterialLibrary::DefaultAssetName);
	TestNotNull(TEXT("材质库车漆引用有效"), Second.Library->CarPaint.LoadSynchronous());
	TestNotNull(TEXT("材质库 Alcantara 引用有效"), Second.Library->Alcantara.LoadSynchronous());
	TestNotNull(TEXT("材质库 Ultrasuede 引用有效"), Second.Library->Ultrasuede.LoadSynchronous());
	TestNotNull(TEXT("材质库牛皮引用有效"), Second.Library->Leather.LoadSynchronous());
	TestNotNull(TEXT("材质库超纤引用有效"), Second.Library->Microfiber.LoadSynchronous());
	TestNotNull(TEXT("材质库织物引用有效"), Second.Library->WovenWool.LoadSynchronous());
	return true;
}

#endif
