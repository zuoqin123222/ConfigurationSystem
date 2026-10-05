#include "AutomotiveMaterialBinder.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "AutomotiveMaterialLibrary.h"
#include "AutomotiveConfigurationState.h"

const FName UAutomotiveMaterialBinder::PaintProxySlotTag(
	TEXT("Configurator.Slot.paint_body"));
const FName UAutomotiveMaterialBinder::InteriorProxySlotTag(
	TEXT("Configurator.Slot.automotive_interior_material_proxy"));
const FString UAutomotiveMaterialBinder::PaintSurfaceId(TEXT("exterior-body-cover"));
const FString UAutomotiveMaterialBinder::InteriorProxySurfaceId(TEXT("door-middle"));

namespace
{
	TOptional<FLinearColor> ResolveCatalogColor(
		const FString& ColorCode,
		const TOptional<FString>& DisplayColorHex = TOptional<FString>())
	{
		if (DisplayColorHex.IsSet())
		{
			return FLinearColor::FromSRGBColor(
				FColor::FromHex(DisplayColorHex.GetValue()));
		}
		if (ColorCode.Equals(TEXT("red"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#A61D24")));
		}
		if (ColorCode.Equals(TEXT("silver"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#BFC3C7")));
		}
		const FString Hex = ColorCode.StartsWith(TEXT("#"))
			? ColorCode.Mid(1)
			: ColorCode;
		bool bIsHex = Hex.Len() == 6 || Hex.Len() == 8;
		for (const TCHAR Character : Hex)
		{
			bIsHex = bIsHex && FChar::IsHexDigit(Character);
		}
		if (bIsHex)
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(ColorCode));
		}
		return TOptional<FLinearColor>();
	}
}

UMeshComponent* UAutomotiveMaterialBinder::FindUniqueTaggedMesh(
	AActor* Vehicle,
	const FName SlotTag,
	FString& OutError)
{
	TInlineComponentArray<UMeshComponent*> Meshes(Vehicle);
	UMeshComponent* Match = nullptr;
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->ComponentHasTag(SlotTag))
		{
			continue;
		}
		if (Match != nullptr)
		{
			OutError = FString::Printf(
				TEXT("代理槽 %s 必须且只能绑定一个 MeshComponent。"),
				*SlotTag.ToString());
			return nullptr;
		}
		Match = Mesh;
	}
	if (Match == nullptr)
	{
		OutError = FString::Printf(
			TEXT("未找到代理槽 %s。"),
			*SlotTag.ToString());
	}
	return Match;
}

USkeletalMeshComponent* UAutomotiveMaterialBinder::FindVisibleSkeletalVehicle(
	AActor* Vehicle)
{
	TInlineComponentArray<USkeletalMeshComponent*> Meshes(Vehicle);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (IsValid(Mesh)
			&& Mesh->IsVisible()
			&& !Mesh->bHiddenInGame
			&& IsValid(Mesh->GetSkeletalMeshAsset())
			&& Mesh->GetMaterialIndex(TEXT("CS_Validation_Paint")) != INDEX_NONE
			&& Mesh->GetMaterialIndex(TEXT("CS_Validation_Interior")) != INDEX_NONE)
		{
			return Mesh;
		}
	}
	return nullptr;
}

bool UAutomotiveMaterialBinder::Bind(
	UAutomotiveConfigurationState* InState,
	UAutomotiveMaterialLibrary* InLibrary,
	AActor* InVehicle)
{
	Unbind();
	if (!IsValid(InState) || !InState->IsInitialized()
		|| !IsValid(InLibrary) || !IsValid(InVehicle))
	{
		LastError = TEXT("状态、材质库或车辆无效。");
		return false;
	}

	UMeshComponent* CandidatePaint = nullptr;
	UMeshComponent* CandidateInterior = nullptr;
	int32 CandidatePaintIndex = 0;
	int32 CandidateInteriorIndex = 0;
	if (USkeletalMeshComponent* SkeletalMesh =
		FindVisibleSkeletalVehicle(InVehicle))
	{
		CandidatePaint = SkeletalMesh;
		CandidateInterior = SkeletalMesh;
		CandidatePaintIndex =
			SkeletalMesh->GetMaterialIndex(TEXT("CS_Validation_Paint"));
		CandidateInteriorIndex =
			SkeletalMesh->GetMaterialIndex(TEXT("CS_Validation_Interior"));
	}
	else
	{
		FString SlotError;
		CandidatePaint =
			FindUniqueTaggedMesh(InVehicle, PaintProxySlotTag, SlotError);
		if (CandidatePaint == nullptr)
		{
			LastError = MoveTemp(SlotError);
			return false;
		}
		CandidateInterior =
			FindUniqueTaggedMesh(InVehicle, InteriorProxySlotTag, SlotError);
		if (CandidateInterior == nullptr)
		{
			LastError = MoveTemp(SlotError);
			return false;
		}
	}

	State = InState;
	Library = InLibrary;
	PaintComponent = CandidatePaint;
	InteriorComponent = CandidateInterior;
	PaintMaterialIndex = CandidatePaintIndex;
	InteriorMaterialIndex = CandidateInteriorIndex;
	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("AutomotiveMaterialBinder bound paint=%s[%d] interior=%s[%d]"),
		*PaintComponent->GetName(),
		PaintMaterialIndex,
		*InteriorComponent->GetName(),
		InteriorMaterialIndex);
	State->OnChangedNative.AddUObject(this, &UAutomotiveMaterialBinder::HandleStateChanged);
	return ApplyCurrentConfiguration();
}

void UAutomotiveMaterialBinder::Unbind()
{
	if (IsValid(State))
	{
		State->OnChangedNative.RemoveAll(this);
	}
	State = nullptr;
	Library = nullptr;
	PaintComponent = nullptr;
	InteriorComponent = nullptr;
	PaintMaterialInstance = nullptr;
	InteriorMaterialInstance = nullptr;
	PaintMaterialIndex = 0;
	InteriorMaterialIndex = 0;
	AppliedInteriorFamilyId.Reset();
	LastError.Reset();
}

void UAutomotiveMaterialBinder::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

void UAutomotiveMaterialBinder::HandleStateChanged()
{
	ApplyCurrentConfiguration();
}

bool UAutomotiveMaterialBinder::ApplyCurrentConfiguration()
{
	if (!IsValid(State) || !IsValid(Library)
		|| !IsValid(PaintComponent) || !IsValid(InteriorComponent))
	{
		LastError = TEXT("Binder 尚未完成有效绑定。");
		return false;
	}

	LastError.Reset();
	const bool bPaintApplied = ApplyPaint();
	const bool bInteriorApplied = ApplyInterior();
	return bPaintApplied && bInteriorApplied;
}

bool UAutomotiveMaterialBinder::ApplyPaint()
{
	UMaterialInterface* PaintMaster = Library->CarPaint.LoadSynchronous();
	if (!IsValid(PaintMaster))
	{
		LastError = TEXT("AutomotiveMaterialLibrary 缺少 CarPaint。");
		return false;
	}

	if (!IsValid(PaintMaterialInstance)
		|| PaintMaterialInstance->Parent != PaintMaster)
	{
		PaintMaterialInstance = UMaterialInstanceDynamic::Create(PaintMaster, this);
	}
	if (!IsValid(PaintMaterialInstance))
	{
		LastError = TEXT("无法创建车漆动态材质实例。");
		return false;
	}
	PaintComponent->SetMaterial(PaintMaterialIndex, PaintMaterialInstance);

	const TMap<FString, FAutomotiveCustomization> Customizations =
		State->GetCustomizations();
	const FAutomotiveCustomization* Customization = Customizations.Find(PaintSurfaceId);
	if (Customization != nullptr
		&& Customization->Kind == EAutomotiveCustomizationKind::Paint)
	{
		const FAutomotivePaintCustomization& Paint = Customization->Paint;
		PaintMaterialInstance->SetVectorParameterValue(
			TEXT("BaseColor"),
			FLinearColor::FromSRGBColor(FColor::FromHex(Paint.ColorHex)));
		PaintMaterialInstance->SetScalarParameterValue(TEXT("Metallic"), Paint.Metallic);
		PaintMaterialInstance->SetScalarParameterValue(TEXT("Roughness"), Paint.Roughness);
		PaintMaterialInstance->SetScalarParameterValue(TEXT("ClearCoat"), Paint.ClearCoat);
		PaintMaterialInstance->SetScalarParameterValue(TEXT("OrangePeel"), Paint.OrangePeel);
		PaintMaterialInstance->SetScalarParameterValue(
			TEXT("FlakeIntensity"),
			Paint.FlakeIntensity);
		UE_LOG(
			LogTemp,
			Verbose,
			TEXT("AutomotiveMaterialBinder applied custom paint %s to %s[%d]"),
			*Paint.ColorHex,
			*PaintComponent->GetName(),
			PaintMaterialIndex);
		return true;
	}

	const FString PaintOptionId = State->GetSelections().FindRef(PaintSurfaceId);
	const AutomotiveCatalog::FOption* PaintOption =
		State->GetCatalogIndex().FindOption(PaintOptionId);
	if (PaintOption == nullptr || !PaintOption->ColorCode.IsSet())
	{
		return true;
	}
	const TOptional<FLinearColor> FixedColor =
		ResolveCatalogColor(PaintOption->ColorCode.GetValue());
	if (!FixedColor.IsSet())
	{
		LastError = FString::Printf(
			TEXT("无法解析标准车漆颜色 %s。"),
			*PaintOption->ColorCode.GetValue());
		return false;
	}
	PaintMaterialInstance->SetVectorParameterValue(
		TEXT("BaseColor"),
		FixedColor.GetValue());
	PaintMaterialInstance->SetScalarParameterValue(
		TEXT("Metallic"),
		PaintOption->ColorCode.GetValue().Equals(
			TEXT("silver"),
			ESearchCase::IgnoreCase) ? 0.8f : 0.35f);
	PaintMaterialInstance->SetScalarParameterValue(TEXT("Roughness"), 0.22f);
	PaintMaterialInstance->SetScalarParameterValue(TEXT("ClearCoat"), 0.85f);
	PaintMaterialInstance->SetScalarParameterValue(TEXT("OrangePeel"), 0.12f);
	PaintMaterialInstance->SetScalarParameterValue(TEXT("FlakeIntensity"), 0.25f);
	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("AutomotiveMaterialBinder applied fixed paint %s as %s to %s[%d]"),
		*PaintOptionId,
		*FixedColor.GetValue().ToString(),
		*PaintComponent->GetName(),
		PaintMaterialIndex);
	return true;
}

bool UAutomotiveMaterialBinder::ApplyInterior()
{
	const FString OptionId =
		State->GetSelections().FindRef(InteriorProxySurfaceId);
	const AutomotiveCatalog::FOption* Option =
		State->GetCatalogIndex().FindOption(OptionId);
	if (Option == nullptr || !Option->MaterialFamilyId.IsSet())
	{
		LastError = TEXT("内饰代理 surface 没有可解析的 materialFamilyId。");
		return false;
	}

	FString FamilyId = Option->MaterialFamilyId.GetValue();
	const TMap<FString, FAutomotiveCustomization> Customizations =
		State->GetCustomizations();
	const FAutomotiveCustomization* Customization =
		Customizations.Find(InteriorProxySurfaceId);
	if (Customization != nullptr)
	{
		if (Customization->Kind == EAutomotiveCustomizationKind::MaterialVariant)
		{
			const AutomotiveCatalog::FMaterialVariant* Variant =
				State->GetCatalogIndex().FindMaterialVariant(
					Customization->MaterialVariantId);
			if (Variant != nullptr)
			{
				FamilyId = Variant->MaterialFamilyId;
			}
		}
	}

	UMaterialInterface* InteriorMaterial =
		Library->LoadInteriorMaterial(FamilyId);
	if (!IsValid(InteriorMaterial))
	{
		LastError = FString::Printf(
			TEXT("内饰代理不支持 materialFamilyId=%s。"),
			*FamilyId);
		return false;
	}
	if (!IsValid(InteriorMaterialInstance)
		|| InteriorMaterialInstance->Parent != InteriorMaterial)
	{
		InteriorMaterialInstance =
			UMaterialInstanceDynamic::Create(InteriorMaterial, this);
	}
	if (!IsValid(InteriorMaterialInstance))
	{
		LastError = TEXT("无法创建内饰动态材质实例。");
		return false;
	}
	if (const AutomotiveCatalog::FMaterialVariant* Variant =
		Customization != nullptr
			&& Customization->Kind == EAutomotiveCustomizationKind::MaterialVariant
			? State->GetCatalogIndex().FindMaterialVariant(
				Customization->MaterialVariantId)
			: nullptr;
		Variant != nullptr && Variant->ColorCode.IsSet())
	{
		const TOptional<FLinearColor> VariantColor = ResolveCatalogColor(
			Variant->ColorCode.GetValue(),
			Variant->DisplayColorHex);
		if (VariantColor.IsSet())
		{
			InteriorMaterialInstance->SetVectorParameterValue(
				TEXT("BaseColor"),
				VariantColor.GetValue());
		}
	}
	else if (Option->ColorCode.IsSet())
	{
		const TOptional<FLinearColor> OptionColor =
			ResolveCatalogColor(Option->ColorCode.GetValue());
		if (OptionColor.IsSet())
		{
			InteriorMaterialInstance->SetVectorParameterValue(
				TEXT("BaseColor"),
				OptionColor.GetValue());
		}
	}
	InteriorComponent->SetMaterial(
		InteriorMaterialIndex,
		InteriorMaterialInstance);
	AppliedInteriorFamilyId = MoveTemp(FamilyId);
	return true;
}
