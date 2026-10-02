#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sc01MaterialLibrary.generated.h"

class UMaterialInterface;

/** SC01 最小可 Cook 材质库：一个车漆和五类内饰 Master Material。 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API USc01MaterialLibrary final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;
	static const FName DefaultAssetName;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SC01|Materials")
	TSoftObjectPtr<UMaterialInterface> CarPaint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SC01|Materials")
	TSoftObjectPtr<UMaterialInterface> Alcantara;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SC01|Materials")
	TSoftObjectPtr<UMaterialInterface> Ultrasuede;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SC01|Materials")
	TSoftObjectPtr<UMaterialInterface> Leather;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SC01|Materials")
	TSoftObjectPtr<UMaterialInterface> Microfiber;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SC01|Materials")
	TSoftObjectPtr<UMaterialInterface> WovenWool;

	/** materialFamilyId 使用 v2 catalog 的稳定 ID。未知类型返回空。 */
	UFUNCTION(BlueprintCallable, Category = "SC01|Materials")
	UMaterialInterface* LoadInteriorMaterial(const FString& MaterialFamilyId) const;
};
