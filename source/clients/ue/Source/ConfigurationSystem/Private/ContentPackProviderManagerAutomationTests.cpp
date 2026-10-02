#include "ContentPackProviderManager.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ContentPackProviderTests
{
	bool CreatePack(
		const FString& Root,
		const FString& PackId,
		const FString& Version,
		const FString& ProviderType,
		const FString& PrimaryAssetId,
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
		const FString Payload = PackId + TEXT("@") + Version;
		if (!FFileHelper::SaveStringToFile(
			Payload,
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
		const FString Manifest = FString::Printf(
			TEXT(R"JSON({
  "schemaVersion":"2.0.0",
  "packId":"%s",
  "version":"%s",
  "catalogVersion":"sc01-draft-20260121",
  "providerType":"%s",
  "engineVersion":"5.8",
  "platform":"Win64",
  "mountPoint":"/Game/ContentPacks/%s/",
  "pak":{"fileName":"%s.pak","bytes":%lld,"sha256":"%s"},
  "primaryAssetIds":["%s"]
})JSON"),
			*PackId,
			*Version,
			*ProviderType,
			*PackId,
			*PackId,
			IFileManager::Get().FileSize(*OutPakPath),
			*Sha256,
			*PrimaryAssetId);
		return FFileHelper::SaveStringToFile(
			Manifest,
			*OutManifestPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void Configure(
		FContentPackProviderManager& Manager,
		int32& MountCalls,
		int32& ScanCalls,
		bool& bFailScan)
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
		Manager.SetScanMountedProviderForTesting(
			[&ScanCalls, &bFailScan](
				const FContentPackManifest& Manifest,
				FString& OutError)
			{
				++ScanCalls;
				if (!Manifest.MountPoint.StartsWith(TEXT("/Game/ContentPacks/")))
				{
					OutError = TEXT("扫描越过受限 mount root。");
					return false;
				}
				if (bFailScan)
				{
					OutError = TEXT("测试注入的 PrimaryAsset 扫描失败。");
					return false;
				}
				return true;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FContentPackProviderManagerAutomationTest,
	"ConfigurationSystem.Runtime.ContentPack.ProviderActivationRecoveryRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FContentPackProviderManagerAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Directory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("ContentPackProviderTests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*Directory, true);

	FString MaterialV1Manifest;
	FString MaterialV1Pak;
	FString MaterialV2Manifest;
	FString MaterialV2Pak;
	FString EnvironmentManifest;
	FString EnvironmentPak;
	FString VehicleManifest;
	FString VehiclePak;
	TestTrue(TEXT("创建 material v1"), ContentPackProviderTests::CreatePack(
		Directory, TEXT("sc01-materials"), TEXT("draft-1"), TEXT("material"),
		TEXT("CarMaterialOption:sc01-paint"),
		MaterialV1Manifest, MaterialV1Pak));
	TestTrue(TEXT("创建 material v2"), ContentPackProviderTests::CreatePack(
		Directory, TEXT("sc01-materials"), TEXT("draft-2"), TEXT("material"),
		TEXT("CarMaterialOption:sc01-paint"),
		MaterialV2Manifest, MaterialV2Pak));
	TestTrue(TEXT("创建 environment"), ContentPackProviderTests::CreatePack(
		Directory, TEXT("sc01-environment"), TEXT("draft-1"), TEXT("environment"),
		TEXT("ConfiguratorEnvironment:sc01-showroom"),
		EnvironmentManifest, EnvironmentPak));
	TestTrue(TEXT("创建 vehicle"), ContentPackProviderTests::CreatePack(
		Directory, TEXT("sc01-vehicle"), TEXT("draft-1"), TEXT("vehicle"),
		TEXT("VehicleDefinition:sc01"),
		VehicleManifest, VehiclePak));

	FContentPackProviderManagerPolicy Policy;
	Policy.EngineVersion = TEXT("5.8");
	Policy.RegistryPath = FPaths::Combine(Directory, TEXT("active-registry.json"));
	int32 MountCalls = 0;
	int32 ScanCalls = 0;
	bool bFailScan = false;
	FContentPackProviderManager Manager(Policy);
	ContentPackProviderTests::Configure(Manager, MountCalls, ScanCalls, bFailScan);

	const FContentPackProviderActivationResult MaterialV1 = Manager.Activate(
		EContentPackProviderType::Material, MaterialV1Manifest, MaterialV1Pak);
	TestTrue(TEXT("material v1 激活"), MaterialV1.bActivated);
	const FContentPackProviderActivationResult MaterialV2 = Manager.Activate(
		EContentPackProviderType::Material, MaterialV2Manifest, MaterialV2Pak);
	TestTrue(TEXT("material v2 同类激活"), MaterialV2.bActivated);
	const FContentPackProviderSlot* Material =
		Manager.FindActive(EContentPackProviderType::Material);
	TestTrue(TEXT("material 有活动记录"), Material != nullptr);
	if (Material != nullptr)
	{
		TestEqual(TEXT("当前版本是 v2"), Material->Current.Version, TEXT("draft-2"));
		TestTrue(TEXT("保留上一版本"), Material->Previous.IsSet());
		if (Material->Previous.IsSet())
		{
			TestEqual(
				TEXT("上一版本是 v1"),
				Material->Previous->Version,
				TEXT("draft-1"));
		}
	}

	TArray<FString> RollbackErrors;
	TestTrue(
		TEXT("material 回滚成功"),
		Manager.Rollback(EContentPackProviderType::Material, RollbackErrors));
	Material = Manager.FindActive(EContentPackProviderType::Material);
	if (Material != nullptr)
	{
		TestEqual(TEXT("回滚后当前为 v1"), Material->Current.Version, TEXT("draft-1"));
		TestTrue(TEXT("回滚后仍保留被替换版本"), Material->Previous.IsSet());
	}

	bFailScan = true;
	const FContentPackProviderActivationResult RejectedEnvironment = Manager.Activate(
		EContentPackProviderType::Environment, EnvironmentManifest, EnvironmentPak);
	TestFalse(TEXT("扫描失败不激活 environment"), RejectedEnvironment.bActivated);
	TestTrue(
		TEXT("扫描失败不污染 active"),
		Manager.FindActive(EContentPackProviderType::Environment) == nullptr);

	bFailScan = false;
	TestTrue(
		TEXT("environment 激活"),
		Manager.Activate(
			EContentPackProviderType::Environment,
			EnvironmentManifest,
			EnvironmentPak).bActivated);
	TestTrue(
		TEXT("vehicle 激活"),
		Manager.Activate(
			EContentPackProviderType::Vehicle,
			VehicleManifest,
			VehiclePak).bActivated);
	TestTrue(
		TEXT("active registry 已持久化"),
		IFileManager::Get().FileExists(*Policy.RegistryPath));
	TestFalse(
		TEXT("原子临时文件未残留"),
		IFileManager::Get().FileExists(*(Policy.RegistryPath + TEXT(".tmp"))));

	int32 RestoreMountCalls = 0;
	int32 RestoreScanCalls = 0;
	FContentPackProviderManager Restored(Policy);
	ContentPackProviderTests::Configure(
		Restored, RestoreMountCalls, RestoreScanCalls, bFailScan);
	TArray<FString> RestoreErrors;
	TestTrue(TEXT("重启恢复三类 Provider"), Restored.RestoreActiveProviders(RestoreErrors));
	TestEqual(TEXT("恢复挂载三类 current"), RestoreMountCalls, 3);
	const FContentPackProviderSlot* RestoredMaterial =
		Restored.FindActive(EContentPackProviderType::Material);
	TestTrue(TEXT("恢复 material"), RestoredMaterial != nullptr);
	if (RestoredMaterial != nullptr)
	{
		TestEqual(
			TEXT("恢复回滚后的 current"),
			RestoredMaterial->Current.Version,
			TEXT("draft-1"));
		TestTrue(TEXT("恢复 previous"), RestoredMaterial->Previous.IsSet());
	}

	const FString RegistryDirectory = FPaths::Combine(Directory, TEXT("registry-as-dir"));
	IFileManager::Get().MakeDirectory(*RegistryDirectory, true);
	FContentPackProviderManagerPolicy FailingPolicy = Policy;
	FailingPolicy.RegistryPath = RegistryDirectory;
	FContentPackProviderManager AtomicFailure(FailingPolicy);
	ContentPackProviderTests::Configure(
		AtomicFailure, MountCalls, ScanCalls, bFailScan);
	const FContentPackProviderActivationResult AtomicRejected = AtomicFailure.Activate(
		EContentPackProviderType::Vehicle, VehicleManifest, VehiclePak);
	TestFalse(TEXT("registry 原子替换失败不激活"), AtomicRejected.bActivated);
	TestTrue(
		TEXT("registry 写失败不污染内存 active"),
		AtomicFailure.FindActive(EContentPackProviderType::Vehicle) == nullptr);

	TestTrue(TEXT("发生挂载调用"), MountCalls >= 6);
	TestTrue(TEXT("每次候选挂载后扫描"), ScanCalls >= 6);
	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	return true;
}

#endif
