#include "Sc01V2CatalogData.h"

#include "Sc01V2Domain.h"

const FPrimaryAssetType USc01V2CatalogData::PrimaryAssetType(TEXT("Sc01V2Catalog"));
const FName USc01V2CatalogData::DefaultAssetName(TEXT("DA_SC01Catalog"));

FPrimaryAssetId USc01V2CatalogData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

bool USc01V2CatalogData::BuildCatalogIndex(
	Sc01V2::FCatalogIndex& OutCatalog,
	Sc01V2::FError& OutError) const
{
	return OutCatalog.LoadJson(CatalogJson, OutError);
}
