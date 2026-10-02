#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Sc01V2CatalogData.generated.h"

namespace Sc01V2
{
	class FCatalogIndex;
	struct FError;
}

/**
 * SC01 v2 目录的可 Cook Primary Asset。
 *
 * JSON 原文内嵌在资产中，Runtime 不依赖仓库外部的 contracts 路径。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API USc01V2CatalogData final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;
	static const FName DefaultAssetName;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** 与 contracts/fixtures/sc01.catalog.draft.v2.json 字节一致的 UTF-16 文本。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SC01 v2", meta = (MultiLine = true))
	FString CatalogJson;

	bool BuildCatalogIndex(Sc01V2::FCatalogIndex& OutCatalog, Sc01V2::FError& OutError) const;
};
