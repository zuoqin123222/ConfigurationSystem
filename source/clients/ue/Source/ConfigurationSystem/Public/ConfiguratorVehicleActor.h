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
 * 运行时优先加载 AuthorizedAudiA5 真实几何；单个资产缺失时保留 Engine 基础形状回退。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API AConfiguratorVehicleActor final : public AActor
{
	GENERATED_BODY()

public:
	AConfiguratorVehicleActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 把领域状态映射到车辆逻辑分区颜色，不参与 canonical key 或价格计算。 */
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
	void SetWheelAnimationEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "车辆体验")
	bool IsWheelAnimationEnabled() const { return bWheelsSpinning; }

	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	bool ToggleWheelSpin();

	/** 自动化探针使用：验证真实几何或代理回退、分区标签及可逆执行器绑定。 */
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
	static const FName AuthorizedResourceTag;

private:
	UMaterialInstanceDynamic* GetOrCreateMaterial(
		UStaticMeshComponent* Component,
		TObjectPtr<UMaterialInstanceDynamic>& Storage);
	void SetComponentColor(
		UStaticMeshComponent* Component,
		TObjectPtr<UMaterialInstanceDynamic>& Storage,
		const FLinearColor& Color);
	void SetMaterialFamilyColor(const FName& MaterialName, const FLinearColor& Color);

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

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RuntimeMaterialInstances;

	bool bWheelsSpinning = false;
};
