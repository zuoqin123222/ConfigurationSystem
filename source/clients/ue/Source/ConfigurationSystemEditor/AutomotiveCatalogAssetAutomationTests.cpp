#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/AssetManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "AutomotiveCatalogData.h"
#include "AutomotiveCatalogDomain.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace AutomotiveCatalogAssetAutomation
{
	constexpr TCHAR PackageName[] = TEXT("/Game/SC01/DA_SC01Catalog");
	constexpr TCHAR AssetName[] = TEXT("DA_SC01Catalog");

	FString SharedCatalogFilename()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(),
			TEXT("../../../contracts/fixtures/sc01.catalog.draft.v2.json")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveCatalogPrimaryAssetConsistencyTest,
	"ConfigurationSystem.Editor.AutomotiveCatalog.AssetConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveCatalogPrimaryAssetConsistencyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace AutomotiveCatalogAssetAutomation;

	FString SharedCatalogJson;
	TestTrue(
		TEXT("读取当前共享车型目录 JSON"),
		FFileHelper::LoadFileToString(
			SharedCatalogJson,
			*SharedCatalogFilename()));
	UAutomotiveCatalogData* Asset = LoadObject<UAutomotiveCatalogData>(
		nullptr,
		TEXT("/Game/SC01/DA_SC01Catalog.DA_SC01Catalog"));
	TestNotNull(TEXT("加载已提交 DA_SC01Catalog"), Asset);
	if (Asset == nullptr || SharedCatalogJson.IsEmpty())
	{
		return false;
	}
	TestEqual(
		TEXT("资产内嵌 JSON 与当前共享 JSON 逐字一致"),
		Asset->CatalogJson,
		SharedCatalogJson);

	AutomotiveCatalog::FCatalogIndex Catalog;
	AutomotiveCatalog::FError Error;
	TestTrue(
		TEXT("已提交资产内嵌目录可通过 Runtime 校验"),
		Catalog.LoadJson(Asset->CatalogJson, Error));
	if (!Catalog.IsValid())
	{
		AddError(Error.Code + TEXT(": ") + Error.Message);
		return false;
	}
	TestEqual(TEXT("已提交资产包含当前 175 个 option"),
		Catalog.GetCatalog().Options.Num(), 175);
	TestEqual(TEXT("已提交资产包含当前 36 个默认选择"),
		Catalog.GetCatalog().DefaultSelections.Num(), 36);
	TestEqual(
		TEXT("铭牌默认显式为不选装"),
		Catalog.GetCatalog().DefaultSelections.FindRef(TEXT("nameplate")),
		FString(TEXT("nameplate-none")));
	const AutomotiveCatalog::FOption* NameplateNone =
		Catalog.FindOption(TEXT("nameplate-none"));
	TestTrue(
		TEXT("nameplate-none 存在且属于铭牌 surface"),
		NameplateNone != nullptr
			&& NameplateNone->SurfaceId == TEXT("nameplate"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveGenerateCatalogPrimaryAssetTest,
	"ConfigurationSystem.Editor.AutomotiveCatalog.GenerateCatalogPrimaryAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveGenerateCatalogPrimaryAssetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace AutomotiveCatalogAssetAutomation;

	FString CatalogJson;
	TestTrue(
		TEXT("读取共享车型目录 v2 JSON"),
		FFileHelper::LoadFileToString(CatalogJson, *SharedCatalogFilename()));
	if (CatalogJson.IsEmpty())
	{
		return false;
	}

	AutomotiveCatalog::FCatalogIndex Catalog;
	AutomotiveCatalog::FError Error;
	TestTrue(TEXT("共享 catalog 可通过 Runtime 校验"), Catalog.LoadJson(CatalogJson, Error));
	if (!Catalog.IsValid())
	{
		AddError(Error.Code + TEXT(": ") + Error.Message);
		return false;
	}

	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
	UAutomotiveCatalogData* Asset =
		LoadObject<UAutomotiveCatalogData>(nullptr, *ObjectPath);
	const bool bCreated = Asset == nullptr;
	if (bCreated)
	{
		UPackage* Package = CreatePackage(PackageName);
		Asset = NewObject<UAutomotiveCatalogData>(
			Package,
			AssetName,
			RF_Public | RF_Standalone);
	}
	TestNotNull(TEXT("创建或加载 DA_SC01Catalog"), Asset);
	if (Asset == nullptr)
	{
		return false;
	}

	Asset->Modify();
	Asset->CatalogJson = CatalogJson;
	Asset->PostEditChange();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Asset);
	}

	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const FString AssetFilename = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension());
	TestTrue(
		TEXT("保存 /Game/SC01/DA_SC01Catalog"),
		UPackage::SavePackage(Package, Asset, *AssetFilename, SaveArgs));

	FAssetRegistryModule& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistry.Get().ScanPathsSynchronous({TEXT("/Game/SC01")}, true);
	UAssetManager::Get().ScanPathsForPrimaryAssets(
		UAutomotiveCatalogData::PrimaryAssetType,
		{TEXT("/Game/SC01")},
		UAutomotiveCatalogData::StaticClass(),
		false,
		false,
		true);

	TestEqual(TEXT("内嵌 JSON 字节数"), Asset->CatalogJson.Len(), CatalogJson.Len());
	TestEqual(TEXT("Primary Asset 类型"), Asset->GetPrimaryAssetId().PrimaryAssetType,
		UAutomotiveCatalogData::PrimaryAssetType);
	TestEqual(TEXT("Primary Asset 名称"), Asset->GetPrimaryAssetId().PrimaryAssetName,
		UAutomotiveCatalogData::DefaultAssetName);
	return true;
}

#endif
