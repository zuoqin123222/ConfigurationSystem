#include "ReversiblePartActuatorComponent.h"

#include "Components/SceneComponent.h"

UReversiblePartActuatorComponent::UReversiblePartActuatorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UReversiblePartActuatorComponent::BindPart(
	USceneComponent* InTargetComponent,
	const FTransform& InClosedRelativeTransform,
	const FTransform& InOpenRelativeTransform)
{
	TargetComponent = InTargetComponent;
	ClosedRelativeTransform = InClosedRelativeTransform;
	OpenRelativeTransform = InOpenRelativeTransform;
	Progress = bStartOpen ? 1.0f : 0.0f;
	bOpenRequested = bStartOpen;
	ApplyProgress();
}

void UReversiblePartActuatorComponent::SetOpen(const bool bInOpen)
{
	bOpenRequested = bInOpen;
}

void UReversiblePartActuatorComponent::Toggle()
{
	bOpenRequested = !bOpenRequested;
}

bool UReversiblePartActuatorComponent::IsMoving() const
{
	const float TargetProgress = bOpenRequested ? 1.0f : 0.0f;
	return !FMath::IsNearlyEqual(Progress, TargetProgress, UE_KINDA_SMALL_NUMBER);
}

void UReversiblePartActuatorComponent::AdvanceActuation(const float DeltaTime)
{
	if (TargetComponent == nullptr || DeltaTime <= 0.0f || !IsMoving())
	{
		return;
	}

	const float TargetProgress = bOpenRequested ? 1.0f : 0.0f;
	if (Duration <= UE_SMALL_NUMBER)
	{
		Progress = TargetProgress;
	}
	else
	{
		// 进度始终沿同一条 [0,1] 轨道移动；反向时不会重建插值起点。
		Progress = FMath::FInterpConstantTo(Progress, TargetProgress, DeltaTime, 1.0f / Duration);
	}
	ApplyProgress();
}

void UReversiblePartActuatorComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceActuation(DeltaTime);
}

void UReversiblePartActuatorComponent::ApplyProgress()
{
	if (TargetComponent == nullptr)
	{
		return;
	}

	const float EasedProgress = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Progress, 0.0f, 1.0f));
	FTransform BlendedTransform;
	BlendedTransform.Blend(ClosedRelativeTransform, OpenRelativeTransform, EasedProgress);
	TargetComponent->SetRelativeTransform(BlendedTransform);
}
