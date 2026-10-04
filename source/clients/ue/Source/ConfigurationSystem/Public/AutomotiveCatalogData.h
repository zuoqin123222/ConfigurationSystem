#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AutomotiveCatalogData.generated.h"

namespace AutomotiveCatalog
{
	class FCatalogIndex;
	struct FError;
}

/**
 * 车型目录 v2 的可 Cook Primary Asset。
 *
 * JSON 原文内嵌在资产中，Runtime 不依赖仓库外部的 contracts 路径。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API UAutomotiveCatalogData final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;
	static const FName DefaultAssetName;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** 与 contracts/fixtures/sc01.catalog.draft.v2.json 字节一致的 UTF-16 文本。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Automotive Catalog", meta = (MultiLine = true))
	FString CatalogJson;

	bool BuildCatalogIndex(
		AutomotiveCatalog::FCatalogIndex& OutCatalog,
		AutomotiveCatalog::FError& OutError) const;
};
