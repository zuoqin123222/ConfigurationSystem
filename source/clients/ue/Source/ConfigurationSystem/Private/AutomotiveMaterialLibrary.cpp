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
	const TSoftObjectPtr<UMaterialInterface>* Material =
		FamilyParents.Find(MaterialFamilyId);
	return Material != nullptr ? Material->LoadSynchronous() : nullptr;
}

UMaterialInterface* UAutomotiveMaterialLibrary::LoadVariantMaterial(
	const FString& VariantId) const
{
	const TSoftObjectPtr<UMaterialInterface>* Material = Variants.Find(VariantId);
	return Material != nullptr ? Material->LoadSynchronous() : nullptr;
}
