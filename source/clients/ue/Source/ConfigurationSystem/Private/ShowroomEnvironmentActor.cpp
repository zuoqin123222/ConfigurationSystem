#include "ShowroomEnvironmentActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"

const FName AShowroomEnvironmentActor::EnvironmentActorTag(
	TEXT("Configurator.EnvironmentController"));
const FName AShowroomEnvironmentActor::TemporaryEnvironmentTag(
	TEXT("Configurator.Environment.Temporary"));

AShowroomEnvironmentActor::AShowroomEnvironmentActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Tags.Add(EnvironmentActorTag);
	Tags.Add(TemporaryEnvironmentTag);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("EnvironmentRoot"));
	SetRootComponent(Root);

	StudioKey = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("StudioKey_TEMP"));
	StudioKey->SetupAttachment(Root);
	StudioKey->SetRelativeRotation(FRotator(-38.0, -32.0, 0.0));
	StudioKey->SetIntensity(7.0f);
	StudioKey->SetLightColor(FLinearColor(1.0f, 0.92f, 0.8f));

	StudioSky = CreateDefaultSubobject<USkyLightComponent>(TEXT("StudioSky_TEMP"));
	StudioSky->SetupAttachment(Root);
	StudioSky->SetMobility(EComponentMobility::Movable);
	StudioSky->SetIntensity(0.8f);

	OutdoorSun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("OutdoorSun_TEMP"));
	OutdoorSun->SetupAttachment(Root);
	OutdoorSun->SetRelativeRotation(FRotator(-24.0, 145.0, 0.0));
	OutdoorSun->SetIntensity(9.0f);
	OutdoorSun->SetLightColor(FLinearColor(0.82f, 0.9f, 1.0f));

	OutdoorSky = CreateDefaultSubobject<USkyLightComponent>(TEXT("OutdoorSky_TEMP"));
	OutdoorSky->SetupAttachment(Root);
	OutdoorSky->SetMobility(EComponentMobility::Movable);
	OutdoorSky->SetIntensity(1.35f);

	SetEnvironmentIndex(0);
}

void AShowroomEnvironmentActor::SetEnvironmentIndex(const int32 InIndex)
{
	EnvironmentIndex = FMath::Clamp(InIndex, 0, 1);
	const bool bStudio = EnvironmentIndex == 0;
	StudioKey->SetVisibility(bStudio, true);
	StudioSky->SetVisibility(bStudio, true);
	OutdoorSun->SetVisibility(!bStudio, true);
	OutdoorSky->SetVisibility(!bStudio, true);
}

void AShowroomEnvironmentActor::ToggleEnvironment()
{
	SetEnvironmentIndex(EnvironmentIndex == 0 ? 1 : 0);
}
