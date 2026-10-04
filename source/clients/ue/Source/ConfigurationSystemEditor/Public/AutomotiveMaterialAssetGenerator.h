#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UAutomotiveMaterialLibrary;

struct FAutomotiveMaterialGenerationResult
{
	TArray<UMaterial*> Materials;
	UAutomotiveMaterialLibrary* Library = nullptr;
	int32 CreatedAssetCount = 0;
	int32 UpdatedAssetCount = 0;
	TArray<FString> Errors;

	bool Succeeded() const
	{
		return Errors.IsEmpty() && Materials.Num() == 6 && Library != nullptr;
	}
};

/** Editor-only、公开且幂等的车型材质资产生成入口。 */
class CONFIGURATIONSYSTEMEDITOR_API FAutomotiveMaterialAssetGenerator final
{
public:
	static constexpr TCHAR AssetRoot[] = TEXT("/Game/SC01/Materials");
	static constexpr TCHAR LibraryPackageName[] =
		TEXT("/Game/SC01/Materials/DA_SC01MaterialLibrary");

	/**
	 * 创建或完全刷新六个材质图和材质库。重复调用复用同一路径，
	 * 清空旧表达式后重建，不产生重名资产或累积节点。
	 */
	static bool Generate(FAutomotiveMaterialGenerationResult& OutResult);
};
