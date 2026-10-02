#include "ContentPackMountService.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContentPackMountAutomationTest,
	"ConfigurationSystem.Runtime.ContentPack.SafePreflightAndMount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContentPackMountAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Directory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("ContentPackTests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString PakPath = FPaths::Combine(Directory, TEXT("automation-pack.pak"));
	const FString ManifestPath = FPaths::Combine(Directory, TEXT("manifest.json"));
	TestTrue(
		TEXT("创建 pak 测试载荷"),
		FFileHelper::SaveStringToFile(
			TEXT("ConfigurationSystem content pack automation"),
			*PakPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FString Sha256;
	FString HashError;
	TestTrue(
		TEXT("计算 pak SHA-256"),
		FContentPackMountService::ComputeFileSha256(PakPath, Sha256, HashError));
	const int64 PakBytes = IFileManager::Get().FileSize(*PakPath);

	auto MakeManifest = [PakBytes, &Sha256](
		const FString& SchemaVersion,
		const FString& CatalogVersion,
		const FString& EngineVersion,
		const FString& Platform,
		const FString& MountPoint,
		const FString& DeclaredSha,
		const FString& PrimaryAssetIds)
	{
		return FString::Printf(
			TEXT(R"JSON({
  "schemaVersion":"%s",
  "packId":"automation-pack",
  "version":"pack-1",
  "catalogVersion":"%s",
  "engineVersion":"%s",
  "platform":"%s",
  "mountPoint":"%s",
  "pak":{"fileName":"automation-pack.pak","bytes":%lld,"sha256":"%s"},
  "primaryAssetIds":%s
})JSON"),
			*SchemaVersion,
			*CatalogVersion,
			*EngineVersion,
			*Platform,
			*MountPoint,
			PakBytes,
			*DeclaredSha,
			*PrimaryAssetIds);
	};

	FContentPackMountPolicy Policy;
	Policy.CatalogVersion = TEXT("catalog-1");
	Policy.EngineVersion = TEXT("5.8");
	int32 MountCalls = 0;
	FContentPackMountService Service(Policy);
	Service.SetInspectPakForTesting([](
		const FString&,
		FString& OutMountPoint,
		FString&)
	{
		OutMountPoint = TEXT("/Game/ContentPacks/automation-pack/");
		return true;
	});
	Service.SetMountPakForTesting([&MountCalls](const FString&, int32)
	{
		++MountCalls;
		return true;
	});

	const FString ValidIds =
		TEXT("[\"CarMaterialOption:paint-red\",\"CarMaterialOption:wheel-sport\"]");
	const FString ValidManifest = MakeManifest(
		TEXT("1.0.0"),
		TEXT("catalog-1"),
		TEXT("5.8"),
		TEXT("Win64"),
		TEXT("/Game/ContentPacks/automation-pack/"),
		Sha256,
		ValidIds);
	TestTrue(
		TEXT("写入有效 manifest"),
		FFileHelper::SaveStringToFile(
			ValidManifest,
			*ManifestPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	const FContentPackMountResult Mounted =
		Service.PreflightAndMount(ManifestPath, PakPath);
	TestTrue(TEXT("有效内容包通过预检"), Mounted.bPreflightPassed);
	TestTrue(TEXT("有效内容包被挂载"), Mounted.bMounted);
	TestEqual(TEXT("有效内容包只挂载一次"), MountCalls, 1);

	const FContentPackMountResult DuplicateAcrossPacks =
		Service.PreflightAndMount(ManifestPath, PakPath);
	TestFalse(TEXT("已挂载 PrimaryAssetId 冲突被拒绝"), DuplicateAcrossPacks.bPreflightPassed);
	TestFalse(TEXT("冲突内容包不挂载"), DuplicateAcrossPacks.bMounted);
	TestEqual(TEXT("冲突未调用底层挂载"), MountCalls, 1);

	struct FInvalidCase
	{
		const TCHAR* Name;
		FString Json;
	};
	const TArray<FInvalidCase> InvalidCases{
		{TEXT("未知 schemaVersion"), MakeManifest(
			TEXT("3.0.0"), TEXT("catalog-1"), TEXT("5.8"), TEXT("Win64"),
			TEXT("/Game/ContentPacks/automation-pack/"), Sha256, ValidIds)},
		{TEXT("catalog 不匹配"), MakeManifest(
			TEXT("1.0.0"), TEXT("catalog-2"), TEXT("5.8"), TEXT("Win64"),
			TEXT("/Game/ContentPacks/automation-pack/"), Sha256, ValidIds)},
		{TEXT("engine 不匹配"), MakeManifest(
			TEXT("1.0.0"), TEXT("catalog-1"), TEXT("5.7"), TEXT("Win64"),
			TEXT("/Game/ContentPacks/automation-pack/"), Sha256, ValidIds)},
		{TEXT("platform 不匹配"), MakeManifest(
			TEXT("1.0.0"), TEXT("catalog-1"), TEXT("5.8"), TEXT("Linux"),
			TEXT("/Game/ContentPacks/automation-pack/"), Sha256, ValidIds)},
		{TEXT("mountPoint 越界"), MakeManifest(
			TEXT("1.0.0"), TEXT("catalog-1"), TEXT("5.8"), TEXT("Win64"),
			TEXT("/Game/Developers/Unsafe/"), Sha256, ValidIds)},
		{TEXT("SHA-256 不匹配"), MakeManifest(
			TEXT("1.0.0"), TEXT("catalog-1"), TEXT("5.8"), TEXT("Win64"),
			TEXT("/Game/ContentPacks/automation-pack/"),
			FString::ChrN(64, TEXT('0')), ValidIds)},
		{TEXT("包内 PrimaryAssetId 重复"), MakeManifest(
			TEXT("1.0.0"), TEXT("catalog-1"), TEXT("5.8"), TEXT("Win64"),
			TEXT("/Game/ContentPacks/automation-pack/"), Sha256,
			TEXT("[\"CarMaterialOption:paint-red\",\"CarMaterialOption:paint-red\"]"))}
	};

	for (const FInvalidCase& Invalid : InvalidCases)
	{
		FContentPackMountService RejectionService(Policy);
		int32 RejectedMountCalls = 0;
		RejectionService.SetInspectPakForTesting([](
			const FString&,
			FString& OutMountPoint,
			FString&)
		{
			OutMountPoint = TEXT("/Game/ContentPacks/automation-pack/");
			return true;
		});
		RejectionService.SetMountPakForTesting([&RejectedMountCalls](const FString&, int32)
		{
			++RejectedMountCalls;
			return true;
		});
		TestTrue(
			Invalid.Name,
			FFileHelper::SaveStringToFile(
				Invalid.Json,
				*ManifestPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
		const FContentPackMountResult Rejected =
			RejectionService.PreflightAndMount(ManifestPath, PakPath);
		TestFalse(Invalid.Name, Rejected.bPreflightPassed);
		TestFalse(TEXT("预检失败不挂载"), Rejected.bMounted);
		TestEqual(TEXT("预检失败未调用底层挂载"), RejectedMountCalls, 0);
	}

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

#endif
