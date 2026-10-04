#include "AutomotiveCatalogData.h"

#include "AutomotiveCatalogDomain.h"

const FPrimaryAssetType UAutomotiveCatalogData::PrimaryAssetType(
	TEXT("AutomotiveCatalog"));
const FName UAutomotiveCatalogData::DefaultAssetName(TEXT("DA_SC01Catalog"));

FPrimaryAssetId UAutomotiveCatalogData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

bool UAutomotiveCatalogData::BuildCatalogIndex(
	AutomotiveCatalog::FCatalogIndex& OutCatalog,
	AutomotiveCatalog::FError& OutError) const
{
	return OutCatalog.LoadJson(CatalogJson, OutError);
}
