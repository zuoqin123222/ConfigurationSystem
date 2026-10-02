#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CarConfigurationState.h"
#include "ConfiguratorVehicleActor.generated.h"

class UMaterialInstanceDynamic;
class UReversiblePartActuatorComponent;
class USceneComponent;
class USmoothWheelControllerComponent;
class UStaticMeshComponent;

/**
 * 首个原子阶段使用的可运行车辆占位 Actor。
 *
 * 几何和基础材质全部来自 /Engine/BasicShapes，明确属于临时资源；正式车辆接入时
 * 保留四个 Configurator.Part.* 标签与 Configurator.Slot.* 逻辑槽即可替换本类。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API AConfiguratorVehicleActor final : public AActor
{
	GENERATED_BODY()

public:
	AConfiguratorVehicleActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 把领域状态映射到占位几何颜色，不参与 canonical key 或价格计算。 */
	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	void ApplyConfiguration(const FCarConfigurationSelection& Selection);

	/** PartId: door-left、door-right、hood、trunk。运动中再次调用可连续反向。 */
	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	bool TogglePart(FName PartId);

	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	bool SetPartOpen(FName PartId, bool bOpen);

	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	void SetWheelMotion(float SteeringDegrees, float SpinDegreesPerSecond);

	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	bool ToggleWheelSpin();

	/** 自动化探针使用：验证四分区标签、逻辑槽和临时资源声明未漂移。 */
	bool HasStablePlaceholderBindings(TArray<FString>& OutErrors) const;

	static const FName PaintPartTag;
	static const FName WheelPartTag;
	static const FName InteriorPartTag;
	static const FName FramePartTag;
	static const FName PaintSlotTag;
	static const FName WheelSlotTag;
	static const FName InteriorSlotTag;
	static const FName FrameSlotTag;
	static const FName TemporaryResourceTag;

private:
	UMaterialInstanceDynamic* GetOrCreateMaterial(
		UStaticMeshComponent* Component,
		TObjectPtr<UMaterialInstanceDynamic>& Storage);
	void SetComponentColor(
		UStaticMeshComponent* Component,
		TObjectPtr<UMaterialInstanceDynamic>& Storage,
		const FLinearColor& Color);

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TObjectPtr<USceneComponent> VehicleRoot;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TObjectPtr<UStaticMeshComponent> PaintBody;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TObjectPtr<UStaticMeshComponent> InteriorCabin;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TObjectPtr<UStaticMeshComponent> Frame;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TObjectPtr<USceneComponent> WheelGroup;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TArray<TObjectPtr<UStaticMeshComponent>> Wheels;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TArray<TObjectPtr<USceneComponent>> WheelSteeringPivots;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TArray<TObjectPtr<USceneComponent>> WheelSpinPivots;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<USceneComponent> LeftDoorPivot;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<USceneComponent> RightDoorPivot;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<USceneComponent> HoodPivot;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<USceneComponent> TrunkPivot;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<UStaticMeshComponent> LeftDoor;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<UStaticMeshComponent> RightDoor;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<UStaticMeshComponent> Hood;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆|临时")
	TObjectPtr<UStaticMeshComponent> Trunk;

	UPROPERTY(VisibleAnywhere, Category = "车辆体验")
	TObjectPtr<UReversiblePartActuatorComponent> LeftDoorActuator;

	UPROPERTY(VisibleAnywhere, Category = "车辆体验")
	TObjectPtr<UReversiblePartActuatorComponent> RightDoorActuator;

	UPROPERTY(VisibleAnywhere, Category = "车辆体验")
	TObjectPtr<UReversiblePartActuatorComponent> HoodActuator;

	UPROPERTY(VisibleAnywhere, Category = "车辆体验")
	TObjectPtr<UReversiblePartActuatorComponent> TrunkActuator;

	UPROPERTY(VisibleAnywhere, Category = "车辆体验")
	TObjectPtr<USmoothWheelControllerComponent> WheelController;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WheelMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> InteriorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrameMaterial;

	bool bWheelsSpinning = false;
};
