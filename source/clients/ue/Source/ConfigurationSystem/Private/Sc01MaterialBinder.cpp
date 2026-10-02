#include "Sc01MaterialBinder.h"

#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sc01MaterialLibrary.h"
#include "Sc01V2ConfigurationState.h"

const FName USc01MaterialBinder::PaintProxySlotTag(
	TEXT("Configurator.Slot.paint_body"));
const FName USc01MaterialBinder::InteriorProxySlotTag(
	TEXT("Configurator.Slot.sc01_interior_material_proxy"));
const FString USc01MaterialBinder::PaintSurfaceId(TEXT("exterior-body-cover"));
const FString USc01MaterialBinder::InteriorProxySurfaceId(TEXT("door-middle"));

UMeshComponent* USc01MaterialBinder::FindUniqueTaggedMesh(
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

bool USc01MaterialBinder::Bind(
	USc01V2ConfigurationState* InState,
	USc01MaterialLibrary* InLibrary,
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
	State->OnChangedNative.AddUObject(this, &USc01MaterialBinder::HandleStateChanged);
	return ApplyCurrentConfiguration();
}

void USc01MaterialBinder::Unbind()
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

void USc01MaterialBinder::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

void USc01MaterialBinder::HandleStateChanged()
{
	ApplyCurrentConfiguration();
}

bool USc01MaterialBinder::ApplyCurrentConfiguration()
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

bool USc01MaterialBinder::ApplyPaint()
{
	UMaterialInterface* PaintMaster = Library->CarPaint.LoadSynchronous();
	if (!IsValid(PaintMaster))
	{
		LastError = TEXT("Sc01MaterialLibrary 缺少 CarPaint。");
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

	const TMap<FString, FSc01V2Customization> Customizations =
		State->GetCustomizations();
	const FSc01V2Customization* Customization = Customizations.Find(PaintSurfaceId);
	if (Customization == nullptr
		|| Customization->Kind != ESc01V2CustomizationKind::Paint)
	{
		return true;
	}

	const FSc01V2PaintCustomization& Paint = Customization->Paint;
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

bool USc01MaterialBinder::ApplyInterior()
{
	const FString OptionId =
		State->GetSelections().FindRef(InteriorProxySurfaceId);
	const Sc01V2::FOption* Option =
		State->GetCatalogIndex().FindOption(OptionId);
	if (Option == nullptr || !Option->MaterialFamilyId.IsSet())
	{
		LastError = TEXT("内饰代理 surface 没有可解析的 materialFamilyId。");
		return false;
	}

	FString FamilyId = Option->MaterialFamilyId.GetValue();
	const TMap<FString, FSc01V2Customization> Customizations =
		State->GetCustomizations();
	if (const FSc01V2Customization* Customization =
		Customizations.Find(InteriorProxySurfaceId))
	{
		if (Customization->Kind == ESc01V2CustomizationKind::MaterialVariant)
		{
			const Sc01V2::FMaterialVariant* Variant =
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
