#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AutomotiveMaterialLibrary.generated.h"

class UMaterialInterface;

/** 车型无关的最小可 Cook 材质库：一个车漆和五类内饰 Master Material。 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API UAutomotiveMaterialLibrary final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;
	static const FName DefaultAssetName;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TSoftObjectPtr<UMaterialInterface> CarPaint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TSoftObjectPtr<UMaterialInterface> Alcantara;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TSoftObjectPtr<UMaterialInterface> Ultrasuede;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TSoftObjectPtr<UMaterialInterface> Leather;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TSoftObjectPtr<UMaterialInterface> Microfiber;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive|Materials")
	TSoftObjectPtr<UMaterialInterface> WovenWool;

	/** materialFamilyId 使用 v2 catalog 的稳定 ID。未知类型返回空。 */
	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	UMaterialInterface* LoadInteriorMaterial(const FString& MaterialFamilyId) const;
};
