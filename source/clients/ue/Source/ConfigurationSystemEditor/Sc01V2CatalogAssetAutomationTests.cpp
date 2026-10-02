#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/AssetManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Sc01V2CatalogData.h"
#include "Sc01V2Domain.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace Sc01V2CatalogAssetAutomation
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
	FSc01V2GenerateCatalogPrimaryAssetTest,
	"ConfigurationSystem.Editor.SC01V2.GenerateCatalogPrimaryAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSc01V2GenerateCatalogPrimaryAssetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace Sc01V2CatalogAssetAutomation;

	FString CatalogJson;
	TestTrue(
		TEXT("读取共享 SC01 v2 catalog JSON"),
		FFileHelper::LoadFileToString(CatalogJson, *SharedCatalogFilename()));
	if (CatalogJson.IsEmpty())
	{
		return false;
	}

	Sc01V2::FCatalogIndex Catalog;
	Sc01V2::FError Error;
	TestTrue(TEXT("共享 catalog 可通过 Runtime 校验"), Catalog.LoadJson(CatalogJson, Error));
	if (!Catalog.IsValid())
	{
		AddError(Error.Code + TEXT(": ") + Error.Message);
		return false;
	}

	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
	USc01V2CatalogData* Asset =
		LoadObject<USc01V2CatalogData>(nullptr, *ObjectPath);
	const bool bCreated = Asset == nullptr;
	if (bCreated)
	{
		UPackage* Package = CreatePackage(PackageName);
		Asset = NewObject<USc01V2CatalogData>(
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
		USc01V2CatalogData::PrimaryAssetType,
		{TEXT("/Game/SC01")},
		USc01V2CatalogData::StaticClass(),
		false,
		false,
		true);

	TestEqual(TEXT("内嵌 JSON 字节数"), Asset->CatalogJson.Len(), CatalogJson.Len());
	TestEqual(TEXT("Primary Asset 类型"), Asset->GetPrimaryAssetId().PrimaryAssetType,
		USc01V2CatalogData::PrimaryAssetType);
	TestEqual(TEXT("Primary Asset 名称"), Asset->GetPrimaryAssetId().PrimaryAssetName,
		USc01V2CatalogData::DefaultAssetName);
	return true;
}

#endif
