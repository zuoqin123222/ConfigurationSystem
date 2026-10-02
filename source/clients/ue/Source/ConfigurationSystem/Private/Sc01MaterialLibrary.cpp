#include "Sc01MaterialLibrary.h"

#include "Materials/MaterialInterface.h"

const FPrimaryAssetType USc01MaterialLibrary::PrimaryAssetType(TEXT("Sc01MaterialLibrary"));
const FName USc01MaterialLibrary::DefaultAssetName(TEXT("DA_SC01MaterialLibrary"));

FPrimaryAssetId USc01MaterialLibrary::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

UMaterialInterface* USc01MaterialLibrary::LoadInteriorMaterial(
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
