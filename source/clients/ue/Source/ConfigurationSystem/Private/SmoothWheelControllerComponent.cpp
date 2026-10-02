#include "SmoothWheelControllerComponent.h"

#include "Components/SceneComponent.h"

USmoothWheelControllerComponent::USmoothWheelControllerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USmoothWheelControllerComponent::BindWheels(
	const TArray<USceneComponent*>& InSteeringPivots,
	const TArray<USceneComponent*>& InSpinPivots)
{
	SteeringPivots.Reset();
	SpinPivots.Reset();
	BaseSteeringRotations.Reset();
	BaseSpinRotations.Reset();
	const int32 WheelCount = FMath::Min(InSteeringPivots.Num(), InSpinPivots.Num());
	for (int32 Index = 0; Index < WheelCount; ++Index)
	{
		if (IsValid(InSteeringPivots[Index]) && IsValid(InSpinPivots[Index]))
		{
			SteeringPivots.Add(InSteeringPivots[Index]);
			SpinPivots.Add(InSpinPivots[Index]);
			BaseSteeringRotations.Add(InSteeringPivots[Index]->GetRelativeRotation().Quaternion());
			BaseSpinRotations.Add(InSpinPivots[Index]->GetRelativeRotation().Quaternion());
		}
	}
	CurrentSteeringDegrees = 0.0f;
	AccumulatedSpinDegrees = 0.0f;
}

void USmoothWheelControllerComponent::SetWheelTargets(
	const float InSteeringDegrees,
	const float InSpinDegreesPerSecond)
{
	TargetSteeringDegrees = FMath::Clamp(InSteeringDegrees, -45.0f, 45.0f);
	TargetSpinDegreesPerSecond = InSpinDegreesPerSecond;
}

void USmoothWheelControllerComponent::AdvanceWheels(const float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	CurrentSteeringDegrees = FMath::FInterpTo(
		CurrentSteeringDegrees, TargetSteeringDegrees, DeltaTime, SteeringResponse);
	CurrentSpinDegreesPerSecond = FMath::FInterpTo(
		CurrentSpinDegreesPerSecond, TargetSpinDegreesPerSecond, DeltaTime, SpinResponse);
	AccumulatedSpinDegrees = FMath::Fmod(
		AccumulatedSpinDegrees + CurrentSpinDegreesPerSecond * DeltaTime, 360.0f);

	for (int32 Index = 0; Index < SteeringPivots.Num(); ++Index)
	{
		if (!IsValid(SteeringPivots[Index]) || !IsValid(SpinPivots[Index])
			|| !BaseSteeringRotations.IsValidIndex(Index)
			|| !BaseSpinRotations.IsValidIndex(Index))
		{
			continue;
		}
		FQuat SteeringRotation = BaseSteeringRotations[Index];
		if (Index < 2)
		{
			SteeringRotation =
				FQuat(FVector::UpVector, FMath::DegreesToRadians(CurrentSteeringDegrees))
				* SteeringRotation;
		}
		SteeringPivots[Index]->SetRelativeRotation(SteeringRotation);
		const FQuat SpinRotation =
			BaseSpinRotations[Index]
			* FQuat(FVector::UpVector, FMath::DegreesToRadians(AccumulatedSpinDegrees));
		SpinPivots[Index]->SetRelativeRotation(SpinRotation);
	}
}

void USmoothWheelControllerComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceWheels(DeltaTime);
}
