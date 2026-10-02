#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "SmoothWheelControllerComponent.generated.h"

class USceneComponent;

/** 平滑插值车轮转向与滚动；仅驱动可替换的视觉组件，不包含车辆物理。 */
UCLASS(ClassGroup=(Vehicle), BlueprintType, meta=(BlueprintSpawnableComponent))
class CONFIGURATIONSYSTEM_API USmoothWheelControllerComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	USmoothWheelControllerComponent();

	UFUNCTION(BlueprintCallable, Category="Vehicle|Wheels")
	void BindWheels(
		const TArray<USceneComponent*>& InSteeringPivots,
		const TArray<USceneComponent*>& InSpinPivots);

	UFUNCTION(BlueprintCallable, Category="Vehicle|Wheels")
	void SetWheelTargets(float InSteeringDegrees, float InSpinDegreesPerSecond);

	UFUNCTION(BlueprintCallable, Category="Vehicle|Wheels")
	void AdvanceWheels(float DeltaTime);

	UFUNCTION(BlueprintPure, Category="Vehicle|Wheels")
	float GetSmoothedSteeringDegrees() const { return CurrentSteeringDegrees; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Wheels")
	float SteeringResponse = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Wheels")
	float SpinResponse = 6.0f;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> SteeringPivots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> SpinPivots;

	TArray<FQuat> BaseSteeringRotations;
	TArray<FQuat> BaseSpinRotations;
	float TargetSteeringDegrees = 0.0f;
	float CurrentSteeringDegrees = 0.0f;
	float TargetSpinDegreesPerSecond = 0.0f;
	float CurrentSpinDegreesPerSecond = 0.0f;
	float AccumulatedSpinDegrees = 0.0f;
};
