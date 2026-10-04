#include "AutomotiveMaterialBinder.h"

#include "Components/MeshComponent.h"
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

	FString SlotError;
	UMeshComponent* CandidatePaint =
		FindUniqueTaggedMesh(InVehicle, PaintProxySlotTag, SlotError);
	if (CandidatePaint == nullptr)
	{
		LastError = MoveTemp(SlotError);
		return false;
	}
	UMeshComponent* CandidateInterior =
		FindUniqueTaggedMesh(InVehicle, InteriorProxySlotTag, SlotError);
	if (CandidateInterior == nullptr)
	{
		LastError = MoveTemp(SlotError);
		return false;
	}

	State = InState;
	Library = InLibrary;
	PaintComponent = CandidatePaint;
	InteriorComponent = CandidateInterior;
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
	PaintComponent->SetMaterial(0, PaintMaterialInstance);

	const TMap<FString, FAutomotiveCustomization> Customizations =
		State->GetCustomizations();
	const FAutomotiveCustomization* Customization = Customizations.Find(PaintSurfaceId);
	if (Customization == nullptr
		|| Customization->Kind != EAutomotiveCustomizationKind::Paint)
	{
		return true;
	}

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
	if (const FAutomotiveCustomization* Customization =
		Customizations.Find(InteriorProxySurfaceId))
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
	InteriorComponent->SetMaterial(0, InteriorMaterial);
	AppliedInteriorFamilyId = MoveTemp(FamilyId);
	return true;
}
