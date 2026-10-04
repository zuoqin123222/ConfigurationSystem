#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "ReversiblePartActuatorComponent.generated.h"

class USceneComponent;

/**
 * 可复用的车辆部件执行器。
 *
 * 执行器只修改绑定组件的相对 Transform；线性进度经过 SmoothStep 后再插值。
 * SetOpen/Toggle 只改变目标，因此运动中反向不会重置起点，也不会产生位置跳变。
 */
UCLASS(ClassGroup=(Vehicle), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class CONFIGURATIONSYSTEM_API UReversiblePartActuatorComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UReversiblePartActuatorComponent();

	/** 绑定被驱动的场景组件，并设置闭合/打开端点。 */
	UFUNCTION(BlueprintCallable, Category="Vehicle|Part Actuator")
	void BindPart(
		USceneComponent* InTargetComponent,
		const FTransform& InClosedRelativeTransform,
		const FTransform& InOpenRelativeTransform);

	/** 设置期望状态；运动途中调用会从当前进度连续反向。 */
	UFUNCTION(BlueprintCallable, Category="Vehicle|Part Actuator")
	void SetOpen(bool bInOpen);

	/** 在当前期望状态的反方向运动。 */
	UFUNCTION(BlueprintCallable, Category="Vehicle|Part Actuator")
	void Toggle();

	/** 立即冻结在当前插值姿态；下一次 SetOpen/Toggle 会恢复驱动。 */
	UFUNCTION(BlueprintCallable, Category="Vehicle|Part Actuator")
	void FreezeAtCurrentPose();

	/**
	 * 推进一步，供固定步长模拟或自动化探针复用。
	 * 正常游戏无需调用，组件 Tick 会自动推进。
	 */
	UFUNCTION(BlueprintCallable, Category="Vehicle|Part Actuator")
	void AdvanceActuation(float DeltaTime);

	UFUNCTION(BlueprintPure, Category="Vehicle|Part Actuator")
	float GetProgress() const { return Progress; }

	UFUNCTION(BlueprintPure, Category="Vehicle|Part Actuator")
	bool IsOpenRequested() const { return bOpenRequested; }

	UFUNCTION(BlueprintPure, Category="Vehicle|Part Actuator")
	bool IsMoving() const;

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	/** 从闭合端点运动到打开端点所需的秒数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Part Actuator", meta=(ClampMin="0.0"))
	float Duration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Part Actuator")
	FTransform ClosedRelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Part Actuator")
	FTransform OpenRelativeTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vehicle|Part Actuator")
	bool bStartOpen = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vehicle|Part Actuator")
	TObjectPtr<USceneComponent> TargetComponent;

private:
	void ApplyProgress();

	UPROPERTY(VisibleInstanceOnly, Category="Vehicle|Part Actuator")
	float Progress = 0.0f;

	bool bOpenRequested = false;
	bool bFrozen = false;
};
