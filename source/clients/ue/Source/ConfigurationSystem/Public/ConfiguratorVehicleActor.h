#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AutomotiveCatalogDomain.h"
#include "CarConfigurationState.h"
#include "ConfiguratorVehicleActor.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;
class UReversiblePartActuatorComponent;
class USceneComponent;
class USkeletalMeshComponent;
class USmoothWheelControllerComponent;
class UStaticMeshComponent;
class UVehicleAnimSequencePlayerComponent;
struct FVehicleAnimationClip;

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
	virtual void Tick(float DeltaSeconds) override;

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

	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	bool PlayVehicleAnimation(FName AnimationId);

	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	bool CloseVehicleAnimation(FName AnimationId);

	/** 原子聚焦动画；NAME_None 表示关闭，快速请求只保留最后一个目标。 */
	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	bool FocusVehicleAnimation(FName NextAnimationId);

	/** Path Tracing 使用：立即冻结骨骼与所有静态代理运动。 */
	UFUNCTION(BlueprintCallable, Category = "车辆体验")
	void FreezeAllVehicleMotion();

	UFUNCTION(BlueprintPure, Category = "车辆体验")
	FName GetActiveVehicleAnimationId() const;

	/** 面向 UI 的焦点状态；切焦过程中立即暴露 pending 目标。 */
	UFUNCTION(BlueprintPure, Category = "车辆体验")
	FName GetFocusedVehicleAnimationId() const;

	UFUNCTION(BlueprintPure, Category = "车辆体验")
	bool IsVehicleAnimationPlaying() const;

	UFUNCTION(BlueprintPure, Category = "车辆体验")
	bool CanPlayVehicleAnimation(FName AnimationId) const;

	/** CEF Shipping 探针使用的只读执行器状态；仅接受稳定动画 ID。 */
	UFUNCTION(BlueprintPure, Category = "车辆体验")
	FString GetAnimationExecutorStateJson(FName AnimationId) const;

	/** 从已校验的车型目录一次性加载整车骨骼网格、完整序列并转换全部帧段。 */
	bool ConfigureAnimationFromCatalog(const AutomotiveCatalog::FCatalog& Catalog);

	static bool BuildAnimationClips(
		const AutomotiveCatalog::FCatalog& Catalog,
		TArray<FVehicleAnimationClip>& OutClips);
	static bool ShouldUseStaticAnimationFallback(
		bool bHasSkeletalMesh,
		bool bHasSequence);

	static FRotator GetHoodOpenRotation();
	static FRotator GetTrunkOpenRotation();

	/** 自动化探针使用：验证真实几何或代理回退、分区标签及可逆执行器绑定。 */
	bool HasStablePlaceholderBindings(TArray<FString>& OutErrors) const;
	int32 GetCatalogSurfaceTargetCount() const;
	UMeshComponent* FindCatalogSurfaceTarget(FName SlotId) const;

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
	bool ApplyStaticAnimationFallback(FName AnimationId, bool bOpen);
	bool IsStaticAnimationSupported(FName AnimationId) const;
	bool IsStaticAnimationMoving(FName AnimationId) const;
	void StartPendingStaticAnimation();
	void SetStaticProxyVisible(bool bVisible);

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TObjectPtr<USceneComponent> VehicleRoot;

	/** 把源模型的水平几何中心对齐 Actor 原点，并把轮胎最低点抬到 Z=0。 */
	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TObjectPtr<USceneComponent> ContentRoot;

	/** 优先显示与完整动画序列 Skeleton 匹配的多骨骼整车；资产不可用时显示静态代理。 */
	UPROPERTY(VisibleAnywhere, Category = "车辆动画")
	TObjectPtr<USkeletalMeshComponent> SkeletalVehicle;

	UPROPERTY(VisibleAnywhere, Category = "车辆动画")
	TObjectPtr<UVehicleAnimSequencePlayerComponent> AnimationPlayer;

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
	TArray<TObjectPtr<UStaticMeshComponent>> WheelTires;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TArray<TObjectPtr<UStaticMeshComponent>> WheelRotors;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TArray<TObjectPtr<UStaticMeshComponent>> BrakeCalipers;

	UPROPERTY(VisibleAnywhere, Category = "占位车辆")
	TArray<TObjectPtr<UStaticMeshComponent>> DoorMirrorParts;

	/** A5 独立静态分件；语义缺失项仍是明确标识的代理，不代表正式 SC01 几何。 */
	UPROPERTY(VisibleAnywhere, Category = "占位车辆|Catalog代理")
	TArray<TObjectPtr<UStaticMeshComponent>> CatalogSurfaceProxyParts;

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
	bool bAnimationCatalogConfigured = false;
	bool bAnimationSequenceReady = false;
	bool bStaticAnimationFallbackEnabled = true;
	TMap<FName, FString> StaticAnimationCloseModes;
	FName ActiveFallbackAnimationId;
	FName PendingFallbackAnimationId;
	bool bHasPendingFallbackFocus = false;
};
