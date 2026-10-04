#include "ContentPackProviderManager.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace AdminImportProviderTests
{
	bool CreatePack(
		const FString& Root,
		const FString& PackId,
		const FString& Version,
		const FString& CatalogVersion,
		const FString& ProviderType,
		FString& OutManifestPath,
		FString& OutPakPath)
	{
		const FString Directory = FPaths::Combine(Root, PackId + TEXT("-") + Version);
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			return false;
		}
		OutPakPath = FPaths::Combine(Directory, PackId + TEXT(".pak"));
		OutManifestPath = FPaths::Combine(Directory, TEXT("manifest.json"));
		if (!FFileHelper::SaveStringToFile(
			PackId + TEXT("@") + Version,
			*OutPakPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			return false;
		}

		FString Sha256;
		FString HashError;
		if (!FContentPackMountService::ComputeFileSha256(
			OutPakPath, Sha256, HashError))
		{
			return false;
		}
		const bool bLegacy = ProviderType.IsEmpty();
		const FString ProviderField = bLegacy
			? FString()
			: FString::Printf(TEXT(",\n  \"providerType\":\"%s\""), *ProviderType);
		const FString Manifest = FString::Printf(
			TEXT(R"JSON({
  "schemaVersion":"%s",
  "packId":"%s",
  "version":"%s",
  "catalogVersion":"%s"%s,
  "engineVersion":"5.8",
  "platform":"Win64",
  "mountPoint":"/Game/ContentPacks/%s/",
  "pak":{"fileName":"%s.pak","bytes":%lld,"sha256":"%s"},
  "primaryAssetIds":["CarMaterialOption:%s"]
})JSON"),
			bLegacy ? TEXT("1.0.0") : TEXT("2.0.0"),
			*PackId,
			*Version,
			*CatalogVersion,
			*ProviderField,
			*PackId,
			*PackId,
			IFileManager::Get().FileSize(*OutPakPath),
			*Sha256,
			*Version);
		return FFileHelper::SaveStringToFile(
			Manifest,
			*OutManifestPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void Configure(FContentPackProviderManager& Manager, int32& MountCalls)
	{
		Manager.SetInspectPakForTesting([](
			const FString& PakPath,
			FString& OutMountPoint,
			FString&)
		{
			OutMountPoint = FString::Printf(
				TEXT("/Game/ContentPacks/%s/"),
				*FPaths::GetBaseFilename(PakPath));
			return true;
		});
		Manager.SetMountPakForTesting([&MountCalls](const FString&, int32)
		{
			++MountCalls;
			return true;
		});
		Manager.SetScanMountedProviderForTesting([](
			const FContentPackManifest&,
			FString&)
		{
			return true;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAdminImportProviderManagementAutomationTest,
	"ConfigurationSystem.Editor.AdminImport.ProviderManagement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAdminImportProviderManagementAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Directory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("AdminImportProviderTests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Directory, true);

	FString CatalogV1Manifest;
	FString CatalogV1Pak;
	FString CatalogV2Manifest;
	FString CatalogV2Pak;
	FString LegacyManifest;
	FString LegacyPak;
	TestTrue(TEXT("创建 SC01 Provider v1"), AdminImportProviderTests::CreatePack(
		Directory, TEXT("sc01-materials"), TEXT("draft-1"),
		TEXT("sc01-draft-20260121"), TEXT("material"),
		CatalogV1Manifest, CatalogV1Pak));
	TestTrue(TEXT("创建 SC01 Provider v2"), AdminImportProviderTests::CreatePack(
		Directory, TEXT("sc01-materials"), TEXT("draft-2"),
		TEXT("sc01-draft-20260121"), TEXT("material"),
		CatalogV2Manifest, CatalogV2Pak));
	TestTrue(TEXT("创建旧 mvp-v1 包"), AdminImportProviderTests::CreatePack(
		Directory, TEXT("legacy-materials"), TEXT("legacy-1"),
		TEXT("mvp-v1"), FString(), LegacyManifest, LegacyPak));

	FContentPackProviderManagerPolicy Policy;
	Policy.EngineVersion = TEXT("5.8");
	Policy.RegistryPath = FPaths::Combine(Directory, TEXT("active-registry.json"));
	FContentPackProviderManager Manager(Policy);
	int32 MountCalls = 0;
	AdminImportProviderTests::Configure(Manager, MountCalls);

	bool bLegacy = false;
	const FContentPackMountResult CatalogPreflight = Manager.Preflight(
		EContentPackProviderType::Material,
		CatalogV1Manifest,
		CatalogV1Pak,
		&bLegacy);
	TestTrue(TEXT("SC01 material 预检通过"), CatalogPreflight.bPreflightPassed);
	TestFalse(TEXT("SC01 不走旧版兼容"), bLegacy);
	TestFalse(
		TEXT("所选 environment 与 manifest material 不匹配"),
		Manager.Preflight(
			EContentPackProviderType::Environment,
			CatalogV1Manifest,
			CatalogV1Pak).bPreflightPassed);

	const FContentPackMountResult LegacyPreflight = Manager.Preflight(
		EContentPackProviderType::Material,
		LegacyManifest,
		LegacyPak,
		&bLegacy);
	TestTrue(TEXT("旧 mvp-v1 保留预检兼容"), LegacyPreflight.bPreflightPassed);
	TestTrue(TEXT("旧包标记为仅兼容预检"), bLegacy);
	TestFalse(
		TEXT("旧 mvp-v1 不允许激活"),
		Manager.Activate(
			EContentPackProviderType::Material,
			LegacyManifest,
			LegacyPak).bActivated);
	TestEqual(TEXT("旧包未触发挂载"), MountCalls, 0);

	TestTrue(
		TEXT("激活 SC01 current"),
		Manager.Activate(
			EContentPackProviderType::Material,
			CatalogV1Manifest,
			CatalogV1Pak).bActivated);
	TestTrue(
		TEXT("激活新版并保留 previous"),
		Manager.Activate(
			EContentPackProviderType::Material,
			CatalogV2Manifest,
			CatalogV2Pak).bActivated);
	const FContentPackProviderSlot* Slot =
		Manager.FindActive(EContentPackProviderType::Material);
	TestTrue(TEXT("material slot 存在"), Slot != nullptr);
	if (Slot != nullptr)
	{
		TestEqual(TEXT("current 为 draft-2"), Slot->Current.Version, TEXT("draft-2"));
		TestTrue(TEXT("previous 存在"), Slot->Previous.IsSet());
	}

	TArray<FString> RollbackErrors;
	TestTrue(
		TEXT("回滚到 previous"),
		Manager.Rollback(EContentPackProviderType::Material, RollbackErrors));
	Slot = Manager.FindActive(EContentPackProviderType::Material);
	if (Slot != nullptr)
	{
		TestEqual(TEXT("回滚后 current 为 draft-1"), Slot->Current.Version, TEXT("draft-1"));
		TestTrue(TEXT("回滚后 previous 仍存在"), Slot->Previous.IsSet());
		if (Slot->Previous.IsSet())
		{
			TestEqual(TEXT("回滚后 previous 为 draft-2"),
				Slot->Previous->Version, TEXT("draft-2"));
		}
	}
	TestTrue(TEXT("registry 已持久化"), FPaths::FileExists(Policy.RegistryPath));
	TestEqual(TEXT("激活两次并回滚共挂载三次"), MountCalls, 3);

	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

#endif
