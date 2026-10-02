#include "ConfiguratorVehicleActor.h"

#include "CarConfiguratorSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

const FName AConfiguratorVehicleActor::PaintPartTag(TEXT("Configurator.Part.paint"));
const FName AConfiguratorVehicleActor::WheelPartTag(TEXT("Configurator.Part.wheel"));
const FName AConfiguratorVehicleActor::InteriorPartTag(TEXT("Configurator.Part.interior"));
const FName AConfiguratorVehicleActor::FramePartTag(TEXT("Configurator.Part.frame"));
const FName AConfiguratorVehicleActor::PaintSlotTag(TEXT("Configurator.Slot.paint_body"));
const FName AConfiguratorVehicleActor::WheelSlotTag(TEXT("Configurator.Slot.wheel_rim"));
const FName AConfiguratorVehicleActor::InteriorSlotTag(TEXT("Configurator.Slot.interior_trim"));
const FName AConfiguratorVehicleActor::FrameSlotTag(TEXT("Configurator.Slot.paint_frame"));
const FName AConfiguratorVehicleActor::TemporaryResourceTag(TEXT("Configurator.Resource.Temporary"));

namespace ConfiguratorVehicle
{
	void MarkPartition(
		UActorComponent* Component,
		const FName PartTag,
		const FName SlotTag)
	{
		Component->ComponentTags.Add(PartTag);
		Component->ComponentTags.Add(SlotTag);
		Component->ComponentTags.Add(AConfiguratorVehicleActor::TemporaryResourceTag);
	}
}

AConfiguratorVehicleActor::AConfiguratorVehicleActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Tags.Add(TemporaryResourceTag);

	VehicleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	VehicleRoot->ComponentTags.Add(TEXT("Vehicle.Root"));
	SetRootComponent(VehicleRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	PaintBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PaintBody"));
	PaintBody->SetupAttachment(VehicleRoot);
	PaintBody->SetStaticMesh(CubeMesh.Object);
	PaintBody->SetMaterial(0, BasicMaterial.Object);
	PaintBody->SetRelativeLocation(FVector(0.0, 0.0, 95.0));
	PaintBody->SetRelativeScale3D(FVector(4.6, 1.9, 0.55));
	ConfiguratorVehicle::MarkPartition(PaintBody, PaintPartTag, PaintSlotTag);

	InteriorCabin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InteriorCabin"));
	InteriorCabin->SetupAttachment(VehicleRoot);
	InteriorCabin->SetStaticMesh(CubeMesh.Object);
	InteriorCabin->SetMaterial(0, BasicMaterial.Object);
	InteriorCabin->SetRelativeLocation(FVector(-25.0, 0.0, 160.0));
	InteriorCabin->SetRelativeScale3D(FVector(2.0, 1.55, 0.55));
	ConfiguratorVehicle::MarkPartition(InteriorCabin, InteriorPartTag, InteriorSlotTag);

	Frame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InternalFrame"));
	Frame->SetupAttachment(VehicleRoot);
	Frame->SetStaticMesh(CubeMesh.Object);
	Frame->SetMaterial(0, BasicMaterial.Object);
	Frame->SetRelativeLocation(FVector(15.0, 0.0, 55.0));
	Frame->SetRelativeScale3D(FVector(3.8, 1.45, 0.12));
	ConfiguratorVehicle::MarkPartition(Frame, FramePartTag, FrameSlotTag);

	WheelGroup = CreateDefaultSubobject<USceneComponent>(TEXT("WheelGroup"));
	WheelGroup->SetupAttachment(VehicleRoot);
	WheelGroup->ComponentTags.Add(WheelPartTag);
	WheelGroup->ComponentTags.Add(WheelSlotTag);
	WheelGroup->ComponentTags.Add(TemporaryResourceTag);

	const FVector WheelLocations[] = {
		FVector(275.0, -175.0, 55.0),
		FVector(275.0, 175.0, 55.0),
		FVector(-275.0, -175.0, 55.0),
		FVector(-275.0, 175.0, 55.0)
	};
	const TCHAR* WheelNames[] = {
		TEXT("WheelFrontLeft"),
		TEXT("WheelFrontRight"),
		TEXT("WheelRearLeft"),
		TEXT("WheelRearRight")
	};
	const FName WheelControlTags[] = {
		TEXT("Vehicle.Part.Wheel.FrontLeft"),
		TEXT("Vehicle.Part.Wheel.FrontRight"),
		TEXT("Vehicle.Part.Wheel.RearLeft"),
		TEXT("Vehicle.Part.Wheel.RearRight")
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(WheelLocations); ++Index)
	{
		UStaticMeshComponent* Wheel =
			CreateDefaultSubobject<UStaticMeshComponent>(WheelNames[Index]);
		Wheel->SetupAttachment(WheelGroup);
		Wheel->SetStaticMesh(CylinderMesh.Object);
		Wheel->SetMaterial(0, BasicMaterial.Object);
		Wheel->SetRelativeLocation(WheelLocations[Index]);
		Wheel->SetRelativeRotation(FRotator(90.0, 0.0, 0.0));
		Wheel->SetRelativeScale3D(FVector(0.72, 0.72, 0.38));
		ConfiguratorVehicle::MarkPartition(Wheel, WheelPartTag, WheelSlotTag);
		Wheel->ComponentTags.Add(WheelControlTags[Index]);
		Wheels.Add(Wheel);
	}
}

void AConfiguratorVehicleActor::BeginPlay()
{
	Super::BeginPlay();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UCarConfiguratorSubsystem* Configurator =
			GameInstance->GetSubsystem<UCarConfiguratorSubsystem>())
		{
			Configurator->RegisterVehicle(this);
		}
	}
}

void AConfiguratorVehicleActor::ApplyConfiguration(
	const FCarConfigurationSelection& Selection)
{
	SetComponentColor(
		PaintBody,
		PaintMaterial,
		Selection.Paint == TEXT("paint-silver")
			? FLinearColor(0.58f, 0.62f, 0.66f)
			: FLinearColor(0.72f, 0.015f, 0.02f));
	SetComponentColor(
		InteriorCabin,
		InteriorMaterial,
		Selection.Interior == TEXT("interior-ivory")
			? FLinearColor(0.82f, 0.75f, 0.58f)
			: FLinearColor(0.015f, 0.018f, 0.022f));
	SetComponentColor(
		Frame,
		FrameMaterial,
		Selection.Frame == TEXT("frame-red")
			? FLinearColor(0.8f, 0.01f, 0.015f)
			: FLinearColor(0.025f, 0.025f, 0.025f));

	const FLinearColor WheelColor =
		Selection.Wheel == TEXT("wheel-forged")
			? FLinearColor(0.5f, 0.52f, 0.55f)
			: FLinearColor(0.04f, 0.045f, 0.05f);
	for (UStaticMeshComponent* Wheel : Wheels)
	{
		SetComponentColor(Wheel, WheelMaterial, WheelColor);
		if (WheelMaterial != nullptr)
		{
			Wheel->SetMaterial(0, WheelMaterial);
		}
	}
}

UMaterialInstanceDynamic* AConfiguratorVehicleActor::GetOrCreateMaterial(
	UStaticMeshComponent* Component,
	TObjectPtr<UMaterialInstanceDynamic>& Storage)
{
	if (Storage == nullptr && Component != nullptr)
	{
		Storage = Component->CreateAndSetMaterialInstanceDynamic(0);
	}
	return Storage;
}

void AConfiguratorVehicleActor::SetComponentColor(
	UStaticMeshComponent* Component,
	TObjectPtr<UMaterialInstanceDynamic>& Storage,
	const FLinearColor& Color)
{
	if (UMaterialInstanceDynamic* Material = GetOrCreateMaterial(Component, Storage))
	{
		Material->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

bool AConfiguratorVehicleActor::HasStablePlaceholderBindings(
	TArray<FString>& OutErrors) const
{
	OutErrors.Reset();
	const auto Validate = [&OutErrors](
		const UActorComponent* Component,
		const FName PartTag,
		const FName SlotTag,
		const TCHAR* Label)
	{
		if (Component == nullptr
			|| !Component->ComponentHasTag(PartTag)
			|| !Component->ComponentHasTag(SlotTag)
			|| !Component->ComponentHasTag(TemporaryResourceTag))
		{
			OutErrors.Add(FString::Printf(TEXT("%s 标签或临时资源声明不完整。"), Label));
		}
	};

	Validate(PaintBody, PaintPartTag, PaintSlotTag, TEXT("paint"));
	Validate(InteriorCabin, InteriorPartTag, InteriorSlotTag, TEXT("interior"));
	Validate(Frame, FramePartTag, FrameSlotTag, TEXT("frame"));
	Validate(WheelGroup, WheelPartTag, WheelSlotTag, TEXT("wheel"));
	if (Wheels.Num() != 4)
	{
		OutErrors.Add(TEXT("wheel 分区必须固定包含四个占位轮。"));
	}
	for (const UStaticMeshComponent* Wheel : Wheels)
	{
		Validate(Wheel, WheelPartTag, WheelSlotTag, TEXT("wheel mesh"));
	}
	return OutErrors.IsEmpty() && Tags.Contains(TemporaryResourceTag);
}
