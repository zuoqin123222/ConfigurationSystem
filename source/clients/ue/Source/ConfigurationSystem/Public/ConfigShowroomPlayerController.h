#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraTypes.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "ConfigShowroomPlayerController.generated.h"

class UConfiguratorPanel;
class ACameraActor;
class AConfiguratorVehicleActor;
class AShowroomEnvironmentActor;

/** 独立运行时视图目标，直接输出完整 FMinimalViewInfo；自由操作不会回写预设 Actor。 */
UCLASS(NotBlueprintable, Transient)
class CONFIGURATIONSYSTEM_API AConfigRuntimeCameraActor final : public ACameraActor
{
	GENERATED_BODY()

public:
	void ApplyCameraPOV(const FMinimalViewInfo& InPOV);
	const FMinimalViewInfo& GetCameraPOV() const { return CameraPOV; }
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override
	{
		(void)DeltaTime;
		OutResult = CameraPOV;
	}

private:
	FMinimalViewInfo CameraPOV;
};

/** 纯 C++ 展厅控制器：语义机位切换、自由镜头、双环境、交互部件与原子快照。 */
UCLASS()
class CONFIGURATIONSYSTEM_API AConfigShowroomPlayerController final
	: public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void UpdateCameraManager(float DeltaSeconds) override;

public:
	AConfigShowroomPlayerController();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category="Configurator|Camera")
	bool SwitchCamera(int32 CameraIndex);

	/** 原生体验 UI 的明确 Set 入口；保留 SwitchCamera 兼容既有调用。 */
	UFUNCTION(BlueprintCallable, Category="Configurator|Camera")
	bool SetCamera(int32 CameraIndex);

	/** 按地图中的 Configurator.Camera.<id> 语义标签切换机位。 */
	UFUNCTION(BlueprintCallable, Category="Configurator|Camera")
	bool SetCameraId(const FString& CameraId);

	/** 解析合法的小写语义机位标签；Interior companion tag 不会被当作 id。 */
	static bool TryParseCameraIdTag(FName Tag, FString& OutCameraId);

	/** Interior companion tag 决定是否跨区黑屏，与 id 命名无关。 */
	static bool ShouldUseBlackCameraTransition(
		bool bFromInterior,
		bool bToInterior,
		bool bSameCamera);

	/** 外部机位围绕 Pivot 按最短方位角弧线插值，供运行时和自动化测试共用。 */
	static FVector InterpolateOrbitLocation(
		const FVector& Start,
		const FVector& End,
		const FVector& Pivot,
		float Alpha);

	/** 插值完整镜头 POV，外部机位的位置仍沿绕车圆弧运动。 */
	static FMinimalViewInfo InterpolateCameraPOV(
		const FMinimalViewInfo& Start,
		const FMinimalViewInfo& End,
		const FVector& Pivot,
		float Alpha);

	/** 从默认外景机位生成首屏远景，用约两秒绕车拉近到目标。 */
	static FMinimalViewInfo BuildRevealStartPOV(
		const FMinimalViewInfo& Target,
		const FVector& Pivot);

	/** 右键平移相机和独立轨道 Pivot 时使用同一个世界空间位移。 */
	static FVector CalculateOrbitPanDelta(
		const FRotator& CameraRotation,
		float Horizontal,
		float Vertical);

	/** 沿当前视线建立轨道中心，确保第一次旋转不改变现有构图。 */
	static FVector CalculateViewAlignedOrbitPivot(
		const FMinimalViewInfo& POV,
		const FVector& ReferencePivot);

	/** 驾驶位和副驾位都属于车内预设。 */
	static bool IsInteriorCameraPreset(int32 CameraIndex);

	/** 只要切换任一端为车内预设，就必须使用黑屏切换。 */
	static bool ShouldUseBlackCameraTransition(int32 FromCameraIndex, int32 ToCameraIndex);

	/** 淡黑等待期间重选当前机位表示取消尚未完成的切换。 */
	static bool ShouldCancelPendingCameraTransition(
		int32 CurrentCameraIndex,
		int32 PendingCameraIndex,
		int32 RequestedCameraIndex);

	/** 淡黑期间改选其他机位时替换旧目标，避免残留黑幕和交互锁。 */
	static bool ShouldReplacePendingCameraTransition(
		int32 CurrentCameraIndex,
		int32 PendingCameraIndex,
		int32 RequestedCameraIndex);

	/** 平移仅对车外预设开放；车内仍可旋转和调整 FOV。 */
	static bool IsCameraPanAllowed(int32 CameraIndex);

	/** 车外轨道旋转保持相机到车辆 Pivot 的距离。 */
	static FVector RotateExteriorCameraLocation(
		const FVector& Location,
		const FVector& Pivot,
		float YawDegrees,
		float PitchDegrees);

	/** 车内滚轮只调整 FOV：正向滚轮缩小 FOV，反向滚轮扩大 FOV。 */
	static float CalculateInteriorZoomFov(float CurrentFov, float WheelDelta);

	UFUNCTION(BlueprintCallable, Category="Configurator|Environment")
	void ToggleEnvironment();

	UFUNCTION(BlueprintCallable, Category="Configurator|Experience")
	bool SetAnimationEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category="Configurator|Experience")
	bool IsAnimationEnabled() const;

	UFUNCTION(BlueprintCallable, Category="Configurator|Experience")
	bool PlayAnimation(FName AnimationId);

	UFUNCTION(BlueprintCallable, Category="Configurator|Experience")
	bool CloseAnimation(FName AnimationId);

	UFUNCTION(BlueprintCallable, Category="Configurator|Experience")
	bool FocusAnimation(FName NextAnimationId);

	UFUNCTION(BlueprintPure, Category="Configurator|Experience")
	bool CanPlayAnimation(FName AnimationId) const;

	UFUNCTION(BlueprintPure, Category="Configurator|Experience")
	FName GetActiveAnimationId() const;

	UFUNCTION(BlueprintCallable, Category="Configurator|Environment")
	bool SetLightPreset(const FString& Preset);

	UFUNCTION(BlueprintPure, Category="Configurator|Environment")
	FString GetLightPreset() const;

	UFUNCTION(BlueprintCallable, Category="Configurator|Rendering")
	bool TogglePathTracing(FString& OutFailureReason);

	UFUNCTION(BlueprintCallable, Category="Configurator|Rendering")
	bool SetRenderMode(const FString& Mode, FString& OutFailureReason);

	UFUNCTION(BlueprintPure, Category="Configurator|Rendering")
	FString GetRenderMode() const;

	UFUNCTION(BlueprintCallable, Category="Configurator|Experience")
	void ResetPresentation();

	UFUNCTION(BlueprintCallable, Category="Configurator|Persistence")
	bool SaveExperience(FString& OutError);

	UFUNCTION(BlueprintCallable, Category="Configurator|Persistence")
	bool LoadExperience(FString& OutError);

	UFUNCTION(BlueprintCallable, Category="Configurator|Vehicle")
	void ToggleVehiclePart(FName PartId);

	UFUNCTION(BlueprintCallable, Category="Configurator|Vehicle")
	void ToggleWheelSpin();

	UFUNCTION(BlueprintPure, Category="Configurator|Camera")
	int32 GetCurrentCameraIndex() const { return CurrentCameraIndex; }

	UFUNCTION(BlueprintPure, Category="Configurator|Camera")
	FString GetCurrentCameraId() const { return CurrentCameraId; }

	UFUNCTION(BlueprintPure, Category="Configurator|Environment")
	int32 GetCurrentEnvironmentIndex() const;

	UFUNCTION(BlueprintPure, Category="Configurator|UI")
	bool IsConfiguratorPanelVisible() const;

private:
	UFUNCTION()
	void HandlePersistentConfigurationChanged();
	UFUNCTION()
	void FlushPersistentConfiguration();

	void Camera0(); void Camera1(); void Camera2(); void Camera3(); void Camera4(); void Camera5();
	void DiscoverCameraPresets();
	bool SwitchToCamera(ACameraActor* Camera, int32 LegacyCameraIndex, const FString& CameraId);
	bool IsInteriorCamera(const ACameraActor* Camera) const;
	bool IsCurrentCameraInterior() const;
	FString FindSemanticCameraId(const ACameraActor* Camera) const;
	void HandleCameraHorizontal(float Value);
	void HandleCameraVertical(float Value);
	void HandleCameraZoom(float Value);
	void HandleOrbitPressed();
	AConfigRuntimeCameraActor* GetInteractiveCamera() const;
	bool GetCameraPresetPOV(int32 CameraIndex, FMinimalViewInfo& OutPOV) const;
	bool GetCameraPresetPOV(const ACameraActor* Camera, FMinimalViewInfo& OutPOV) const;
	void RotateInteractiveCamera(float YawDegrees, float PitchDegrees);
	void PanInteractiveCamera(float Horizontal, float Vertical);
	void DollyInteractiveCamera(float Amount);
	void AdjustInteriorCameraFov(float WheelDelta);
	void SetInteractiveTargetPOV(const FMinimalViewInfo& POV);
	void ResetInteractiveOrbit(const FMinimalViewInfo& POV, bool bResetPivot);
	void UpdateStageProjectionOffset(float DeltaSeconds);
	void ApplyRuntimeCameraPOV(const FMinimalViewInfo& POV);
	void ToggleEnvironmentInput();
	void TogglePathTracingInput();
	void SaveInput();
	void LoadInput();
	void ToggleLeftDoor(); void ToggleRightDoor(); void ToggleHood(); void ToggleTrunk();
	void ToggleWheelsInput();
	void CancelInitialCameraReveal(bool bSnapToPreset);
	void StartInitialCameraReveal();
	void FinishInitialCameraReveal();
	void FinishInteriorExteriorCameraSwitch();
	FVector GetVehicleCameraPivot() const;

	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorPanel> ConfiguratorPanel;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACameraActor>> ShowroomCameras;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<ACameraActor>> ShowroomCamerasById;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CurrentCameraPreset;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> PendingCameraPreset;

	UPROPERTY(Transient)
	TObjectPtr<AShowroomEnvironmentActor> Environment;

	UPROPERTY(Transient)
	TObjectPtr<AConfiguratorVehicleActor> Vehicle;

	UPROPERTY(Transient)
	TObjectPtr<AConfigRuntimeCameraActor> RuntimeCamera;

	int32 CurrentCameraIndex = INDEX_NONE;
	int32 PendingCameraIndex = INDEX_NONE;
	FString CurrentCameraId;
	FString PendingCameraId;
	FMinimalViewInfo CameraTransitionStartPOV;
	FMinimalViewInfo CameraTransitionEndPOV;
	FVector CameraTransitionPivot = FVector::ZeroVector;
	FVector OrbitPivot = FVector::ZeroVector;
	FMinimalViewInfo InteractiveTargetPOV;
	float CameraTransitionElapsed = 0.0f;
	float CameraTransitionDuration = 0.85f;
	/** 当前非对称投影补偿；独立于机位 Transform，便于关闭后无损回退。 */
	float CurrentStageProjectionOffsetX = 0.0f;
	FTimerHandle InitialRevealTimer;
	FTimerHandle InitialRevealCompletionTimer;
	FTimerHandle CameraZoneTransitionTimer;
	FTimerHandle PersistenceDebounceTimer;
	bool bOrbitTransitionActive = false;
	bool bInitialRevealPending = false;
	bool bInitialRevealActive = false;
	bool bInteractiveSmoothingActive = false;
	bool bApplyingLoadedSnapshot = false;
};
