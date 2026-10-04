#include "PathTracingLightingRig.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

const FName APathTracingLightingRig::RigActorTag(
	TEXT("Configurator.PathTracing.LightingRig"));

FPathTracingLightingPreset FPathTracingLightingPreset::ForEnvironment(
	const int32 EnvironmentIndex)
{
	FPathTracingLightingPreset Preset;
	if (FMath::Clamp(EnvironmentIndex, 0, 1) == 1)
	{
		Preset.CubemapPath = TEXT("/Game/Library/HDRIs/008.008");
		Preset.SkyIntensity = 1.2f;
		Preset.DirectionalIntensity = 90000.0f;
		Preset.PointIntensity = 1800.0f;
		Preset.RectIntensity = 5000.0f;
		Preset.DirectionalRotation = FRotator(-45.0, -35.0, 0.0);
	}
	else
	{
		Preset.CubemapPath = TEXT("/Game/Library/HDRIs/Studio_02.Studio_02");
		Preset.SkyIntensity = 1.0f;
		Preset.DirectionalIntensity = 50000.0f;
		Preset.PointIntensity = 2500.0f;
		Preset.RectIntensity = 8000.0f;
		Preset.DirectionalRotation = FRotator(-35.0, -135.0, 0.0);
	}
	return Preset;
}

APathTracingLightingRig::APathTracingLightingRig()
{
	PrimaryActorTick.bCanEverTick = false;
	SetFlags(RF_Transient);
	Tags.Add(RigActorTag);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("PathTracingSkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->Mobility = EComponentMobility::Movable;
	SkyLight->SourceType = SLS_SpecifiedCubemap;
	SkyLight->bRealTimeCapture = false;

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(
		TEXT("PathTracingDirectionalLight"));
	DirectionalLight->SetupAttachment(SceneRoot);
	DirectionalLight->Mobility = EComponentMobility::Movable;
	DirectionalLight->SetCastShadows(true);
	DirectionalLight->SetUseTemperature(true);
	DirectionalLight->SetTemperature(5500.0f);

	PointLight = CreateDefaultSubobject<UPointLightComponent>(
		TEXT("PathTracingPointLight"));
	PointLight->SetupAttachment(SceneRoot);
	PointLight->Mobility = EComponentMobility::Movable;
	PointLight->SetRelativeLocation(FVector(260.0, -280.0, 190.0));
	PointLight->SetAttenuationRadius(900.0f);
	PointLight->SetCastShadows(true);

	RectLight = CreateDefaultSubobject<URectLightComponent>(
		TEXT("PathTracingRectLight"));
	RectLight->SetupAttachment(SceneRoot);
	RectLight->Mobility = EComponentMobility::Movable;
	RectLight->SetRelativeLocation(FVector(-260.0, 300.0, 280.0));
	RectLight->SetRelativeRotation(FRotator(-28.0, -40.0, 0.0));
	RectLight->SetSourceWidth(240.0f);
	RectLight->SetSourceHeight(140.0f);
	RectLight->SetAttenuationRadius(1200.0f);
	RectLight->SetCastShadows(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	Floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PathTracingFloor"));
	Floor->SetupAttachment(SceneRoot);
	Floor->SetMobility(EComponentMobility::Movable);
	Floor->SetStaticMesh(CubeMesh.Object);
	Floor->SetMaterial(0, BasicMaterial.Object);
	Floor->SetRelativeLocation(FVector(0.0, 0.0, -5.0));
	Floor->SetRelativeScale3D(FVector(18.0, 18.0, 0.1));
	Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

bool APathTracingLightingRig::ApplyEnvironment(const int32 InEnvironmentIndex)
{
	EnvironmentIndex = FMath::Clamp(InEnvironmentIndex, 0, 1);
	const FPathTracingLightingPreset Preset =
		FPathTracingLightingPreset::ForEnvironment(EnvironmentIndex);
	UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, *Preset.CubemapPath);
	if (!IsValid(Cubemap))
	{
		return false;
	}

	SkyLight->SourceType = SLS_SpecifiedCubemap;
	SkyLight->SetCubemap(Cubemap);
	SkyLight->SetIntensity(Preset.SkyIntensity);
	DirectionalLight->SetRelativeRotation(Preset.DirectionalRotation);
	DirectionalLight->SetIntensity(Preset.DirectionalIntensity);
	PointLight->SetIntensity(Preset.PointIntensity);
	RectLight->SetIntensity(Preset.RectIntensity);
	return true;
}
