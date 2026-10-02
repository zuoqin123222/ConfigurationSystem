#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "ConfigShowroomPlayerController.generated.h"

class UConfiguratorPanel;
class ACameraActor;
class AConfiguratorVehicleActor;
class AShowroomEnvironmentActor;

/** 纯 C++ 展厅控制器：五机位平滑切换、双环境、交互部件与原子快照。 */
UCLASS()
class CONFIGURATIONSYSTEM_API AConfigShowroomPlayerController final
	: public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;

public:
	AConfigShowroomPlayerController();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category="Configurator|Camera")
	bool SwitchCamera(int32 CameraIndex);

	/** 外部机位围绕 Pivot 按最短方位角弧线插值，供运行时和自动化测试共用。 */
	static FVector InterpolateOrbitLocation(
		const FVector& Start,
		const FVector& End,
		const FVector& Pivot,
		float Alpha);

	UFUNCTION(BlueprintCallable, Category="Configurator|Environment")
	void ToggleEnvironment();

	UFUNCTION(BlueprintCallable, Category="Configurator|Rendering")
	bool TogglePathTracing(FString& OutFailureReason);

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

	UFUNCTION(BlueprintPure, Category="Configurator|Environment")
	int32 GetCurrentEnvironmentIndex() const;

	UFUNCTION(BlueprintPure, Category="Configurator|UI")
	bool IsConfiguratorPanelVisible() const;

private:
	UFUNCTION()
	void HandlePersistentConfigurationChanged();

	void Camera0(); void Camera1(); void Camera2(); void Camera3(); void Camera4();
	void ToggleEnvironmentInput();
	void TogglePathTracingInput();
	void SaveInput();
	void LoadInput();
	void ToggleLeftDoor(); void ToggleRightDoor(); void ToggleHood(); void ToggleTrunk();
	void ToggleWheelsInput();
	void FinishInteriorExteriorCameraSwitch();
	bool IsInteriorCamera(int32 CameraIndex) const;
	FVector GetVehicleCameraPivot() const;

	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorPanel> ConfiguratorPanel;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACameraActor>> ShowroomCameras;

	UPROPERTY(Transient)
	TObjectPtr<AShowroomEnvironmentActor> Environment;

	UPROPERTY(Transient)
	TObjectPtr<AConfiguratorVehicleActor> Vehicle;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> TransitionCamera;

	int32 CurrentCameraIndex = 0;
	int32 PendingCameraIndex = INDEX_NONE;
	FVector CameraTransitionStart = FVector::ZeroVector;
	FVector CameraTransitionEnd = FVector::ZeroVector;
	FVector CameraTransitionPivot = FVector::ZeroVector;
	float CameraTransitionElapsed = 0.0f;
	float CameraTransitionDuration = 0.85f;
	FTimerHandle CameraZoneTransitionTimer;
	bool bOrbitTransitionActive = false;
	bool bApplyingLoadedSnapshot = false;
};
