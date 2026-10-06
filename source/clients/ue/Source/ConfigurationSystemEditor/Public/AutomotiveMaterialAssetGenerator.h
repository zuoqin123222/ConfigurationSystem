#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceConstant;
class UAutomotiveMaterialLibrary;

struct FAutomotiveMaterialGenerationResult
{
	TArray<UMaterialInstanceConstant*> Variants;
	UAutomotiveMaterialLibrary* Library = nullptr;
	int32 CreatedAssetCount = 0;
	int32 UpdatedAssetCount = 0;
	int32 ImportedTextureCount = 0;
	TMap<FString, FString> FamilyParentPaths;
	TArray<FString> Errors;

	bool Succeeded() const
	{
		return Errors.IsEmpty()
			&& FamilyParentPaths.Num() == 17
			&& Variants.Num() == 352
			&& Library != nullptr;
	}
};

/** Editor-only、公开且幂等的 SC01 Substrate Material Instance 物化入口。 */
class CONFIGURATIONSYSTEMEDITOR_API FAutomotiveMaterialAssetGenerator final
{
public:
	static constexpr TCHAR AssetRoot[] = TEXT("/Game/SC01/Materials");
	static constexpr TCHAR VariantRoot[] = TEXT("/Game/SC01/Materials/Variants");
	static constexpr TCHAR TextureRoot[] = TEXT("/Game/SC01/Materials/VariantTextures");
	static constexpr TCHAR LibraryPackageName[] =
		TEXT("/Game/SC01/Materials/DA_SC01MaterialLibrary");

	/**
	 * 审计 17 个材料族的仓库内 SubstrateMaterials 母材质，并按 catalog
	 * 创建或完全刷新 352 个 MI。不会创建 Master Material，也不会复制母材质。
	 */
	static bool Generate(FAutomotiveMaterialGenerationResult& OutResult);

	/** 写出可审阅的母材质映射、参数与 352 个 MI 统计。 */
	static bool WriteAuditReport(
		const FString& Filename,
		const FAutomotiveMaterialGenerationResult& Result);
};
