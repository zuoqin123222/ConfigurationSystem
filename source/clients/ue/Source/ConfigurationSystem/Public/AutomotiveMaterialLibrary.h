#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AutomotiveMaterialLibrary.generated.h"

class UMaterialInterface;

/**
 * SC01 可 Cook 材质索引。只引用仓库内 SubstrateMaterials 母材质以及由 catalog
 * 物化的 Material Instance，不拥有或复制母材质图。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API UAutomotiveMaterialLibrary final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;
	static const FName DefaultAssetName;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TMap<FString, TSoftObjectPtr<UMaterialInterface>> FamilyParents;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TMap<FString, TSoftObjectPtr<UMaterialInterface>> Variants;

	/** materialFamilyId 使用 v2 catalog 的稳定 ID；返回已审计的 Substrate 母材质。 */
	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	UMaterialInterface* LoadInteriorMaterial(const FString& MaterialFamilyId) const;

	/** variantId 使用 v2 catalog 的稳定 ID；返回 catalog 对应的物化 MI。 */
	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	UMaterialInterface* LoadVariantMaterial(const FString& VariantId) const;
};
