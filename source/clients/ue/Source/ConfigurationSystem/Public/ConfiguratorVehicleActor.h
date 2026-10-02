#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CarConfigurationState.h"
#include "ConfiguratorVehicleActor.generated.h"

class UMaterialInstanceDynamic;
class USceneComponent;
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

	/** 把领域状态映射到占位几何颜色，不参与 canonical key 或价格计算。 */
	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	void ApplyConfiguration(const FCarConfigurationSelection& Selection);

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

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PaintMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WheelMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> InteriorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrameMaterial;
};
