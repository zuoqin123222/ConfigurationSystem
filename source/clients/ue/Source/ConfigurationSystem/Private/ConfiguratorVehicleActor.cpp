#include "ConfiguratorVehicleActor.h"

#include "CarConfiguratorSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ReversiblePartActuatorComponent.h"
#include "Sc01MaterialBinder.h"
#include "SmoothWheelControllerComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

const FName AConfiguratorVehicleActor::PaintPartTag(TEXT("Configurator.Part.paint"));
const FName AConfiguratorVehicleActor::WheelPartTag(TEXT("Configurator.Part.wheel"));
const FName AConfiguratorVehicleActor::InteriorPartTag(TEXT("Configurator.Part.interior"));
const FName AConfiguratorVehicleActor::FramePartTag(TEXT("Configurator.Part.frame"));
const FName AConfiguratorVehicleActor::PaintSlotTag(TEXT("Configurator.Slot.paint_body"));
const FName AConfiguratorVehicleActor::WheelSlotTag(TEXT("Configurator.Slot.wheel_rim"));
const FName AConfiguratorVehicleActor::InteriorSlotTag(TEXT("Configurator.Slot.interior_trim"));
const FName AConfiguratorVehicleActor::FrameSlotTag(TEXT("Configurator.Slot.paint_frame"));
const FName AConfiguratorVehicleActor::TemporaryResourceTag(TEXT("Configurator.Resource.Temporary"));
const FName AConfiguratorVehicleActor::AuthorizedResourceTag(TEXT("Configurator.Resource.AuthorizedAudiA5"));

namespace ConfiguratorVehicle
{
	UStaticMesh* LoadOptionalStaticMesh(const TCHAR* ObjectPath)
	{
		return Cast<UStaticMesh>(StaticLoadObject(
			UStaticMesh::StaticClass(),
			nullptr,
			ObjectPath,
			nullptr,
			LOAD_NoWarn));
	}

	void MarkPartition(
		UActorComponent* Component,
		const FName PartTag,
		const FName SlotTag,
		const bool bAuthorized)
	{
		Component->ComponentTags.Add(PartTag);
		Component->ComponentTags.Add(SlotTag);
		Component->ComponentTags.Add(
			bAuthorized
				? AConfiguratorVehicleActor::AuthorizedResourceTag
				: AConfiguratorVehicleActor::TemporaryResourceTag);
	}
}

AConfiguratorVehicleActor::AConfiguratorVehicleActor()
{
	PrimaryActorTick.bCanEverTick = false;
	VehicleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	VehicleRoot->ComponentTags.Add(TEXT("Vehicle.Root"));
	SetRootComponent(VehicleRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UStaticMesh* AuthorizedBody = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/BodyMesh.BodyMesh"));
	UStaticMesh* AuthorizedInterior = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/InteriorMesh.InteriorMesh"));
	UStaticMesh* AuthorizedFrame = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/FrameMesh.FrameMesh"));
	UStaticMesh* AuthorizedDoorLeft = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/DoorMesh_FL.DoorMesh_FL"));
	UStaticMesh* AuthorizedDoorRight = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/DoorMesh_FR.DoorMesh_FR"));
	UStaticMesh* AuthorizedHood = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/HoodMesh.HoodMesh"));
	UStaticMesh* AuthorizedTrunk = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/TrunkMesh.TrunkMesh"));
	UStaticMesh* AuthorizedWheelFL = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/WheelMesh_FL.WheelMesh_FL"));
	UStaticMesh* AuthorizedWheelFR = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/WheelMesh_FR.WheelMesh_FR"));
	UStaticMesh* AuthorizedWheelRL = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/WheelMesh_RL.WheelMesh_RL"));
	UStaticMesh* AuthorizedWheelRR = ConfiguratorVehicle::LoadOptionalStaticMesh(
		TEXT("/Game/Configurator/AuthorizedAudiA5/WheelMesh_RR.WheelMesh_RR"));
	const bool bAuthorizedBody = AuthorizedBody != nullptr;
	Tags.Add(bAuthorizedBody ? AuthorizedResourceTag : TemporaryResourceTag);

	PaintBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PaintBody"));
	PaintBody->SetupAttachment(VehicleRoot);
	PaintBody->SetStaticMesh(bAuthorizedBody ? AuthorizedBody : CubeMesh.Object.Get());
	if (!bAuthorizedBody)
	{
		PaintBody->SetMaterial(0, BasicMaterial.Object);
		PaintBody->SetRelativeLocation(FVector(0.0, 0.0, 95.0));
		PaintBody->SetRelativeScale3D(FVector(4.6, 1.9, 0.55));
	}
	ConfiguratorVehicle::MarkPartition(PaintBody, PaintPartTag, PaintSlotTag, bAuthorizedBody);

	InteriorCabin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InteriorCabin"));
	InteriorCabin->SetupAttachment(VehicleRoot);
	const bool bAuthorizedInterior = AuthorizedInterior != nullptr;
	InteriorCabin->SetStaticMesh(
		bAuthorizedInterior ? AuthorizedInterior : CubeMesh.Object.Get());
	if (!bAuthorizedInterior)
	{
		InteriorCabin->SetMaterial(0, BasicMaterial.Object);
		InteriorCabin->SetRelativeLocation(FVector(-25.0, 0.0, 160.0));
		InteriorCabin->SetRelativeScale3D(FVector(2.0, 1.55, 0.55));
	}
	ConfiguratorVehicle::MarkPartition(
		InteriorCabin, InteriorPartTag, InteriorSlotTag, bAuthorizedInterior);
	// SC01 v2 最小闭环只驱动这一个明确代理槽，避免把 38 个 surface
	// 错误地广播到整车所有内饰 Mesh。
	InteriorCabin->ComponentTags.Add(USc01MaterialBinder::InteriorProxySlotTag);

	Frame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InternalFrame"));
	Frame->SetupAttachment(VehicleRoot);
	const bool bAuthorizedFrame = AuthorizedFrame != nullptr;
	Frame->SetStaticMesh(bAuthorizedFrame ? AuthorizedFrame : CubeMesh.Object.Get());
	if (!bAuthorizedFrame)
	{
		Frame->SetMaterial(0, BasicMaterial.Object);
		Frame->SetRelativeLocation(FVector(15.0, 0.0, 55.0));
		Frame->SetRelativeScale3D(FVector(3.8, 1.45, 0.12));
	}
	ConfiguratorVehicle::MarkPartition(Frame, FramePartTag, FrameSlotTag, bAuthorizedFrame);

	WheelGroup = CreateDefaultSubobject<USceneComponent>(TEXT("WheelGroup"));
	WheelGroup->SetupAttachment(VehicleRoot);
	WheelGroup->ComponentTags.Add(WheelPartTag);
	WheelGroup->ComponentTags.Add(WheelSlotTag);
	WheelGroup->ComponentTags.Add(
		AuthorizedWheelFL != nullptr && AuthorizedWheelFR != nullptr
			&& AuthorizedWheelRL != nullptr && AuthorizedWheelRR != nullptr
			? AuthorizedResourceTag : TemporaryResourceTag);

	const FVector WheelLocations[] = {
		FVector(20.4874, -78.8235, 1.6167),
		FVector(20.4874, 78.8235, 1.6167),
		FVector(-254.4983, -78.8235, 1.6167),
		FVector(-254.4983, 78.8235, 1.6167)
	};
	UStaticMesh* AuthorizedWheelMeshes[] = {
		AuthorizedWheelFL, AuthorizedWheelFR,
		AuthorizedWheelRL, AuthorizedWheelRR
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
		USceneComponent* SteeringPivot = CreateDefaultSubobject<USceneComponent>(
			*FString::Printf(TEXT("%sSteeringPivot"), WheelNames[Index]));
		SteeringPivot->SetupAttachment(WheelGroup);
		SteeringPivot->SetRelativeLocation(WheelLocations[Index]);
		WheelSteeringPivots.Add(SteeringPivot);

		USceneComponent* SpinPivot = CreateDefaultSubobject<USceneComponent>(
			*FString::Printf(TEXT("%sSpinPivot"), WheelNames[Index]));
		SpinPivot->SetupAttachment(SteeringPivot);
		if (AuthorizedWheelMeshes[Index] == nullptr)
		{
			// Engine Cylinder 的轴为本地 Z；Roll 90° 将轮轴对齐车辆横向 Y。
			SpinPivot->SetRelativeRotation(FRotator(0.0, 0.0, 90.0));
		}
		WheelSpinPivots.Add(SpinPivot);

		UStaticMeshComponent* Wheel =
			CreateDefaultSubobject<UStaticMeshComponent>(WheelNames[Index]);
		Wheel->SetupAttachment(SpinPivot);
		const bool bAuthorizedWheel = AuthorizedWheelMeshes[Index] != nullptr;
		Wheel->SetStaticMesh(
			bAuthorizedWheel ? AuthorizedWheelMeshes[Index] : CylinderMesh.Object.Get());
		if (!bAuthorizedWheel)
		{
			Wheel->SetMaterial(0, BasicMaterial.Object);
			Wheel->SetRelativeScale3D(FVector(0.72, 0.72, 0.38));
		}
		else
		{
			Wheel->SetRelativeLocation(-WheelLocations[Index]);
		}
		ConfiguratorVehicle::MarkPartition(
			Wheel, WheelPartTag, WheelSlotTag, bAuthorizedWheel);
		Wheel->ComponentTags.Add(WheelControlTags[Index]);
		Wheels.Add(Wheel);
	}

	const auto CreateActuatedPanel = [this](
		const FName PivotName,
		const FName Name,
		const FVector& PivotLocation,
		const FVector& PanelOffset,
		const FVector& Scale,
		UStaticMesh* AuthorizedMesh)
	{
		USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(PivotName);
		Pivot->SetupAttachment(VehicleRoot);
		Pivot->SetRelativeLocation(PivotLocation);
		UStaticMeshComponent* Panel = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Panel->SetupAttachment(Pivot);
		const bool bAuthorized = AuthorizedMesh != nullptr;
		Panel->SetStaticMesh(bAuthorized ? AuthorizedMesh : CubeMesh.Object.Get());
		if (!bAuthorized)
		{
			Panel->SetMaterial(0, BasicMaterial.Object);
			Panel->SetRelativeLocation(PanelOffset);
			Panel->SetRelativeScale3D(Scale);
		}
		else
		{
			Panel->SetRelativeLocation(-PivotLocation);
		}
		Panel->ComponentTags.Add(
			bAuthorized ? AuthorizedResourceTag : TemporaryResourceTag);
		Panel->ComponentTags.Add(TEXT("Configurator.Part.Actuated"));
		return TPair<USceneComponent*, UStaticMeshComponent*>(Pivot, Panel);
	};

	const auto LeftDoorParts = CreateActuatedPanel(
		TEXT("LeftDoorHingePivot"), TEXT("DoorLeft"),
		FVector(-35.9232, -82.8285, 59.6402), FVector(175.0, 0.0, 0.0),
		FVector(1.75, 0.08, 0.52), AuthorizedDoorLeft);
	LeftDoorPivot = LeftDoorParts.Key;
	LeftDoor = LeftDoorParts.Value;
	const auto RightDoorParts = CreateActuatedPanel(
		TEXT("RightDoorHingePivot"), TEXT("DoorRight"),
		FVector(-35.9232, 82.8285, 59.6402), FVector(175.0, 0.0, 0.0),
		FVector(1.75, 0.08, 0.52), AuthorizedDoorRight);
	RightDoorPivot = RightDoorParts.Key;
	RightDoor = RightDoorParts.Value;
	const auto HoodParts = CreateActuatedPanel(
		TEXT("HoodHingePivot"), TEXT("Hood"),
		FVector(-43.4899, 0.0, 95.0235), FVector(135.0, 0.0, 0.0),
		FVector(1.35, 1.72, 0.08), AuthorizedHood);
	HoodPivot = HoodParts.Key;
	Hood = HoodParts.Value;
	const auto TrunkParts = CreateActuatedPanel(
		TEXT("TrunkHingePivot"), TEXT("Trunk"),
		FVector(-298.9839, 0.0, 103.8786), FVector(-105.0, 0.0, 0.0),
		FVector(1.05, 1.72, 0.08), AuthorizedTrunk);
	TrunkPivot = TrunkParts.Key;
	Trunk = TrunkParts.Value;

	LeftDoorActuator = CreateDefaultSubobject<UReversiblePartActuatorComponent>(
		TEXT("LeftDoorActuator"));
	RightDoorActuator = CreateDefaultSubobject<UReversiblePartActuatorComponent>(
		TEXT("RightDoorActuator"));
	HoodActuator = CreateDefaultSubobject<UReversiblePartActuatorComponent>(
		TEXT("HoodActuator"));
	TrunkActuator = CreateDefaultSubobject<UReversiblePartActuatorComponent>(
		TEXT("TrunkActuator"));
	WheelController = CreateDefaultSubobject<USmoothWheelControllerComponent>(
		TEXT("SmoothWheelController"));
}

void AConfiguratorVehicleActor::BeginPlay()
{
	Super::BeginPlay();
	LeftDoorActuator->BindPart(
		LeftDoorPivot,
		LeftDoorPivot->GetRelativeTransform(),
		FTransform(FRotator(0.0, 58.0, 0.0), LeftDoorPivot->GetRelativeLocation()));
	RightDoorActuator->BindPart(
		RightDoorPivot,
		RightDoorPivot->GetRelativeTransform(),
		FTransform(FRotator(0.0, -58.0, 0.0), RightDoorPivot->GetRelativeLocation()));
	HoodActuator->BindPart(
		HoodPivot,
		HoodPivot->GetRelativeTransform(),
		FTransform(FRotator(-32.0, 0.0, 0.0), HoodPivot->GetRelativeLocation()));
	TrunkActuator->BindPart(
		TrunkPivot,
		TrunkPivot->GetRelativeTransform(),
		FTransform(FRotator(35.0, 0.0, 0.0), TrunkPivot->GetRelativeLocation()));
	TArray<USceneComponent*> SteeringPivots;
	TArray<USceneComponent*> SpinPivots;
	for (USceneComponent* Pivot : WheelSteeringPivots) { SteeringPivots.Add(Pivot); }
	for (USceneComponent* Pivot : WheelSpinPivots) { SpinPivots.Add(Pivot); }
	WheelController->BindWheels(SteeringPivots, SpinPivots);
	WheelController->SetWheelTargets(0.0f, 0.0f);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UCarConfiguratorSubsystem* Configurator =
			GameInstance->GetSubsystem<UCarConfiguratorSubsystem>())
		{
			Configurator->RegisterVehicle(this);
		}
	}
}

void AConfiguratorVehicleActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UCarConfiguratorSubsystem* Configurator =
			GameInstance->GetSubsystem<UCarConfiguratorSubsystem>())
		{
			Configurator->UnregisterVehicle(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AConfiguratorVehicleActor::TogglePart(const FName PartId)
{
	UReversiblePartActuatorComponent* Actuator = nullptr;
	if (PartId == TEXT("door-left")) { Actuator = LeftDoorActuator; }
	else if (PartId == TEXT("door-right")) { Actuator = RightDoorActuator; }
	else if (PartId == TEXT("hood")) { Actuator = HoodActuator; }
	else if (PartId == TEXT("trunk")) { Actuator = TrunkActuator; }
	if (!IsValid(Actuator))
	{
		return false;
	}
	Actuator->Toggle();
	return true;
}

bool AConfiguratorVehicleActor::SetPartOpen(const FName PartId, const bool bOpen)
{
	UReversiblePartActuatorComponent* Actuator = nullptr;
	if (PartId == TEXT("door-left")) { Actuator = LeftDoorActuator; }
	else if (PartId == TEXT("door-right")) { Actuator = RightDoorActuator; }
	else if (PartId == TEXT("hood")) { Actuator = HoodActuator; }
	else if (PartId == TEXT("trunk")) { Actuator = TrunkActuator; }
	if (!IsValid(Actuator))
	{
		return false;
	}
	Actuator->SetOpen(bOpen);
	return true;
}

void AConfiguratorVehicleActor::SetWheelMotion(
	const float SteeringDegrees,
	const float SpinDegreesPerSecond)
{
	if (WheelController != nullptr)
	{
		bWheelsSpinning = !FMath::IsNearlyZero(SpinDegreesPerSecond);
		WheelController->SetWheelTargets(SteeringDegrees, SpinDegreesPerSecond);
	}
}

bool AConfiguratorVehicleActor::ToggleWheelSpin()
{
	SetWheelMotion(0.0f, bWheelsSpinning ? 0.0f : 180.0f);
	return bWheelsSpinning;
}

void AConfiguratorVehicleActor::ApplyConfiguration(
	const FCarConfigurationSelection& Selection)
{
	const FLinearColor PaintColor =
		Selection.Paint == TEXT("paint-silver")
			? FLinearColor(0.58f, 0.62f, 0.66f)
			: FLinearColor(0.72f, 0.015f, 0.02f);
	const FLinearColor InteriorColor =
		Selection.Interior == TEXT("interior-ivory")
			? FLinearColor(0.82f, 0.75f, 0.58f)
			: FLinearColor(0.015f, 0.018f, 0.022f);
	SetMaterialFamilyColor(TEXT("M_A5_Paint"), PaintColor);
	SetMaterialFamilyColor(TEXT("M_A5_Interior"), InteriorColor);

	if (PaintBody->ComponentHasTag(TemporaryResourceTag))
	{
		SetComponentColor(PaintBody, PaintMaterial, PaintColor);
	}
	if (InteriorCabin->ComponentHasTag(TemporaryResourceTag))
	{
		SetComponentColor(InteriorCabin, InteriorMaterial, InteriorColor);
	}
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

void AConfiguratorVehicleActor::SetMaterialFamilyColor(
	const FName& MaterialName,
	const FLinearColor& Color)
{
	TInlineComponentArray<UStaticMeshComponent*> MeshComponents(this);
	for (UStaticMeshComponent* Component : MeshComponents)
	{
		if (!IsValid(Component))
		{
			continue;
		}
		for (int32 MaterialIndex = 0;
			MaterialIndex < Component->GetNumMaterials();
			++MaterialIndex)
		{
			UMaterialInterface* Current = Component->GetMaterial(MaterialIndex);
			UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Current);
			UMaterialInterface* Base =
				Dynamic != nullptr ? Dynamic->Parent.Get() : Current;
			if (!IsValid(Base) || Base->GetFName() != MaterialName)
			{
				continue;
			}
			if (Dynamic == nullptr)
			{
				Dynamic = Component->CreateAndSetMaterialInstanceDynamic(MaterialIndex);
				if (Dynamic != nullptr)
				{
					RuntimeMaterialInstances.Add(Dynamic);
				}
			}
			if (Dynamic != nullptr)
			{
				Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
			}
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
			|| (!Component->ComponentHasTag(TemporaryResourceTag)
				&& !Component->ComponentHasTag(AuthorizedResourceTag)))
		{
			OutErrors.Add(FString::Printf(TEXT("%s 标签或资源声明不完整。"), Label));
		}
	};

	Validate(PaintBody, PaintPartTag, PaintSlotTag, TEXT("paint"));
	Validate(InteriorCabin, InteriorPartTag, InteriorSlotTag, TEXT("interior"));
	if (InteriorCabin == nullptr
		|| !InteriorCabin->ComponentHasTag(USc01MaterialBinder::InteriorProxySlotTag))
	{
		OutErrors.Add(TEXT("内饰代理必须包含唯一 SC01 v2 材质槽标签。"));
	}
	Validate(Frame, FramePartTag, FrameSlotTag, TEXT("frame"));
	Validate(WheelGroup, WheelPartTag, WheelSlotTag, TEXT("wheel"));
	for (const UStaticMeshComponent* Panel : { LeftDoor, RightDoor, Hood, Trunk })
	{
		if (Panel == nullptr
			|| (!Panel->ComponentHasTag(TemporaryResourceTag)
				&& !Panel->ComponentHasTag(AuthorizedResourceTag))
			|| !Panel->ComponentHasTag(TEXT("Configurator.Part.Actuated")))
		{
			OutErrors.Add(TEXT("可逆车身部件缺少资源或执行器绑定标签。"));
		}
	}
	if (LeftDoorPivot == nullptr || RightDoorPivot == nullptr
		|| HoodPivot == nullptr || TrunkPivot == nullptr
		|| LeftDoor == nullptr || LeftDoor->GetAttachParent() != LeftDoorPivot
		|| RightDoor == nullptr || RightDoor->GetAttachParent() != RightDoorPivot
		|| Hood == nullptr || Hood->GetAttachParent() != HoodPivot
		|| Trunk == nullptr || Trunk->GetAttachParent() != TrunkPivot)
	{
		OutErrors.Add(TEXT("活动件必须挂载到各自真实铰链 Pivot。"));
	}
	if (LeftDoorActuator == nullptr || RightDoorActuator == nullptr
		|| HoodActuator == nullptr || TrunkActuator == nullptr || WheelController == nullptr)
	{
		OutErrors.Add(TEXT("车身可逆执行器或平滑车轮控制器不完整。"));
	}
	if (Wheels.Num() != 4)
	{
		OutErrors.Add(TEXT("wheel 分区必须固定包含四个占位轮。"));
	}
	if (WheelSteeringPivots.Num() != 4 || WheelSpinPivots.Num() != 4)
	{
		OutErrors.Add(TEXT("车轮必须固定包含四组独立转向/滚动层级。"));
	}
	for (int32 Index = 0; Index < Wheels.Num(); ++Index)
	{
		const UStaticMeshComponent* Wheel = Wheels[Index];
		Validate(Wheel, WheelPartTag, WheelSlotTag, TEXT("wheel mesh"));
		if (!WheelSpinPivots.IsValidIndex(Index)
			|| !WheelSteeringPivots.IsValidIndex(Index)
			|| !IsValid(WheelSpinPivots[Index])
			|| !IsValid(WheelSteeringPivots[Index])
			|| Wheel == nullptr
			|| Wheel->GetAttachParent() != WheelSpinPivots[Index]
			|| WheelSpinPivots[Index]->GetAttachParent() != WheelSteeringPivots[Index]
			|| (!Wheel->ComponentHasTag(AuthorizedResourceTag)
				&& !FMath::IsNearlyEqual(
					FMath::Abs(WheelSpinPivots[Index]->GetRelativeRotation().Roll),
					90.0f)))
		{
			OutErrors.Add(TEXT("车轮轴向或 Steering/Spin/Mesh 层级不正确。"));
		}
	}
	return OutErrors.IsEmpty()
		&& (Tags.Contains(TemporaryResourceTag) || Tags.Contains(AuthorizedResourceTag));
}
