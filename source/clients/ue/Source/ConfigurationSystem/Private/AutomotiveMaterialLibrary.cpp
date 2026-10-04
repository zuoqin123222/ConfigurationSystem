#include "AutomotiveMaterialLibrary.h"

#include "Materials/MaterialInterface.h"

const FPrimaryAssetType UAutomotiveMaterialLibrary::PrimaryAssetType(
	TEXT("AutomotiveMaterialLibrary"));
const FName UAutomotiveMaterialLibrary::DefaultAssetName(TEXT("DA_SC01MaterialLibrary"));

FPrimaryAssetId UAutomotiveMaterialLibrary::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

UMaterialInterface* UAutomotiveMaterialLibrary::LoadInteriorMaterial(
	const FString& MaterialFamilyId) const
{
	const TSoftObjectPtr<UMaterialInterface>* Material = nullptr;
	if (MaterialFamilyId == TEXT("alcantara"))
	{
		Material = &Alcantara;
	}
	else if (MaterialFamilyId == TEXT("ultrasuede"))
	{
		Material = &Ultrasuede;
	}
	else if (MaterialFamilyId == TEXT("leather"))
	{
		Material = &Leather;
	}
	else if (MaterialFamilyId == TEXT("microfiber"))
	{
		Material = &Microfiber;
	}
	else if (MaterialFamilyId == TEXT("woven-wool"))
	{
		Material = &WovenWool;
	}
	return Material != nullptr ? Material->LoadSynchronous() : nullptr;
}
