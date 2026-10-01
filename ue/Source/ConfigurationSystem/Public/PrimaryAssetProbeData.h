#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture2D.h"
#include "PrimaryAssetProbeData.generated.h"

/**
 * 用于验证 AssetManager 扫描、枚举和异步加载链路的最小 Primary Asset。
 * Probe Bundle 会同时异步加载软引用纹理，而不需要地图对测试资产建立硬引用。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API UPrimaryAssetProbeData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** 由 Editor 命令生成的测试纹理；通过 Asset Bundle 参与异步加载。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Primary Asset Probe", meta = (AssetBundles = "Probe"))
	TSoftObjectPtr<UTexture2D> ProbeTexture;
};
