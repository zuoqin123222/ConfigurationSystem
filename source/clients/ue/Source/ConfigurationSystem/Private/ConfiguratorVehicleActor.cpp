#include "ConfiguratorVehicleActor.h"

#include "CarConfiguratorSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ReversiblePartActuatorComponent.h"
#include "AutomotiveMaterialBinder.h"
#include "SmoothWheelControllerComponent.h"
#include "VehicleAnimSequencePlayerComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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
	// CDF1A166 审计边界：X[-358.6124268, 109.1891403]，
	// Y[-102.7749023, 102.7748260]，轮胎最低点 Z=-32.3401680。
	// 展厅地面顶面统一为 Z=0，因此内容根把轮胎最低点精确抬到零平面。
	const FVector ContentRootOffset(124.7116432, 0.0000381, 32.3401680);

	UStaticMesh* LoadOptionalStaticMesh(const TCHAR* ObjectPath)
	{
		return Cast<UStaticMesh>(StaticLoadObject(
			UStaticMesh::StaticClass(),
			nullptr,
			ObjectPath,
			nullptr,
			LOAD_NoWarn));
	}

	UStaticMesh* LoadIndependentStaticMesh(const TCHAR* AssetName)
	{
		const FString AuthorizedPath = FString::Printf(
			TEXT("/Game/Configurator/AuthorizedAudiA5/%s.%s"),
			AssetName,
			AssetName);
		if (UStaticMesh* Mesh = LoadOptionalStaticMesh(*AuthorizedPath))
		{
			return Mesh;
		}

		// 暂存资产会在 Actor CDO 的默认子组件上形成硬引用，Shipping Cook
		// 可沿组件的 StaticMesh 属性收集依赖，不依赖编辑器目录扫描。
		const FString StagingPath = FString::Printf(
			TEXT("/Game/Configurator/_ImportStaging/CDF1A16663CD47AF963602DE04499D19/%s.%s"),
			AssetName,
			AssetName);
		return LoadOptionalStaticMesh(*StagingPath);
	}

	UStaticMesh* LoadStagingStaticMesh(const TCHAR* AssetName)
	{
		const FString StagingPath = FString::Printf(
			TEXT("/Game/Configurator/_ImportStaging/CDF1A16663CD47AF963602DE04499D19/%s.%s"),
			AssetName,
			AssetName);
		return LoadOptionalStaticMesh(*StagingPath);
	}

	template <typename T>
	T* LoadOptionalAsset(const TCHAR* ObjectPath)
	{
		return Cast<T>(StaticLoadObject(T::StaticClass(), nullptr, ObjectPath, nullptr, LOAD_NoWarn));
	}

	UMaterialInterface* LoadSkeletalValidationMaterial(const FName SlotName)
	{
		static const TMap<FName, const TCHAR*> MaterialPaths = {
			{TEXT("CS_Validation_Glass"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_Glass.M_A5_Glass")},
			{TEXT("CS_Validation_Interior"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_Interior.M_A5_Interior")},
			{TEXT("CS_Validation_LightClear"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_LightClear.M_A5_LightClear")},
			{TEXT("CS_Validation_LightRed"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_LightRed.M_A5_LightRed")},
			{TEXT("CS_Validation_Metal"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_Metal.M_A5_Metal")},
			{TEXT("CS_Validation_Paint"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_Paint.M_A5_Paint")},
			{TEXT("CS_Validation_Plastic"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_Plastic.M_A5_Plastic")},
			{TEXT("CS_Validation_Rubber"),
				TEXT("/Game/Configurator/AuthorizedAudiA5/Materials/M_A5_Rubber.M_A5_Rubber")}
		};
		FName ResolvedSlotName = SlotName;
		const FString SlotText = SlotName.ToString();
		if (SlotText.StartsWith(TEXT("sc01_")))
		{
			if (SlotName == TEXT("sc01_exterior_body_cover"))
			{
				ResolvedSlotName = TEXT("CS_Validation_Paint");
			}
			else if (SlotText.Contains(TEXT("wheel"))
				|| SlotText.Contains(TEXT("caliper"))
				|| SlotName == TEXT("sc01_lower_skirt")
				|| SlotName == TEXT("sc01_pedal"))
			{
				ResolvedSlotName = TEXT("CS_Validation_Metal");
			}
			else
			{
				ResolvedSlotName = TEXT("CS_Validation_Interior");
			}
		}
		const TCHAR* const* ObjectPath = MaterialPaths.Find(ResolvedSlotName);
		return ObjectPath != nullptr
			? LoadOptionalAsset<UMaterialInterface>(*ObjectPath)
			: nullptr;
	}

	void PopulateMissingSkeletalMaterials(
		USkeletalMeshComponent* Component,
		const USkeletalMesh* Mesh)
	{
		if (!IsValid(Component) || !IsValid(Mesh))
		{
			return;
		}
		const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
		for (int32 Index = 0; Index < Materials.Num(); ++Index)
		{
			const UMaterialInterface* ExistingMaterial =
				Component->GetMaterial(Index);
			const bool bNeedsProjectMaterial =
				ExistingMaterial == nullptr
				|| ExistingMaterial->GetPathName().Contains(
					TEXT("DefaultMaterial"))
				|| ExistingMaterial->GetPathName().Contains(
					TEXT("WorldGridMaterial"));
			if (bNeedsProjectMaterial)
			{
				if (UMaterialInterface* Material =
					LoadSkeletalValidationMaterial(Materials[Index].MaterialSlotName))
				{
					Component->SetMaterial(Index, Material);
					UE_LOG(
						LogTemp,
						Display,
						TEXT("骨骼车辆材质补齐：slot=%d name=%s material=%s"),
						Index,
						*Materials[Index].MaterialSlotName.ToString(),
						*Material->GetPathName());
				}
				else
				{
					UE_LOG(
						LogTemp,
						Error,
						TEXT("骨骼车辆材质补齐失败：slot=%d name=%s"),
						Index,
						*Materials[Index].MaterialSlotName.ToString());
				}
			}
		}
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
	PrimaryActorTick.bCanEverTick = true;
	VehicleRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	VehicleRoot->ComponentTags.Add(TEXT("Vehicle.Root"));
	SetRootComponent(VehicleRoot);

	ContentRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleContentRoot"));
	ContentRoot->SetupAttachment(VehicleRoot);
	ContentRoot->SetRelativeLocation(ConfiguratorVehicle::ContentRootOffset);
	ContentRoot->ComponentTags.Add(TEXT("Vehicle.ContentRoot"));

	SkeletalVehicle = CreateDefaultSubobject<USkeletalMeshComponent>(
		TEXT("SkeletalVehicle"));
	SkeletalVehicle->SetupAttachment(ContentRoot);
	// 骨骼 FBX 已在 DCC 阶段将最低点归零；抵消静态分件代理专用的 Z 抬升，
	// 但保留 ContentRoot 的 X 几何居中偏移。
	SkeletalVehicle->SetRelativeLocation(
		FVector(0.0, 0.0, -ConfiguratorVehicle::ContentRootOffset.Z));
	SkeletalVehicle->SetVisibility(false);
	SkeletalVehicle->SetHiddenInGame(true);

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
	// 正式语义 TrunkMesh 合并了敞篷收纳机构；优先使用暂存中的纯后盖分件。
	UStaticMesh* AuthorizedTrunk =
		ConfiguratorVehicle::LoadStagingStaticMesh(TEXT("TrunkMesh"));
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
	PaintBody->SetupAttachment(ContentRoot);
	PaintBody->SetStaticMesh(bAuthorizedBody ? AuthorizedBody : CubeMesh.Object.Get());
	if (!bAuthorizedBody)
	{
		PaintBody->SetMaterial(0, BasicMaterial.Object);
		PaintBody->SetRelativeLocation(FVector(0.0, 0.0, 95.0));
		PaintBody->SetRelativeScale3D(FVector(4.6, 1.9, 0.55));
	}
	ConfiguratorVehicle::MarkPartition(PaintBody, PaintPartTag, PaintSlotTag, bAuthorizedBody);

	InteriorCabin = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InteriorCabin"));
	InteriorCabin->SetupAttachment(ContentRoot);
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
	// 车型目录 v2 最小闭环只驱动这一个明确代理槽，避免把 40 个 surface
	// 错误地广播到整车所有内饰 Mesh。
	InteriorCabin->ComponentTags.Add(UAutomotiveMaterialBinder::InteriorProxySlotTag);

	Frame = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("InternalFrame"));
	Frame->SetupAttachment(ContentRoot);
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
	WheelGroup->SetupAttachment(ContentRoot);
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
	const TCHAR* TireAssetNames[] = {
		TEXT("SM_tireAFrontLeft"), TEXT("SM_tireAFrontRight"),
		TEXT("SM_tireARearLeft"), TEXT("SM_tireARearRight")
	};
	const TCHAR* RimAssetNames[] = {
		TEXT("WheelMesh_FL"), TEXT("WheelMesh_FR"),
		TEXT("WheelMesh_RL"), TEXT("WheelMesh_RR")
	};
	const TCHAR* RotorAssetNames[] = {
		TEXT("SM_brakeRotorFrontLeft"), TEXT("SM_brakeRotorFrontRight"),
		TEXT("SM_brakeRotorRearLeft"), TEXT("SM_brakeRotorRearRight")
	};
	const TCHAR* CaliperAssetNames[] = {
		TEXT("SM_brakeCaliperFrontLeft"), TEXT("SM_brakeCaliperFrontRight"),
		TEXT("SM_brakeCaliperRearleft"), TEXT("SM_brakeCaliperRearRight")
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
		WheelSpinPivots.Add(SpinPivot);

		UStaticMesh* RimMesh =
			ConfiguratorVehicle::LoadStagingStaticMesh(RimAssetNames[Index]);
		UStaticMesh* TireMesh =
			ConfiguratorVehicle::LoadStagingStaticMesh(TireAssetNames[Index]);
		UStaticMesh* RotorMesh =
			ConfiguratorVehicle::LoadStagingStaticMesh(RotorAssetNames[Index]);
		UStaticMesh* CaliperMesh =
			ConfiguratorVehicle::LoadStagingStaticMesh(CaliperAssetNames[Index]);
		UStaticMeshComponent* Wheel =
			CreateDefaultSubobject<UStaticMeshComponent>(WheelNames[Index]);
		Wheel->SetupAttachment(SpinPivot);
		const bool bAuthorizedWheel =
			RimMesh != nullptr || AuthorizedWheelMeshes[Index] != nullptr;
		Wheel->SetStaticMesh(
			RimMesh != nullptr
				? RimMesh
				: (bAuthorizedWheel ? AuthorizedWheelMeshes[Index] : CylinderMesh.Object.Get()));
		if (!bAuthorizedWheel)
		{
			Wheel->SetMaterial(0, BasicMaterial.Object);
			Wheel->SetRelativeScale3D(FVector(0.72, 0.72, 0.38));
			// Engine Cylinder 的轴为本地 Z；只旋转视觉网格，使控制轴保持本地 Y。
			Wheel->SetRelativeRotation(FRotator(0.0, 0.0, 90.0));
		}
		else
		{
			Wheel->SetRelativeLocation(-WheelLocations[Index]);
		}
		ConfiguratorVehicle::MarkPartition(
			Wheel, WheelPartTag, WheelSlotTag, bAuthorizedWheel);
		Wheel->ComponentTags.Add(WheelControlTags[Index]);
		Wheels.Add(Wheel);

		UStaticMeshComponent* Tire = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("%sTire"), WheelNames[Index]));
		Tire->SetupAttachment(SpinPivot);
		Tire->SetStaticMesh(TireMesh);
		Tire->SetRelativeLocation(-WheelLocations[Index]);
		Tire->ComponentTags.Add(TEXT("Vehicle.Part.Tire"));
		WheelTires.Add(Tire);

		UStaticMeshComponent* Rotor = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("%sRotor"), WheelNames[Index]));
		Rotor->SetupAttachment(SpinPivot);
		Rotor->SetStaticMesh(RotorMesh);
		Rotor->SetRelativeLocation(-WheelLocations[Index]);
		Rotor->ComponentTags.Add(TEXT("Vehicle.Part.BrakeRotor"));
		WheelRotors.Add(Rotor);

		UStaticMeshComponent* Caliper = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("%sCaliper"), WheelNames[Index]));
		// 卡钳跟随前轮转向，但不能挂在 Spin Pivot 下随轮胎滚动。
		Caliper->SetupAttachment(SteeringPivot);
		Caliper->SetStaticMesh(CaliperMesh);
		Caliper->SetRelativeLocation(-WheelLocations[Index]);
		Caliper->ComponentTags.Add(TEXT("Vehicle.Part.BrakeCaliper"));
		BrakeCalipers.Add(Caliper);
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
		Pivot->SetupAttachment(ContentRoot);
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

	const auto MarkCatalogProxyTarget = [](UMeshComponent* Component, const FName SlotId)
	{
		check(Component != nullptr);
		Component->ComponentTags.AddUnique(
			UAutomotiveMaterialBinder::MakeProxyTargetTag(SlotId));
	};
	MarkCatalogProxyTarget(PaintBody, TEXT("A5Proxy_ExteriorBodyCover"));
	MarkCatalogProxyTarget(Hood, TEXT("A5Proxy_EngineBayCover"));
	MarkCatalogProxyTarget(Trunk, TEXT("A5Proxy_RearWing"));
	MarkCatalogProxyTarget(Wheels[0], TEXT("A5Proxy_WheelMaterial"));
	MarkCatalogProxyTarget(Wheels[1], TEXT("A5Proxy_WheelStyle"));
	MarkCatalogProxyTarget(Wheels[2], TEXT("A5Proxy_WheelColor"));
	MarkCatalogProxyTarget(BrakeCalipers[0], TEXT("A5Proxy_FrontCaliperColor"));
	MarkCatalogProxyTarget(BrakeCalipers[2], TEXT("A5Proxy_RearCaliperColor"));
	MarkCatalogProxyTarget(LeftDoor, TEXT("A5Proxy_DoorUpper"));
	MarkCatalogProxyTarget(InteriorCabin, TEXT("A5Proxy_DoorMiddle"));
	MarkCatalogProxyTarget(Frame, TEXT("A5Proxy_LowerSkirt"));

	struct FCatalogProxyDefinition
	{
		const TCHAR* SurfaceId;
		const TCHAR* SlotId;
		const TCHAR* AssetName;
	};
	// 这些都是 Authorized Audi A5 代理分件。名称只描述当前 UI 语义映射；
	// 对 A5 没有同名语义的项目使用尚未占用的可见分件，绝不表示正式 SC01 几何。
	const FCatalogProxyDefinition CatalogProxyDefinitions[] = {
		{TEXT("steering-wheel-skin"), TEXT("A5Proxy_SteeringWheelSkin"), TEXT("SM_steeringwheel")},
		{TEXT("steering-wheel-addon"), TEXT("A5Proxy_SteeringWheelAddon"), TEXT("SM_swColumn")},
		{TEXT("steering-center-mark"), TEXT("A5Proxy_SteeringCenterMark"), TEXT("SM_gaugeBezel")},
		{TEXT("ip-wings"), TEXT("A5Proxy_IpWings"), TEXT("SM_leftPanelInt")},
		{TEXT("ip-middle"), TEXT("A5Proxy_IpMiddle"), TEXT("SM_dashMain")},
		{TEXT("ip-instrument-cover"), TEXT("A5Proxy_IpInstrumentCover"), TEXT("SM_gaugeGlass")},
		{TEXT("ip-upper-trim"), TEXT("A5Proxy_IpUpperTrim"), TEXT("SM_dashCenter")},
		{TEXT("ip-lower-trim"), TEXT("A5Proxy_IpLowerTrim"), TEXT("SM_centerconsoleControl")},
		{TEXT("ip-center-mark"), TEXT("A5Proxy_IpCenterMark"), TEXT("SM_speedNeedle")},
		{TEXT("a-pillar-surface"), TEXT("A5Proxy_APillarSurface"), TEXT("SM_windshieldTrim")},
		{TEXT("seat-backrest"), TEXT("A5Proxy_SeatBackrest"), TEXT("SM_seatFrontLeftBackA")},
		{TEXT("seat-bolster"), TEXT("A5Proxy_SeatBolster"), TEXT("SM_seatFrontRightBotA")},
		{TEXT("seat-shell-back"), TEXT("A5Proxy_SeatShellBack"), TEXT("SM_seatFrontRightBackA")},
		{TEXT("seat-headrest-mark"), TEXT("A5Proxy_SeatHeadrestMark"), TEXT("SM_seatfrontLeftHeadA")},
		{TEXT("door-armrest"), TEXT("A5Proxy_DoorArmrest"), TEXT("SM_armrestCover")},
		{TEXT("door-armrest-skin"), TEXT("A5Proxy_DoorArmrestSkin"), TEXT("SM_doorPanelIntLeft")},
		{TEXT("storage-soft-bag"), TEXT("A5Proxy_StorageSoftBag"), TEXT("SM_glovebox")},
		{TEXT("console-armrest-cover"), TEXT("A5Proxy_ConsoleArmrestCover"), TEXT("SM_centerconsoleInt")},
		{TEXT("console-armrest-side"), TEXT("A5Proxy_ConsoleArmrestSide"), TEXT("SM_centerconsoleSide")},
		{TEXT("handbrake"), TEXT("A5Proxy_Handbrake"), TEXT("SM_shifter")},
		{TEXT("roof-surface"), TEXT("A5Proxy_RoofSurface"), TEXT("SM_sunvisorLeft")},
		{TEXT("interior-painted-parts"), TEXT("A5Proxy_InteriorPaintedParts"), TEXT("SM_heatingControl")},
		{TEXT("door-sill"), TEXT("A5Proxy_DoorSill"), TEXT("SM_doorTrimLeft")},
		{TEXT("embroidered-logo"), TEXT("A5Proxy_EmbroideredLogo"), TEXT("SM_infoBezel")},
		{TEXT("headrest-embroidery"), TEXT("A5Proxy_HeadrestEmbroidery"), TEXT("SM_seatfrontRightHeadA")},
		{TEXT("door-panel-embroidery"), TEXT("A5Proxy_DoorPanelEmbroidery"), TEXT("SM_doorInteriorAright")},
		{TEXT("center-panel-trim"), TEXT("A5Proxy_CenterPanelTrim"), TEXT("SM_HMI")},
		{TEXT("nameplate"), TEXT("A5Proxy_Nameplate"), TEXT("SM_Screen")},
		{TEXT("pedal"), TEXT("A5Proxy_Pedal"), TEXT("SM_pedals")}
	};
	for (const FCatalogProxyDefinition& Definition : CatalogProxyDefinitions)
	{
		UStaticMeshComponent* Proxy = CreateDefaultSubobject<UStaticMeshComponent>(
			*FString::Printf(TEXT("CatalogProxy_%s"), Definition.SlotId));
		Proxy->SetupAttachment(ContentRoot);
		UStaticMesh* ProxyMesh =
			ConfiguratorVehicle::LoadIndependentStaticMesh(Definition.AssetName);
		Proxy->SetStaticMesh(ProxyMesh != nullptr ? ProxyMesh : CubeMesh.Object.Get());
		if (ProxyMesh == nullptr)
		{
			Proxy->SetMaterial(0, BasicMaterial.Object);
			Proxy->SetRelativeScale3D(FVector(0.08));
		}
		Proxy->ComponentTags.Add(
			ProxyMesh != nullptr ? AuthorizedResourceTag : TemporaryResourceTag);
		Proxy->ComponentTags.Add(TEXT("Configurator.Resource.AuthorizedAudiA5.ProxySurface"));
		Proxy->ComponentTags.Add(FName(*FString::Printf(
			TEXT("Configurator.Surface.%s"),
			Definition.SurfaceId)));
		MarkCatalogProxyTarget(Proxy, Definition.SlotId);
		CatalogSurfaceProxyParts.Add(Proxy);
	}

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
	AnimationPlayer = CreateDefaultSubobject<UVehicleAnimSequencePlayerComponent>(
		TEXT("AnimSequenceFramePlayer"));
	AnimationPlayer->BindMesh(SkeletalVehicle);
}

bool AConfiguratorVehicleActor::BuildAnimationClips(
	const AutomotiveCatalog::FCatalog& Catalog,
	TArray<FVehicleAnimationClip>& OutClips)
{
	OutClips.Reset(Catalog.Animations.Num());
	for (const AutomotiveCatalog::FAnimation& Animation : Catalog.Animations)
	{
		if (!UVehicleAnimSequencePlayerComponent::IsStableAnimationId(
			Animation.AnimationId))
		{
			OutClips.Reset();
			return false;
		}
		FVehicleAnimationClip Clip;
		Clip.AnimationId = FName(*Animation.AnimationId);
		Clip.DisplayName = FText::FromString(Animation.DisplayName);
		Clip.FrameRate = static_cast<float>(Animation.FrameRate);
		Clip.StartFrame = Animation.StartFrame;
		Clip.EndFrame = Animation.EndFrame;
		if (Animation.LoopMode == TEXT("none"))
		{
			Clip.LoopMode = EVehicleAnimationLoopMode::None;
		}
		else if (Animation.LoopMode == TEXT("forward"))
		{
			Clip.LoopMode = EVehicleAnimationLoopMode::Forward;
		}
		else if (Animation.LoopMode == TEXT("ping-pong"))
		{
			Clip.LoopMode = EVehicleAnimationLoopMode::PingPong;
		}
		else
		{
			OutClips.Reset();
			return false;
		}
		if (Animation.CloseMode == TEXT("reverse"))
		{
			Clip.CloseMode = EVehicleAnimationCloseMode::Reverse;
		}
		else if (Animation.CloseMode == TEXT("reset-to-start"))
		{
			Clip.CloseMode = EVehicleAnimationCloseMode::ResetToStart;
		}
		else if (Animation.CloseMode == TEXT("stop"))
		{
			Clip.CloseMode = EVehicleAnimationCloseMode::Stop;
		}
		else
		{
			OutClips.Reset();
			return false;
		}
		OutClips.Add(MoveTemp(Clip));
	}
	return !OutClips.IsEmpty();
}

bool AConfiguratorVehicleActor::ConfigureAnimationFromCatalog(
	const AutomotiveCatalog::FCatalog& Catalog)
{
	if (bAnimationCatalogConfigured)
	{
		return bAnimationSequenceReady;
	}
	bAnimationCatalogConfigured = true;
	StaticAnimationCloseModes.Reset();
	for (const AutomotiveCatalog::FAnimation& Animation : Catalog.Animations)
	{
		StaticAnimationCloseModes.Add(
			FName(*Animation.AnimationId),
			Animation.CloseMode);
	}

	USkeletalMesh* WholeVehicleMesh =
		ConfiguratorVehicle::LoadOptionalAsset<USkeletalMesh>(*Catalog.SkeletalMeshPath);
	UAnimSequence* FullVehicleSequence =
		ConfiguratorVehicle::LoadOptionalAsset<UAnimSequence>(*Catalog.SequencePath);
	bStaticAnimationFallbackEnabled = ShouldUseStaticAnimationFallback(
		IsValid(WholeVehicleMesh),
		IsValid(FullVehicleSequence));
	if (bStaticAnimationFallbackEnabled)
	{
		SkeletalVehicle->SetSkeletalMeshAsset(nullptr);
		SkeletalVehicle->SetVisibility(false);
		SkeletalVehicle->SetHiddenInGame(true);
		SetStaticProxyVisible(true);
		return false;
	}

	SkeletalVehicle->SetSkeletalMeshAsset(WholeVehicleMesh);
	ConfiguratorVehicle::PopulateMissingSkeletalMaterials(
		SkeletalVehicle,
		WholeVehicleMesh);
	TArray<FVehicleAnimationClip> Clips;
	bAnimationSequenceReady =
		BuildAnimationClips(Catalog, Clips)
		&& AnimationPlayer->SetSequenceAndClips(FullVehicleSequence, Clips);
	if (!bAnimationSequenceReady)
	{
		AnimationPlayer->FreezeAnimation();
		SkeletalVehicle->SetSkeletalMeshAsset(nullptr);
		SkeletalVehicle->SetVisibility(false);
		SkeletalVehicle->SetHiddenInGame(true);
		SetStaticProxyVisible(true);
		bStaticAnimationFallbackEnabled = true;
		return false;
	}

	bStaticAnimationFallbackEnabled = false;
	SkeletalVehicle->SetVisibility(true);
	SkeletalVehicle->SetHiddenInGame(false);
	SetStaticProxyVisible(false);
	return true;
}

bool AConfiguratorVehicleActor::ShouldUseStaticAnimationFallback(
	const bool bHasSkeletalMesh,
	const bool bHasSequence)
{
	return !bHasSkeletalMesh || !bHasSequence;
}

void AConfiguratorVehicleActor::SetStaticProxyVisible(const bool bVisible)
{
	TInlineComponentArray<UStaticMeshComponent*> StaticParts(this);
	for (UStaticMeshComponent* StaticPart : StaticParts)
	{
		StaticPart->SetVisibility(bVisible);
		StaticPart->SetHiddenInGame(!bVisible);
	}
}

int32 AConfiguratorVehicleActor::GetCatalogSurfaceTargetCount() const
{
	int32 Count = 0;
	TInlineComponentArray<UMeshComponent*> Meshes(this);
	for (const UMeshComponent* Mesh : Meshes)
	{
		Count += IsValid(Mesh) && Mesh->ComponentTags.ContainsByPredicate(
			[](const FName Tag)
			{
				return Tag.ToString().StartsWith(
					TEXT("Configurator.ProxySurfaceTarget."));
			}) ? 1 : 0;
	}
	return Count;
}

UMeshComponent* AConfiguratorVehicleActor::FindCatalogSurfaceTarget(
	const FName SlotId) const
{
	const FName TargetTag = UAutomotiveMaterialBinder::MakeProxyTargetTag(SlotId);
	TInlineComponentArray<UMeshComponent*> Meshes(this);
	UMeshComponent* Match = nullptr;
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->ComponentHasTag(TargetTag))
		{
			continue;
		}
		if (Match != nullptr)
		{
			return nullptr;
		}
		Match = Mesh;
	}
	return Match;
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
		FTransform(GetHoodOpenRotation(), HoodPivot->GetRelativeLocation()));
	TrunkActuator->BindPart(
		TrunkPivot,
		TrunkPivot->GetRelativeTransform(),
		FTransform(GetTrunkOpenRotation(), TrunkPivot->GetRelativeLocation()));
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

void AConfiguratorVehicleActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bHasPendingFallbackFocus
		&& !IsStaticAnimationMoving(ActiveFallbackAnimationId))
	{
		StartPendingStaticAnimation();
	}
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

void AConfiguratorVehicleActor::SetWheelAnimationEnabled(const bool bEnabled)
{
	SetWheelMotion(0.0f, bEnabled ? 180.0f : 0.0f);
}

bool AConfiguratorVehicleActor::ToggleWheelSpin()
{
	SetWheelAnimationEnabled(!bWheelsSpinning);
	return bWheelsSpinning;
}

bool AConfiguratorVehicleActor::PlayVehicleAnimation(const FName AnimationId)
{
	if (IsValid(AnimationPlayer)
		&& AnimationPlayer->PlayAnimationById(AnimationId))
	{
		ActiveFallbackAnimationId = NAME_None;
		return true;
	}
	if (!bStaticAnimationFallbackEnabled)
	{
		return false;
	}
	const bool bApplied = ApplyStaticAnimationFallback(AnimationId, true);
	if (bApplied)
	{
		ActiveFallbackAnimationId = AnimationId;
	}
	return bApplied;
}

bool AConfiguratorVehicleActor::CloseVehicleAnimation(const FName AnimationId)
{
	if (IsValid(AnimationPlayer)
		&& AnimationPlayer->CloseAnimationById(AnimationId))
	{
		return true;
	}
	if (!bStaticAnimationFallbackEnabled)
	{
		return false;
	}
	const bool bApplied = ApplyStaticAnimationFallback(AnimationId, false);
	if (bApplied && ActiveFallbackAnimationId == AnimationId)
	{
		ActiveFallbackAnimationId = NAME_None;
	}
	return bApplied;
}

bool AConfiguratorVehicleActor::FocusVehicleAnimation(const FName NextAnimationId)
{
	if (!NextAnimationId.IsNone()
		&& !UVehicleAnimSequencePlayerComponent::IsStableAnimationId(
			NextAnimationId.ToString().ToLower()))
	{
		return false;
	}
	if (!bStaticAnimationFallbackEnabled)
	{
		if (IsValid(AnimationPlayer)
			&& AnimationPlayer->FocusAnimationById(NextAnimationId))
		{
			return true;
		}
		// 运行时序列/网格异常时，不能留下不可控的骨骼车；恢复可交互静态代理。
		if (IsValid(AnimationPlayer))
		{
			AnimationPlayer->FreezeAnimation();
		}
		SkeletalVehicle->SetVisibility(false);
		SkeletalVehicle->SetHiddenInGame(true);
		SetStaticProxyVisible(true);
		bStaticAnimationFallbackEnabled = true;
		ActiveFallbackAnimationId = NAME_None;
		return NextAnimationId.IsNone()
			|| (IsStaticAnimationSupported(NextAnimationId)
				&& PlayVehicleAnimation(NextAnimationId));
	}
	if (!NextAnimationId.IsNone() && !IsStaticAnimationSupported(NextAnimationId))
	{
		return false;
	}
	if (NextAnimationId == ActiveFallbackAnimationId)
	{
		bHasPendingFallbackFocus = false;
		PendingFallbackAnimationId = NAME_None;
		if (!NextAnimationId.IsNone())
		{
			return ApplyStaticAnimationFallback(NextAnimationId, true);
		}
		return true;
	}
	if (ActiveFallbackAnimationId.IsNone())
	{
		return NextAnimationId.IsNone()
			|| PlayVehicleAnimation(NextAnimationId);
	}

	PendingFallbackAnimationId = NextAnimationId;
	bHasPendingFallbackFocus = true;
	if (!ApplyStaticAnimationFallback(ActiveFallbackAnimationId, false))
	{
		bHasPendingFallbackFocus = false;
		PendingFallbackAnimationId = NAME_None;
		return false;
	}
	if (!IsStaticAnimationMoving(ActiveFallbackAnimationId))
	{
		StartPendingStaticAnimation();
	}
	return true;
}

void AConfiguratorVehicleActor::FreezeAllVehicleMotion()
{
	bHasPendingFallbackFocus = false;
	PendingFallbackAnimationId = NAME_None;
	ActiveFallbackAnimationId = NAME_None;
	if (IsValid(AnimationPlayer))
	{
		AnimationPlayer->FreezeAnimation();
	}
	for (UReversiblePartActuatorComponent* Actuator :
		{LeftDoorActuator, RightDoorActuator, HoodActuator, TrunkActuator})
	{
		if (IsValid(Actuator))
		{
			Actuator->FreezeAtCurrentPose();
		}
	}
	if (IsValid(WheelController))
	{
		WheelController->StopImmediately();
	}
	bWheelsSpinning = false;
}

FName AConfiguratorVehicleActor::GetActiveVehicleAnimationId() const
{
	const FName SequenceAnimationId = IsValid(AnimationPlayer)
		? AnimationPlayer->GetActiveAnimationId()
		: NAME_None;
	return SequenceAnimationId.IsNone()
		? ActiveFallbackAnimationId
		: SequenceAnimationId;
}

FName AConfiguratorVehicleActor::GetFocusedVehicleAnimationId() const
{
	if (!bStaticAnimationFallbackEnabled && IsValid(AnimationPlayer))
	{
		return AnimationPlayer->GetFocusedAnimationId();
	}
	return bHasPendingFallbackFocus
		? PendingFallbackAnimationId
		: ActiveFallbackAnimationId;
}

bool AConfiguratorVehicleActor::IsVehicleAnimationPlaying() const
{
	return !GetActiveVehicleAnimationId().IsNone();
}

bool AConfiguratorVehicleActor::CanPlayVehicleAnimation(
	const FName AnimationId) const
{
	if (!UVehicleAnimSequencePlayerComponent::IsStableAnimationId(
		AnimationId.ToString().ToLower()))
	{
		return false;
	}
	if (IsValid(AnimationPlayer)
		&& AnimationPlayer->HasAnimationById(AnimationId))
	{
		return true;
	}
	// 五个稳定动画 ID 均具备静态代理回退路径。能力查询必须与
	// FocusVehicleAnimation 的故障回退保持一致，不能因启动瞬间组件
	// 注册状态不同而把可执行项永久过滤出 Web 菜单。
	return IsStaticAnimationSupported(AnimationId);
}

FString AConfiguratorVehicleActor::GetAnimationExecutorStateJson(
	const FName AnimationId) const
{
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetStringField(TEXT("animationId"), AnimationId.ToString().ToLower());
	State->SetBoolField(TEXT("canPlay"), CanPlayVehicleAnimation(AnimationId));
	State->SetBoolField(
		TEXT("active"),
		GetActiveVehicleAnimationId() == AnimationId);
	State->SetBoolField(
		TEXT("focused"),
		GetFocusedVehicleAnimationId() == AnimationId);

	const UReversiblePartActuatorComponent* Actuator =
		AnimationId == TEXT("hood") ? HoodActuator
		: AnimationId == TEXT("trunk") ? TrunkActuator
		: AnimationId == TEXT("door-left") ? LeftDoorActuator
		: AnimationId == TEXT("door-right") ? RightDoorActuator
		: nullptr;
	if (!bStaticAnimationFallbackEnabled
		&& IsValid(AnimationPlayer)
		&& AnimationPlayer->HasAnimationById(AnimationId))
	{
		State->SetStringField(TEXT("executor"), TEXT("sequence"));
		State->SetNumberField(
			TEXT("currentFrame"),
			AnimationPlayer->GetCurrentFrame());
		State->SetNumberField(
			TEXT("direction"),
			AnimationPlayer->GetDirection());
		State->SetBoolField(
			TEXT("moving"),
			AnimationPlayer->IsPlaying());
	}
	else if (AnimationId == TEXT("wheel-spin"))
	{
		State->SetStringField(TEXT("executor"), TEXT("wheel"));
		State->SetBoolField(TEXT("enabled"), bWheelsSpinning);
		State->SetBoolField(TEXT("moving"), bWheelsSpinning);
	}
	else if (IsValid(Actuator))
	{
		State->SetStringField(TEXT("executor"), TEXT("part-actuator"));
		State->SetNumberField(TEXT("progress"), Actuator->GetProgress());
		State->SetBoolField(TEXT("openRequested"), Actuator->IsOpenRequested());
		State->SetBoolField(TEXT("moving"), Actuator->IsMoving());
	}
	else
	{
		State->SetStringField(TEXT("executor"), TEXT("none"));
		State->SetBoolField(TEXT("moving"), false);
	}

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(State, Writer) ? Json : FString();
}

bool AConfiguratorVehicleActor::ApplyStaticAnimationFallback(
	const FName AnimationId,
	const bool bOpen)
{
	if (AnimationId == TEXT("wheel-spin"))
	{
		SetWheelAnimationEnabled(bOpen);
		return true;
	}
	const FString* CloseMode = StaticAnimationCloseModes.Find(AnimationId);
	if (!bOpen && CloseMode != nullptr && *CloseMode == TEXT("stop"))
	{
		// 静态代理遵循 Catalog closeMode；stop 保持当前姿态。
		return true;
	}
	if (AnimationId == TEXT("hood")
		|| AnimationId == TEXT("door-left")
		|| AnimationId == TEXT("door-right")
		|| AnimationId == TEXT("trunk"))
	{
		return SetPartOpen(AnimationId, bOpen);
	}
	return false;
}

bool AConfiguratorVehicleActor::IsStaticAnimationSupported(
	const FName AnimationId) const
{
	return AnimationId == TEXT("wheel-spin")
		|| AnimationId == TEXT("trunk")
		|| AnimationId == TEXT("hood")
		|| AnimationId == TEXT("door-left")
		|| AnimationId == TEXT("door-right");
}

bool AConfiguratorVehicleActor::IsStaticAnimationMoving(
	const FName AnimationId) const
{
	if (AnimationId == TEXT("wheel-spin"))
	{
		return bWheelsSpinning;
	}
	const UReversiblePartActuatorComponent* Actuator =
		AnimationId == TEXT("hood") ? HoodActuator
		: AnimationId == TEXT("trunk") ? TrunkActuator
		: AnimationId == TEXT("door-left") ? LeftDoorActuator
		: AnimationId == TEXT("door-right") ? RightDoorActuator
		: nullptr;
	return IsValid(Actuator) && Actuator->IsMoving();
}

void AConfiguratorVehicleActor::StartPendingStaticAnimation()
{
	if (!bHasPendingFallbackFocus)
	{
		return;
	}
	const FName NextAnimationId = PendingFallbackAnimationId;
	const FName PreviousAnimationId = ActiveFallbackAnimationId;
	PendingFallbackAnimationId = NAME_None;
	bHasPendingFallbackFocus = false;
	ActiveFallbackAnimationId = NAME_None;
	if (!NextAnimationId.IsNone()
		&& !PlayVehicleAnimation(NextAnimationId)
		&& !PreviousAnimationId.IsNone())
	{
		// 新动画失败时恢复旧静态代理，避免 UI 成功切焦后车辆留在闭合空态。
		if (ApplyStaticAnimationFallback(PreviousAnimationId, true))
		{
			ActiveFallbackAnimationId = PreviousAnimationId;
		}
	}
}

FRotator AConfiguratorVehicleActor::GetHoodOpenRotation()
{
	return FRotator(32.0, 0.0, 0.0);
}

FRotator AConfiguratorVehicleActor::GetTrunkOpenRotation()
{
	return FRotator(-35.0, 0.0, 0.0);
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
		|| !InteriorCabin->ComponentHasTag(UAutomotiveMaterialBinder::InteriorProxySlotTag))
	{
		OutErrors.Add(TEXT("内饰代理必须包含唯一车型目录材质槽标签。"));
	}
	Validate(Frame, FramePartTag, FrameSlotTag, TEXT("frame"));
	Validate(WheelGroup, WheelPartTag, WheelSlotTag, TEXT("wheel"));
	if (ContentRoot == nullptr
		|| ContentRoot->GetAttachParent() != VehicleRoot
		|| !ContentRoot->GetRelativeLocation().Equals(
			ConfiguratorVehicle::ContentRootOffset, 0.01))
	{
		OutErrors.Add(TEXT("车辆内容根未按审计边界归中并落地。"));
	}
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
	if (WheelTires.Num() != 4 || WheelRotors.Num() != 4 || BrakeCalipers.Num() != 4)
	{
		OutErrors.Add(TEXT("四轮必须分别包含独立轮胎、制动盘与不滚动卡钳。"));
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
			|| !WheelTires.IsValidIndex(Index)
			|| !WheelRotors.IsValidIndex(Index)
			|| !BrakeCalipers.IsValidIndex(Index)
			|| !IsValid(WheelTires[Index])
			|| !IsValid(WheelRotors[Index])
			|| !IsValid(BrakeCalipers[Index])
			|| WheelTires[Index]->GetAttachParent() != WheelSpinPivots[Index]
			|| WheelRotors[Index]->GetAttachParent() != WheelSpinPivots[Index]
			|| BrakeCalipers[Index]->GetAttachParent() != WheelSteeringPivots[Index])
		{
			OutErrors.Add(TEXT("车轮轴向或 Steering/Spin/Mesh 层级不正确。"));
		}
	}
	if (Trunk == nullptr
		|| Trunk->GetStaticMesh() == nullptr
		|| Trunk->GetStaticMesh()->GetFName() != TEXT("TrunkMesh"))
	{
		OutErrors.Add(TEXT("后盖必须仅使用语义 TrunkMesh，不得携带敞篷收纳机构。"));
	}
	return OutErrors.IsEmpty()
		&& (Tags.Contains(TemporaryResourceTag) || Tags.Contains(AuthorizedResourceTag));
}
