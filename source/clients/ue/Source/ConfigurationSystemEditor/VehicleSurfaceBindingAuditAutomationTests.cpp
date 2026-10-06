#include "VehicleSurfaceBindingAudit.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehicleSurfaceBindingAuditAutomationTest,
	"ConfigurationSystem.Editor.AdminImport.SurfaceBindingSlotLodAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVehicleSurfaceBindingAuditAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString FixturePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("../../../contracts/fixtures/vehicle-surface-binding.valid.json")));
	FVehicleSurfaceBindingContract ParsedContract;
	TArray<FString> ParseErrors;
	TestTrue(
		TEXT("读取 SC01 40 surface 正向 fixture"),
		FVehicleSurfaceBindingAudit::LoadContractFile(
			FixturePath,
			ParsedContract,
			ParseErrors));
	TestEqual(TEXT("UE 契约解析得到 40 个 surface"), ParsedContract.SurfaceIds.Num(), 40);

	FString InvalidJson;
	TestTrue(TEXT("读取 fixture 文本"), FFileHelper::LoadFileToString(InvalidJson, *FixturePath));
	InvalidJson = InvalidJson.Replace(
		TEXT("\"surfaceId\":\"exterior-body-cover\""),
		TEXT("\"surfaceId\":\"wheel-material\""),
		ESearchCase::CaseSensitive);
	FVehicleSurfaceBindingContract InvalidContract;
	ParseErrors.Reset();
	TestFalse(
		TEXT("UE 契约解析拒绝稳定顺序变化"),
		FVehicleSurfaceBindingAudit::LoadContractJson(
			InvalidJson,
			InvalidContract,
			ParseErrors));

	FVehicleSurfaceBindingContract Contract;
	Contract.VehicleId = TEXT("sc01");
	Contract.CatalogVersion = TEXT("sc01-draft-20261007");
	Contract.ModelVersion = TEXT("test-model");
	for (int32 Index = 0; Index < 40; ++Index)
	{
		Contract.SurfaceIds.Add(FString::Printf(TEXT("surface-%02d"), Index));
		Contract.MaterialSlotIds.Add(FName(*FString::Printf(TEXT("sc01_surface_%02d"), Index)));
	}

	FVehicleSurfaceBindingMeshSnapshot Valid;
	Valid.MaterialSlotIds = Contract.MaterialSlotIds;
	Valid.MaterialSlotIds.Add(TEXT("fixed_glass"));
	Valid.LodMaterialSlotIds.Add(Contract.MaterialSlotIds);
	Valid.LodMaterialSlotIds.Add(Contract.MaterialSlotIds);
	Valid.LodMaterialSlotIds[1].Add(Contract.MaterialSlotIds[0]);
	const FVehicleSurfaceBindingAuditResult ValidResult =
		FVehicleSurfaceBindingAudit::AuditSnapshot(Contract, Valid);
	TestTrue(TEXT("全部 40 个 slot 在每级 LOD 中存在时通过"), ValidResult.bPassed);
	TestEqual(TEXT("记录 40 个 surface"), ValidResult.SurfaceCount, 40);
	TestEqual(TEXT("记录两级 LOD"), ValidResult.LodCount, 2);

	FVehicleSurfaceBindingMeshSnapshot Missing = Valid;
	Missing.LodMaterialSlotIds[1].Remove(Contract.MaterialSlotIds[39]);
	const FVehicleSurfaceBindingAuditResult MissingResult =
		FVehicleSurfaceBindingAudit::AuditSnapshot(Contract, Missing);
	TestFalse(TEXT("任一 LOD 缺失 surface slot 时拒绝"), MissingResult.bPassed);
	TestTrue(
		TEXT("报告指明 LOD 与缺失 slot"),
		MissingResult.Issues.ContainsByPredicate(
			[](const FString& Issue)
			{
				return Issue.Contains(TEXT("LOD1"))
					&& Issue.Contains(TEXT("sc01_surface_39"));
			}));

	FVehicleSurfaceBindingMeshSnapshot Duplicate = Valid;
	Duplicate.MaterialSlotIds.Add(Contract.MaterialSlotIds[2]);
	const FVehicleSurfaceBindingAuditResult DuplicateResult =
		FVehicleSurfaceBindingAudit::AuditSnapshot(Contract, Duplicate);
	TestFalse(TEXT("SkeletalMesh 槽名重复时拒绝"), DuplicateResult.bPassed);
	TestTrue(
		TEXT("报告重复槽"),
		DuplicateResult.Issues.ContainsByPredicate(
			[](const FString& Issue)
			{
				return Issue.Contains(TEXT("material slot 重复"));
			}));
	return true;
}

#endif
