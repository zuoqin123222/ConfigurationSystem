#include "Sc01MaterialAssetGenerator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "Sc01MaterialLibrary.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace Sc01MaterialGeneration
{
	struct FMaterialSpec
	{
		const TCHAR* Name;
		FLinearColor BaseColor;
		float Roughness;
		float MicrostructureScale;
		float MicrostructureStrength;
		float FuzzAmount;
		float FuzzExponent;
	};

	template <typename AssetType>
	AssetType* LoadOrCreate(
		const FString& PackageName,
		const FString& AssetName,
		bool& bOutCreated)
	{
		const FString ObjectPath =
			FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
		if (AssetType* Existing = LoadObject<AssetType>(nullptr, *ObjectPath))
		{
			bOutCreated = false;
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackageName);
		bOutCreated = true;
		return NewObject<AssetType>(
			Package,
			*AssetName,
			RF_Public | RF_Standalone);
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

	template <typename ExpressionType>
	ExpressionType* AddExpression(UMaterial* Material, const int32 X, const int32 Y)
	{
		return CastChecked<ExpressionType>(
			UMaterialEditingLibrary::CreateMaterialExpression(
				Material,
				ExpressionType::StaticClass(),
				X,
				Y));
	}

	UMaterialExpressionScalarParameter* AddScalar(
		UMaterial* Material,
		const TCHAR* Name,
		const float Default,
		const int32 X,
		const int32 Y)
	{
		UMaterialExpressionScalarParameter* Result =
			AddExpression<UMaterialExpressionScalarParameter>(Material, X, Y);
		Result->ParameterName = Name;
		Result->DefaultValue = Default;
		return Result;
	}

	UMaterialExpressionVectorParameter* AddColor(
		UMaterial* Material,
		const FLinearColor& Default,
		const int32 X,
		const int32 Y)
	{
		UMaterialExpressionVectorParameter* Result =
			AddExpression<UMaterialExpressionVectorParameter>(Material, X, Y);
		Result->ParameterName = TEXT("BaseColor");
		Result->DefaultValue = Default;
		return Result;
	}

	void Connect(
		UMaterialExpression* From,
		UMaterialExpression* To,
		const TCHAR* Input)
	{
		UMaterialEditingLibrary::ConnectMaterialExpressions(
			From,
			TEXT(""),
			To,
			Input);
	}

	void ResetMaterial(UMaterial* Material)
	{
		Material->Modify();
		// UE 5.8 的 DeleteAllMaterialExpressions 在遍历时原地移除，可能跳过元素；
		// 先复制快照再逐个删除，保证重复生成不会累积参数或节点。
		const TArray<UMaterialExpression*> ExistingExpressions =
			UMaterialEditingLibrary::GetMaterialExpressions(Material);
		for (UMaterialExpression* Expression : ExistingExpressions)
		{
			UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
		}
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->TwoSided = false;
	}

	void BuildCarPaint(UMaterial* Material)
	{
		ResetMaterial(Material);
		Material->SetShadingModel(MSM_ClearCoat);

		UMaterialExpressionVectorParameter* BaseColor =
			AddColor(Material, FLinearColor(0.55f, 0.015f, 0.02f), -900, -280);
		UMaterialExpressionScalarParameter* Metallic =
			AddScalar(Material, TEXT("Metallic"), 0.82f, -900, -120);
		UMaterialExpressionScalarParameter* Roughness =
			AddScalar(Material, TEXT("Roughness"), 0.22f, -900, 20);
		UMaterialExpressionScalarParameter* ClearCoat =
			AddScalar(Material, TEXT("ClearCoat"), 1.0f, -900, 160);
		UMaterialExpressionScalarParameter* ClearCoatRoughness =
			AddScalar(Material, TEXT("ClearCoatRoughness"), 0.08f, -900, 300);
		UMaterialExpressionScalarParameter* OrangePeel =
			AddScalar(Material, TEXT("OrangePeel"), 0.08f, -900, 440);
		UMaterialExpressionScalarParameter* FlakeIntensity =
			AddScalar(Material, TEXT("FlakeIntensity"), 0.18f, -900, 580);

		UMaterialExpressionNoise* OrangeNoise =
			AddExpression<UMaterialExpressionNoise>(Material, -620, 400);
		OrangeNoise->Scale = 180.0f;
		OrangeNoise->Quality = 1;
		OrangeNoise->Levels = 2;
		OrangeNoise->OutputMin = -0.5f;
		OrangeNoise->OutputMax = 0.5f;
		UMaterialExpressionMultiply* OrangeAmount =
			AddExpression<UMaterialExpressionMultiply>(Material, -380, 300);
		Connect(OrangeNoise, OrangeAmount, TEXT("A"));
		Connect(OrangePeel, OrangeAmount, TEXT("B"));
		UMaterialExpressionAdd* FinalRoughness =
			AddExpression<UMaterialExpressionAdd>(Material, -120, 100);
		Connect(Roughness, FinalRoughness, TEXT("A"));
		Connect(OrangeAmount, FinalRoughness, TEXT("B"));

		UMaterialExpressionNoise* FlakeNoise =
			AddExpression<UMaterialExpressionNoise>(Material, -620, -520);
		FlakeNoise->Scale = 900.0f;
		FlakeNoise->Quality = 1;
		FlakeNoise->Levels = 1;
		FlakeNoise->OutputMin = 0.0f;
		FlakeNoise->OutputMax = 0.12f;
		UMaterialExpressionMultiply* FlakeAmount =
			AddExpression<UMaterialExpressionMultiply>(Material, -380, -420);
		Connect(FlakeNoise, FlakeAmount, TEXT("A"));
		Connect(FlakeIntensity, FlakeAmount, TEXT("B"));
		UMaterialExpressionAdd* FinalColor =
			AddExpression<UMaterialExpressionAdd>(Material, -120, -260);
		Connect(BaseColor, FinalColor, TEXT("A"));
		Connect(FlakeAmount, FinalColor, TEXT("B"));

		UMaterialEditingLibrary::ConnectMaterialProperty(
			FinalColor, TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(
			Metallic, TEXT(""), MP_Metallic);
		UMaterialEditingLibrary::ConnectMaterialProperty(
			FinalRoughness, TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(
			ClearCoat, TEXT(""), MP_CustomData0);
		UMaterialEditingLibrary::ConnectMaterialProperty(
			ClearCoatRoughness, TEXT(""), MP_CustomData1);
	}

	void BuildInterior(UMaterial* Material, const FMaterialSpec& Spec)
	{
		ResetMaterial(Material);
		Material->SetShadingModel(MSM_DefaultLit);

		UMaterialExpressionVectorParameter* BaseColor =
			AddColor(Material, Spec.BaseColor, -900, -300);
		UMaterialExpressionScalarParameter* Roughness =
			AddScalar(Material, TEXT("Roughness"), Spec.Roughness, -900, -140);
		UMaterialExpressionScalarParameter* MicroScale =
			AddScalar(
				Material,
				TEXT("MicrostructureScale"),
				Spec.MicrostructureScale,
				-900,
				20);
		UMaterialExpressionScalarParameter* MicroStrength =
			AddScalar(
				Material,
				TEXT("MicrostructureStrength"),
				Spec.MicrostructureStrength,
				-900,
				160);
		UMaterialExpressionScalarParameter* FuzzAmount =
			AddScalar(Material, TEXT("FuzzAmount"), Spec.FuzzAmount, -900, 300);
		UMaterialExpressionScalarParameter* FuzzExponent =
			AddScalar(Material, TEXT("FuzzExponent"), Spec.FuzzExponent, -900, 440);

		UMaterialExpressionTextureCoordinate* TexCoord =
			AddExpression<UMaterialExpressionTextureCoordinate>(Material, -650, -20);
		UMaterialExpressionMultiply* ScaledUv =
			AddExpression<UMaterialExpressionMultiply>(Material, -430, 20);
		Connect(TexCoord, ScaledUv, TEXT("A"));
		Connect(MicroScale, ScaledUv, TEXT("B"));
		UMaterialExpressionNoise* MicroNoise =
			AddExpression<UMaterialExpressionNoise>(Material, -210, 20);
		MicroNoise->Scale = 1.0f;
		MicroNoise->Quality = 1;
		MicroNoise->Levels = 2;
		MicroNoise->OutputMin = -0.5f;
		MicroNoise->OutputMax = 0.5f;
		Connect(ScaledUv, MicroNoise, TEXT("Position"));
		UMaterialExpressionMultiply* MicroAmount =
			AddExpression<UMaterialExpressionMultiply>(Material, 20, 40);
		Connect(MicroNoise, MicroAmount, TEXT("A"));
		Connect(MicroStrength, MicroAmount, TEXT("B"));
		UMaterialExpressionAdd* FinalRoughness =
			AddExpression<UMaterialExpressionAdd>(Material, 250, -60);
		Connect(Roughness, FinalRoughness, TEXT("A"));
		Connect(MicroAmount, FinalRoughness, TEXT("B"));

		UMaterialExpressionFresnel* Fresnel =
			AddExpression<UMaterialExpressionFresnel>(Material, -420, -400);
		Connect(FuzzExponent, Fresnel, TEXT("ExponentIn"));
		UMaterialExpressionMultiply* Fuzz =
			AddExpression<UMaterialExpressionMultiply>(Material, -180, -350);
		Connect(Fresnel, Fuzz, TEXT("A"));
		Connect(FuzzAmount, Fuzz, TEXT("B"));
		UMaterialExpressionAdd* FinalColor =
			AddExpression<UMaterialExpressionAdd>(Material, 80, -270);
		Connect(BaseColor, FinalColor, TEXT("A"));
		Connect(Fuzz, FinalColor, TEXT("B"));

		UMaterialEditingLibrary::ConnectMaterialProperty(
			FinalColor, TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(
			FinalRoughness, TEXT(""), MP_Roughness);
		UMaterialExpressionScalarParameter* Specular =
			AddScalar(Material, TEXT("Specular"), 0.35f, 20, 220);
		UMaterialEditingLibrary::ConnectMaterialProperty(
			Specular, TEXT(""), MP_Specular);
	}

	UMaterial* GenerateMaterial(
		const FMaterialSpec& Spec,
		const bool bCarPaint,
		FSc01MaterialGenerationResult& Result)
	{
		const FString AssetName(Spec.Name);
		const FString PackageName =
			FString::Printf(TEXT("%s/%s"), FSc01MaterialAssetGenerator::AssetRoot, Spec.Name);
		bool bCreated = false;
		UMaterial* Material =
			LoadOrCreate<UMaterial>(PackageName, AssetName, bCreated);
		if (Material == nullptr)
		{
			Result.Errors.Add(FString::Printf(TEXT("无法创建材质 %s。"), Spec.Name));
			return nullptr;
		}
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Material);
			++Result.CreatedAssetCount;
		}
		else
		{
			++Result.UpdatedAssetCount;
		}

		if (bCarPaint)
		{
			BuildCarPaint(Material);
		}
		else
		{
			BuildInterior(Material, Spec);
		}
		const TArray<FString> CompileErrors =
			UMaterialEditingLibrary::RecompileMaterial(Material);
		for (const FString& Error : CompileErrors)
		{
			Result.Errors.Add(FString::Printf(TEXT("%s: %s"), Spec.Name, *Error));
		}
		Material->PostEditChange();
		if (!Save(Material))
		{
			Result.Errors.Add(FString::Printf(TEXT("保存材质 %s 失败。"), Spec.Name));
		}
		Result.Materials.Add(Material);
		return Material;
	}
}

bool FSc01MaterialAssetGenerator::Generate(
	FSc01MaterialGenerationResult& OutResult)
{
	using namespace Sc01MaterialGeneration;
	OutResult = FSc01MaterialGenerationResult();

	const FMaterialSpec Specs[] = {
		{TEXT("M_SC01_CarPaint"), FLinearColor(0.55f, 0.015f, 0.02f), 0.22f, 0, 0, 0, 0},
		{TEXT("M_SC01_Alcantara"), FLinearColor(0.035f, 0.038f, 0.042f), 0.78f, 260.0f, 0.16f, 0.12f, 4.0f},
		{TEXT("M_SC01_Ultrasuede"), FLinearColor(0.07f, 0.075f, 0.08f), 0.74f, 310.0f, 0.13f, 0.10f, 4.8f},
		{TEXT("M_SC01_Leather"), FLinearColor(0.11f, 0.045f, 0.022f), 0.46f, 95.0f, 0.09f, 0.025f, 6.0f},
		{TEXT("M_SC01_Microfiber"), FLinearColor(0.025f, 0.028f, 0.032f), 0.70f, 420.0f, 0.12f, 0.08f, 5.0f},
		{TEXT("M_SC01_WovenWool"), FLinearColor(0.10f, 0.095f, 0.085f), 0.86f, 180.0f, 0.20f, 0.14f, 3.5f}
	};

	TMap<FString, UMaterial*> ByName;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Specs); ++Index)
	{
		if (UMaterial* Material =
			GenerateMaterial(Specs[Index], Index == 0, OutResult))
		{
			ByName.Add(Specs[Index].Name, Material);
		}
	}
	if (ByName.Num() != UE_ARRAY_COUNT(Specs))
	{
		return false;
	}

	const FString LibraryAssetName(TEXT("DA_SC01MaterialLibrary"));
	bool bLibraryCreated = false;
	USc01MaterialLibrary* Library = LoadOrCreate<USc01MaterialLibrary>(
		LibraryPackageName,
		LibraryAssetName,
		bLibraryCreated);
	if (Library == nullptr)
	{
		OutResult.Errors.Add(TEXT("无法创建 DA_SC01MaterialLibrary。"));
		return false;
	}
	if (bLibraryCreated)
	{
		FAssetRegistryModule::AssetCreated(Library);
		++OutResult.CreatedAssetCount;
	}
	else
	{
		++OutResult.UpdatedAssetCount;
	}

	Library->Modify();
	Library->CarPaint = ByName.FindRef(TEXT("M_SC01_CarPaint"));
	Library->Alcantara = ByName.FindRef(TEXT("M_SC01_Alcantara"));
	Library->Ultrasuede = ByName.FindRef(TEXT("M_SC01_Ultrasuede"));
	Library->Leather = ByName.FindRef(TEXT("M_SC01_Leather"));
	Library->Microfiber = ByName.FindRef(TEXT("M_SC01_Microfiber"));
	Library->WovenWool = ByName.FindRef(TEXT("M_SC01_WovenWool"));
	Library->PostEditChange();
	if (!Save(Library))
	{
		OutResult.Errors.Add(TEXT("保存 DA_SC01MaterialLibrary 失败。"));
	}
	OutResult.Library = Library;
	return OutResult.Succeeded();
}
