#include "AutomotiveMaterialAssetGenerator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AutomotiveMaterialLibrary.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace AutomotiveMaterialGeneration
{
	struct FFamilySpec
	{
		const TCHAR* Id;
		const TCHAR* ParentPath;
	};

	// 这些资产均属于随仓库提交的 /Game/SubstrateMaterials/Overview 内容。
	// 映射按材料的物理外观和用途选择；没有为 SC01 复制或重建任何母材质图。
	const FFamilySpec FamilySpecs[] = {
		{TEXT("paint"), TEXT("/Game/SubstrateMaterials/Materials/01_Paints/0_Templates/MTP_Paint_Metallic_Glint.MTP_Paint_Metallic_Glint")},
		{TEXT("aluminum-alloy"), TEXT("/Game/SubstrateMaterials/Materials/03_Metals/1_Basic/MI_Aluminum.MI_Aluminum")},
		{TEXT("magnesium-alloy"), TEXT("/Game/SubstrateMaterials/Materials/03_Metals/1_Basic/MI_Titanium_Dark.MI_Titanium_Dark")},
		{TEXT("carbon-fiber"), TEXT("/Game/SubstrateMaterials/Materials/04_Carbon/1_CarbonFiber/Templates/MTP_CarbonFiber_OPBR.MTP_CarbonFiber_OPBR")},
		{TEXT("metal"), TEXT("/Game/SubstrateMaterials/Materials/03_Metals/0_Templates/MTP_Metal.MTP_Metal")},
		{TEXT("ppg"), TEXT("/Game/SubstrateMaterials/Materials/01_Paints/0_Templates/MTP_Paint_Dielectric.MTP_Paint_Dielectric")},
		{TEXT("ultrasuede"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/6_Suede/MI_Suede_Charcoal.MI_Suede_Charcoal")},
		{TEXT("alcantara"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/6_Suede/MI_Suede_Black.MI_Suede_Black")},
		{TEXT("leather"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/0_Templates/MTP_Leather_OPBR.MTP_Leather_OPBR")},
		{TEXT("microfiber"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/2_PlasticLeather/MI_Plastic_Leather_Black_OPBR.MI_Plastic_Leather_Black_OPBR")},
		{TEXT("eva"), TEXT("/Game/SubstrateMaterials/Materials/07_Rubbers/0_Templates/MTP_Rubber_OPBR.MTP_Rubber_OPBR")},
		{TEXT("woven-fabric"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/0_Templates/MTP_Fabric_OPBR.MTP_Fabric_OPBR")},
		{TEXT("woven-wool"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/4_Fabric/MI_Fabric_Weave_OPBR.MI_Fabric_Weave_OPBR")},
		{TEXT("felt"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/0_Templates/MTP_Fabric_Velvet_OPBR.MTP_Fabric_Velvet_OPBR")},
		{TEXT("spray"), TEXT("/Game/SubstrateMaterials/Materials/01_Paints/0_Templates/MTP_Paint_Dielectric.MTP_Paint_Dielectric")},
		{TEXT("carpet"), TEXT("/Game/SubstrateMaterials/Materials/02_Upholstery/0_Templates/MTP_Carpet.MTP_Carpet")},
		{TEXT("rubber"), TEXT("/Game/SubstrateMaterials/Materials/07_Rubbers/0_Templates/MTP_Rubber_OPBR.MTP_Rubber_OPBR")}
	};

	FString CatalogFilename()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir(),
			TEXT("../../../contracts/fixtures/sc01.catalog.draft.v2.json"));
	}

	FString WoolSourceFilename(const FString& VariantId)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir(),
			FPaths::Combine(
				TEXT("SourceAssets/SC01/WovenWool"),
				VariantId + TEXT(".png")));
	}

	bool Save(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(
			Package,
			Asset,
			*FPackageName::LongPackageNameToFilename(
				Package->GetName(),
				FPackageName::GetAssetPackageExtension()),
			Args);
	}

	template <typename AssetType>
	AssetType* LoadOrCreate(
		const FString& PackageName,
		const FString& AssetName,
		bool& bOutCreated)
	{
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			if (AssetType* Existing = LoadObject<AssetType>(nullptr, *ObjectPath))
			{
				bOutCreated = false;
				return Existing;
			}
		}
		UPackage* Package = CreatePackage(*PackageName);
		AssetType* Result = NewObject<AssetType>(
			Package, *AssetName, RF_Public | RF_Standalone);
		bOutCreated = Result != nullptr;
		if (Result != nullptr)
		{
			FAssetRegistryModule::AssetCreated(Result);
		}
		return Result;
	}

	FString AssetSafeName(const FString& StableId, const TCHAR* Prefix)
	{
		FString Result = StableId;
		for (TCHAR& Character : Result)
		{
			if (!FChar::IsAlnum(Character))
			{
				Character = TEXT('_');
			}
		}
		return FString(Prefix) + Result;
	}

	bool ParseHexColor(const FString& Hex, FLinearColor& OutColor)
	{
		if (Hex.Len() != 7 || Hex[0] != TEXT('#'))
		{
			return false;
		}
		for (int32 Index = 1; Index < Hex.Len(); ++Index)
		{
			if (!FChar::IsHexDigit(Hex[Index]))
			{
				return false;
			}
		}
		OutColor = FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
		return true;
	}

	FName FindVectorParameter(
		UMaterialInterface* Parent,
		const TArray<FName>& Candidates)
	{
		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> Ids;
		Parent->GetAllVectorParameterInfo(Parameters, Ids);
		for (const FName Candidate : Candidates)
		{
			if (Parameters.ContainsByPredicate([Candidate](const FMaterialParameterInfo& Info)
				{ return Info.Name == Candidate; }))
			{
				return Candidate;
			}
		}
		return NAME_None;
	}

	FName FindTextureParameter(
		UMaterialInterface* Parent,
		const TArray<FName>& Candidates)
	{
		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> Ids;
		Parent->GetAllTextureParameterInfo(Parameters, Ids);
		for (const FName Candidate : Candidates)
		{
			if (Parameters.ContainsByPredicate([Candidate](const FMaterialParameterInfo& Info)
				{ return Info.Name == Candidate; }))
			{
				return Candidate;
			}
		}
		return NAME_None;
	}

	void SetFirstScalar(
		UMaterialInstanceConstant* Instance,
		UMaterialInterface* Parent,
		const TArray<FName>& Candidates,
		const float Value)
	{
		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> Ids;
		Parent->GetAllScalarParameterInfo(Parameters, Ids);
		for (const FName Candidate : Candidates)
		{
			if (Parameters.ContainsByPredicate([Candidate](const FMaterialParameterInfo& Info)
				{ return Info.Name == Candidate; }))
			{
				Instance->SetScalarParameterValueEditorOnly(
					FMaterialParameterInfo(Candidate), Value);
				return;
			}
		}
	}

	UTexture2D* LoadOrCreateWoolTexture(
		const FString& VariantId,
		const FString& SourceFilename,
		FAutomotiveMaterialGenerationResult& Result)
	{
		const FString AssetName = AssetSafeName(VariantId, TEXT("T_SC01_"));
		const FString PackageName =
			FString(FAutomotiveMaterialAssetGenerator::TextureRoot)
			+ TEXT("/WovenWool/") + AssetName;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			if (UTexture2D* Existing = LoadObject<UTexture2D>(
				nullptr, *(PackageName + TEXT(".") + AssetName)))
			{
				++Result.ImportedTextureCount;
				return Existing;
			}
		}
		if (!FPaths::FileExists(SourceFilename))
		{
			Result.Errors.Add(TEXT("羊毛缩略图不存在：") + SourceFilename);
			return nullptr;
		}
		FImage Image;
		if (!FImageUtils::LoadImage(*SourceFilename, Image))
		{
			Result.Errors.Add(TEXT("UE5.8 无法解码羊毛缩略图：") + SourceFilename);
			return nullptr;
		}
		Image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		UPackage* Package = CreatePackage(*PackageName);
		UTexture2D* Texture = NewObject<UTexture2D>(
			Package, *AssetName, RF_Public | RF_Standalone);
		if (Texture == nullptr)
		{
			Result.Errors.Add(TEXT("无法物化羊毛纹理：") + VariantId);
			return nullptr;
		}
		Texture->Source.Init(
			Image.SizeX,
			Image.SizeY,
			1,
			1,
			TSF_BGRA8,
			Image.RawData.GetData());
		Texture->SRGB = true;
		Texture->NeverStream = false;
		Texture->PostEditChange();
		FAssetRegistryModule::AssetCreated(Texture);
		if (!Save(Texture))
		{
			Result.Errors.Add(TEXT("无法保存羊毛纹理：") + VariantId);
			return nullptr;
		}
		++Result.CreatedAssetCount;
		++Result.ImportedTextureCount;
		return Texture;
	}

	UTexture2D* LoadOrCreateColorTexture(
		const FString& VariantId,
		const FString& FamilyId,
		const FLinearColor& LinearColor,
		FAutomotiveMaterialGenerationResult& Result)
	{
		const FString AssetName = AssetSafeName(VariantId, TEXT("T_SC01_"));
		const FString PackageName =
			FString(FAutomotiveMaterialAssetGenerator::TextureRoot)
			+ TEXT("/") + FamilyId + TEXT("/") + AssetName;
		bool bCreated = false;
		UTexture2D* Texture =
			LoadOrCreate<UTexture2D>(PackageName, AssetName, bCreated);
		if (Texture == nullptr)
		{
			Result.Errors.Add(TEXT("无法创建色卡纹理：") + VariantId);
			return nullptr;
		}
		const FColor Color = LinearColor.ToFColorSRGB();
		const uint8 Pixel[] = {Color.B, Color.G, Color.R, Color.A};
		Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, Pixel);
		Texture->SRGB = true;
		Texture->NeverStream = true;
		Texture->PostEditChange();
		if (!Save(Texture))
		{
			Result.Errors.Add(TEXT("无法保存色卡纹理：") + VariantId);
			return nullptr;
		}
		if (bCreated)
		{
			++Result.CreatedAssetCount;
		}
		else
		{
			++Result.UpdatedAssetCount;
		}
		++Result.ImportedTextureCount;
		return Texture;
	}

	float WoolScale(const FString& DisplayName)
	{
		if (DisplayName.StartsWith(TEXT("SQUARES"))) return 1.25f;
		if (DisplayName.StartsWith(TEXT("PEPITA"))) return 1.15f;
		if (DisplayName.StartsWith(TEXT("SOLM"))) return 1.0f;
		if (DisplayName.StartsWith(TEXT("MADRAS"))) return 0.9f;
		if (DisplayName.StartsWith(TEXT("TARTAN"))) return 0.85f;
		return 0.8f;
	}

	bool LoadCatalog(TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
	{
		FString Json;
		const FString Filename = CatalogFilename();
		if (!FFileHelper::LoadFileToString(Json, *Filename))
		{
			OutError = TEXT("无法读取 catalog：") + Filename;
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			OutError = TEXT("catalog JSON 非法：") + Filename;
			return false;
		}
		return true;
	}
}

bool FAutomotiveMaterialAssetGenerator::Generate(
	FAutomotiveMaterialGenerationResult& OutResult)
{
	using namespace AutomotiveMaterialGeneration;
	OutResult = FAutomotiveMaterialGenerationResult();

	TSharedPtr<FJsonObject> Catalog;
	FString CatalogError;
	if (!LoadCatalog(Catalog, CatalogError))
	{
		OutResult.Errors.Add(MoveTemp(CatalogError));
		return false;
	}

	TMap<FString, UMaterialInterface*> Parents;
	TSet<UObject*> SkeletalUsageAssets;
	for (const FFamilySpec& Spec : FamilySpecs)
	{
		const FString Path(Spec.ParentPath);
		if (!Path.StartsWith(TEXT("/Game/SubstrateMaterials/")))
		{
			OutResult.Errors.Add(TEXT("母材质越出仓库 SubstrateMaterials：") + Path);
			continue;
		}
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *Path);
		if (Parent == nullptr)
		{
			OutResult.Errors.Add(
				FString::Printf(TEXT("%s 母材质加载失败：%s"), Spec.Id, Spec.ParentPath));
			continue;
		}
		UMaterial* BaseMaterial = Parent->GetMaterial();
		bool bHasSkeletalUsage = false;
		const bool bNeedsSkeletalUsage =
			Parent->NeedsSetMaterialUsage_Concurrent(
				bHasSkeletalUsage,
				MATUSAGE_SkeletalMesh);
		if (BaseMaterial == nullptr
			|| (bNeedsSkeletalUsage
				&& !Parent->SetMaterialUsage(MATUSAGE_SkeletalMesh))
			|| !Parent->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh))
		{
			OutResult.Errors.Add(
				FString::Printf(
					TEXT("%s 母材质无法启用 SkeletalMesh usage：%s"),
					Spec.Id,
					Spec.ParentPath));
			continue;
		}
		if (bNeedsSkeletalUsage)
		{
			BaseMaterial->PostEditChange();
			Parent->PostEditChange();
			bool bUsageSaved = true;
			if (!SkeletalUsageAssets.Contains(BaseMaterial))
			{
				bUsageSaved = Save(BaseMaterial);
				SkeletalUsageAssets.Add(BaseMaterial);
			}
			if (Parent != BaseMaterial
				&& !SkeletalUsageAssets.Contains(Parent))
			{
				bUsageSaved = Save(Parent) && bUsageSaved;
				SkeletalUsageAssets.Add(Parent);
			}
			if (!bUsageSaved)
			{
				OutResult.Errors.Add(
					FString::Printf(
						TEXT("%s 母材质 SkeletalMesh usage 保存失败：%s"),
						Spec.Id,
						*BaseMaterial->GetPathName()));
				continue;
			}
		}
		Parents.Add(Spec.Id, Parent);
		OutResult.FamilyParentPaths.Add(Spec.Id, Path);
	}
	if (Parents.Num() != UE_ARRAY_COUNT(FamilySpecs))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr;
	if (!Catalog->TryGetArrayField(TEXT("materialVariants"), Variants)
		|| Variants == nullptr || Variants->Num() != 352)
	{
		OutResult.Errors.Add(TEXT("catalog 必须恰好包含 352 个 materialVariants。"));
		return false;
	}

	TMap<FString, TSoftObjectPtr<UMaterialInterface>> VariantReferences;
	for (const TSharedPtr<FJsonValue>& Value : *Variants)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString VariantId;
		FString FamilyId;
		FString DisplayName;
		FString ThumbnailUrl;
		if (!Value.IsValid() || !Value->TryGetObject(Object)
			|| !(*Object)->TryGetStringField(TEXT("variantId"), VariantId)
			|| !(*Object)->TryGetStringField(TEXT("materialFamilyId"), FamilyId)
			|| !(*Object)->TryGetStringField(TEXT("displayName"), DisplayName)
			|| !(*Object)->TryGetStringField(TEXT("thumbnailUrl"), ThumbnailUrl))
		{
			OutResult.Errors.Add(TEXT("materialVariant 缺少稳定字段。"));
			continue;
		}
		UMaterialInterface* Parent = Parents.FindRef(FamilyId);
		if (Parent == nullptr)
		{
			OutResult.Errors.Add(VariantId + TEXT(" 引用了未审计的材料族 ") + FamilyId);
			continue;
		}

		const FString AssetName = AssetSafeName(VariantId, TEXT("MI_SC01_"));
		const FString PackageName =
			FString(VariantRoot) + TEXT("/") + FamilyId + TEXT("/") + AssetName;
		bool bCreated = false;
		UMaterialInstanceConstant* Instance =
			LoadOrCreate<UMaterialInstanceConstant>(PackageName, AssetName, bCreated);
		if (Instance == nullptr)
		{
			OutResult.Errors.Add(TEXT("无法创建 MI：") + VariantId);
			continue;
		}
		bCreated ? ++OutResult.CreatedAssetCount : ++OutResult.UpdatedAssetCount;
		Instance->Modify();
		Instance->ClearParameterValuesEditorOnly();
		Instance->SetParentEditorOnly(Parent);

		const TSharedPtr<FJsonObject>* Ui = nullptr;
		FString ColorHex;
		UTexture2D* ColorTexture = nullptr;
		if ((*Object)->TryGetObjectField(TEXT("ui"), Ui)
			&& Ui != nullptr
			&& (*Ui)->TryGetStringField(TEXT("sortColorHex"), ColorHex))
		{
			FLinearColor Color;
			if (!ParseHexColor(ColorHex, Color))
			{
				OutResult.Errors.Add(VariantId + TEXT(" 的 sortColorHex 非法。"));
			}
			else
			{
				ColorTexture = LoadOrCreateColorTexture(
					VariantId, FamilyId, Color, OutResult);
			}
		}
		else if (FamilyId == TEXT("woven-wool"))
		{
			ColorTexture = LoadOrCreateWoolTexture(
				VariantId, WoolSourceFilename(VariantId), OutResult);
		}
		else
		{
			OutResult.Errors.Add(VariantId + TEXT(" 既无 sortColorHex 也不是羊毛花纹。"));
		}
		const FName TextureParameter = FindTextureParameter(
			Parent,
			{TEXT("Diffuse Color Map"), TEXT("Color Map"), TEXT("Base Color Map")});
		if (ColorTexture == nullptr || TextureParameter.IsNone())
		{
			OutResult.Errors.Add(VariantId + TEXT(" 的母材质没有可写颜色纹理参数。"));
		}
		else
		{
			Instance->SetTextureParameterValueEditorOnly(
				FMaterialParameterInfo(TextureParameter), ColorTexture);
		}
		if (FamilyId == TEXT("woven-wool"))
		{
			SetFirstScalar(
				Instance, Parent,
				{TEXT("Tile Uniform Scale"), TEXT("Uniform Scale")},
				WoolScale(DisplayName));
			SetFirstScalar(
				Instance, Parent,
				{TEXT("Rotation"), TEXT("UV Rotation")},
				0.0f);
		}

		FMetaData& MetaData = Instance->GetOutermost()->GetMetaData();
		MetaData.SetValue(Instance, TEXT("SC01.VariantId"), *VariantId);
		MetaData.SetValue(Instance, TEXT("SC01.MaterialFamilyId"), *FamilyId);
		MetaData.SetValue(Instance, TEXT("SC01.ThumbnailUrl"), *ThumbnailUrl);
		if (FamilyId == TEXT("woven-wool"))
		{
			MetaData.SetValue(
				Instance, TEXT("SC01.PatternScale"),
				*FString::SanitizeFloat(WoolScale(DisplayName)));
			MetaData.SetValue(Instance, TEXT("SC01.PatternRotationDegrees"), TEXT("0"));
		}
		Instance->PostEditChange();
		if (!Save(Instance))
		{
			OutResult.Errors.Add(TEXT("保存 MI 失败：") + VariantId);
		}
		OutResult.Variants.Add(Instance);
		VariantReferences.Add(
			VariantId,
			TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Instance)));
	}

	bool bLibraryCreated = false;
	UAutomotiveMaterialLibrary* Library = LoadOrCreate<UAutomotiveMaterialLibrary>(
		LibraryPackageName, TEXT("DA_SC01MaterialLibrary"), bLibraryCreated);
	if (Library == nullptr)
	{
		OutResult.Errors.Add(TEXT("无法创建 DA_SC01MaterialLibrary。"));
		return false;
	}
	bLibraryCreated ? ++OutResult.CreatedAssetCount : ++OutResult.UpdatedAssetCount;
	Library->Modify();
	Library->FamilyParents.Reset();
	for (const TPair<FString, UMaterialInterface*>& Pair : Parents)
	{
		Library->FamilyParents.Add(
			Pair.Key,
			TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Pair.Value)));
	}
	Library->Variants = MoveTemp(VariantReferences);
	Library->PostEditChange();
	if (!Save(Library))
	{
		OutResult.Errors.Add(TEXT("保存 DA_SC01MaterialLibrary 失败。"));
	}
	OutResult.Library = Library;
	return OutResult.Succeeded();
}

bool FAutomotiveMaterialAssetGenerator::WriteAuditReport(
	const FString& Filename,
	const FAutomotiveMaterialGenerationResult& Result)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("scope"), TEXT("/Game/SubstrateMaterials/Maps/Overview"));
	Root->SetStringField(TEXT("policy"), TEXT("reuse-only-no-parent-copy"));
	Root->SetNumberField(TEXT("familyCount"), Result.FamilyParentPaths.Num());
	Root->SetNumberField(TEXT("variantInstanceCount"), Result.Variants.Num());
	Root->SetNumberField(TEXT("variantTextureCount"), Result.ImportedTextureCount);
	TArray<TSharedPtr<FJsonValue>> Families;
	TArray<FString> FamilyIds;
	Result.FamilyParentPaths.GetKeys(FamilyIds);
	FamilyIds.Sort();
	for (const FString& FamilyId : FamilyIds)
	{
		TSharedRef<FJsonObject> Family = MakeShared<FJsonObject>();
		const FString& ParentPath = Result.FamilyParentPaths.FindChecked(FamilyId);
		Family->SetStringField(TEXT("materialFamilyId"), FamilyId);
		Family->SetStringField(TEXT("parentMaterialPath"), ParentPath);
		Family->SetBoolField(
			TEXT("insideRepositorySubstrateMaterials"),
			ParentPath.StartsWith(TEXT("/Game/SubstrateMaterials/")));
		Families.Add(MakeShared<FJsonValueObject>(Family));
	}
	Root->SetArrayField(TEXT("families"), Families);
	TArray<TSharedPtr<FJsonValue>> Errors;
	for (const FString& Error : Result.Errors)
	{
		Errors.Add(MakeShared<FJsonValueString>(Error));
	}
	Root->SetArrayField(TEXT("errors"), Errors);
	Root->SetBoolField(TEXT("passed"), Result.Succeeded());

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	return FFileHelper::SaveStringToFile(Json, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
