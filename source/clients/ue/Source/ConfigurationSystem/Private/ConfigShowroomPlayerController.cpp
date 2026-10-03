#include "ConfigShowroomPlayerController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CarConfiguratorSubsystem.h"
#include "ConfiguratorExperienceSaveGame.h"
#include "ConfiguratorPanel.h"
#include "ConfiguratorVehicleActor.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "PathTracingExperienceSubsystem.h"
#include "Sc01V2ConfigurationState.h"
#include "ShowroomEnvironmentActor.h"
#include "TimerManager.h"

AConfigShowroomPlayerController::AConfigShowroomPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

FVector AConfigShowroomPlayerController::InterpolateOrbitLocation(
	const FVector& Start,
	const FVector& End,
	const FVector& Pivot,
	const float Alpha)
{
	const float Eased = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f));
	const FVector StartRelative = Start - Pivot;
	const FVector EndRelative = End - Pivot;
	const float StartRadius = FVector2D(StartRelative.X, StartRelative.Y).Size();
	const float EndRadius = FVector2D(EndRelative.X, EndRelative.Y).Size();
	const float StartYaw = FMath::RadiansToDegrees(FMath::Atan2(StartRelative.Y, StartRelative.X));
	const float EndYaw = FMath::RadiansToDegrees(FMath::Atan2(EndRelative.Y, EndRelative.X));
	const float Yaw = StartYaw + FMath::FindDeltaAngleDegrees(StartYaw, EndYaw) * Eased;
	const float Radius = FMath::Lerp(StartRadius, EndRadius, Eased);
	const float Height = FMath::Lerp(StartRelative.Z, EndRelative.Z, Eased);
	return Pivot + FVector(
		FMath::Cos(FMath::DegreesToRadians(Yaw)) * Radius,
		FMath::Sin(FMath::DegreesToRadians(Yaw)) * Radius,
		Height);
}

FMinimalViewInfo AConfigShowroomPlayerController::InterpolateCameraPOV(
	const FMinimalViewInfo& Start,
	const FMinimalViewInfo& End,
	const FVector& Pivot,
	const float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (ClampedAlpha <= 0.0f)
	{
		return Start;
	}
	if (ClampedAlpha >= 1.0f)
	{
		// BlendViewInfo intentionally ignores several discrete/advanced fields.
		// Return the complete target POV at the boundary so the transition actor
		// cannot retain defaults for any FMinimalViewInfo member.
		return End;
	}
	const float Eased = FMath::SmoothStep(0.0f, 1.0f, ClampedAlpha);
	FMinimalViewInfo Result = Start;
	FMinimalViewInfo BlendTarget = End;
	Result.BlendViewInfo(BlendTarget, Eased);
	Result.Location = InterpolateOrbitLocation(Start.Location, End.Location, Pivot, Alpha);
	Result.Rotation = FQuat::Slerp(
		Start.Rotation.Quaternion(),
		End.Rotation.Quaternion(),
		Eased).Rotator();
	Result.FOV = FMath::Lerp(Start.FOV, End.FOV, Eased);
	return Result;
}

void AConfigShowroomPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOrbitTransitionActive || !IsValid(TransitionCamera))
	{
		return;
	}

	CameraTransitionElapsed += DeltaSeconds;
	const float Alpha = FMath::Clamp(
		CameraTransitionElapsed / FMath::Max(CameraTransitionDuration, KINDA_SMALL_NUMBER),
		0.0f,
		1.0f);
	const FMinimalViewInfo POV = InterpolateCameraPOV(
		CameraTransitionStartPOV,
		CameraTransitionEndPOV,
		CameraTransitionPivot,
		Alpha);
	TransitionCamera->ApplyCameraPOV(POV);

	if (Alpha >= 1.0f)
	{
		bOrbitTransitionActive = false;
		TransitionCamera->ApplyCameraPOV(CameraTransitionEndPOV);
		SetViewTarget(ShowroomCameras[CurrentCameraIndex]);
	}
}

void AConfigShowroomPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	ShowroomCameras.SetNum(5);
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		for (int32 Index = 0; Index < ShowroomCameras.Num(); ++Index)
		{
			const FName CameraTag(*FString::Printf(TEXT("Configurator.Camera.%d"), Index));
			if (It->ActorHasTag(CameraTag))
			{
				ShowroomCameras[Index] = *It;
			}
		}
	}
	SwitchCamera(0);

	for (TActorIterator<AShowroomEnvironmentActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(AShowroomEnvironmentActor::EnvironmentActorTag))
		{
			Environment = *It;
			break;
		}
	}
	for (TActorIterator<AConfiguratorVehicleActor> It(GetWorld()); It; ++It)
	{
		Vehicle = *It;
		break;
	}

	ConfiguratorPanel = CreateWidget<UConfiguratorPanel>(
		this,
		UConfiguratorPanel::StaticClass());
	if (ConfiguratorPanel != nullptr)
	{
		ConfiguratorPanel->AddToViewport();
	}

	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	// 存档不存在时保持默认配置；有效存档在所有运行时对象就绪后一次恢复。
	FString IgnoredLoadError;
	LoadExperience(IgnoredLoadError);
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UCarConfiguratorSubsystem* Configurator =
			GameInstance->GetSubsystem<UCarConfiguratorSubsystem>())
		{
			Configurator->OnConfigurationChanged.AddDynamic(
				this, &AConfigShowroomPlayerController::HandlePersistentConfigurationChanged);
			if (USc01V2ConfigurationState* V2State = Configurator->GetSc01V2State())
			{
				V2State->OnChanged.AddDynamic(
					this, &AConfigShowroomPlayerController::HandlePersistentConfigurationChanged);
			}
		}
	}
}

void AConfigShowroomPlayerController::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UCarConfiguratorSubsystem* Configurator =
			GameInstance->GetSubsystem<UCarConfiguratorSubsystem>())
		{
			Configurator->OnConfigurationChanged.RemoveDynamic(
				this, &AConfigShowroomPlayerController::HandlePersistentConfigurationChanged);
			if (USc01V2ConfigurationState* V2State = Configurator->GetSc01V2State())
			{
				V2State->OnChanged.RemoveDynamic(
					this, &AConfigShowroomPlayerController::HandlePersistentConfigurationChanged);
			}
		}
	}
	if (GetWorldTimerManager().IsTimerActive(PersistenceDebounceTimer))
	{
		FlushPersistentConfiguration();
	}
	Super::EndPlay(EndPlayReason);
}

void AConfigShowroomPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AConfigShowroomPlayerController::Camera0);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AConfigShowroomPlayerController::Camera1);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AConfigShowroomPlayerController::Camera2);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AConfigShowroomPlayerController::Camera3);
	InputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AConfigShowroomPlayerController::Camera4);
	InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AConfigShowroomPlayerController::ToggleEnvironmentInput);
	InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AConfigShowroomPlayerController::TogglePathTracingInput);
	InputComponent->BindKey(EKeys::F5, IE_Pressed, this, &AConfigShowroomPlayerController::SaveInput);
	InputComponent->BindKey(EKeys::F9, IE_Pressed, this, &AConfigShowroomPlayerController::LoadInput);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AConfigShowroomPlayerController::ToggleLeftDoor);
	InputComponent->BindKey(EKeys::W, IE_Pressed, this, &AConfigShowroomPlayerController::ToggleRightDoor);
	InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AConfigShowroomPlayerController::ToggleHood);
	InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AConfigShowroomPlayerController::ToggleTrunk);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AConfigShowroomPlayerController::ToggleWheelsInput);
}

bool AConfigShowroomPlayerController::SwitchCamera(const int32 CameraIndex)
{
	if (!ShowroomCameras.IsValidIndex(CameraIndex) || !IsValid(ShowroomCameras[CameraIndex]))
	{
		return false;
	}
	if (CameraIndex == CurrentCameraIndex && GetViewTarget() == ShowroomCameras[CameraIndex])
	{
		return true;
	}

	const bool bCrossesInteriorBoundary =
		IsInteriorCamera(CurrentCameraIndex) != IsInteriorCamera(CameraIndex);
	bOrbitTransitionActive = false;
	GetWorldTimerManager().ClearTimer(CameraZoneTransitionTimer);

	if (bCrossesInteriorBoundary)
	{
		PendingCameraIndex = CameraIndex;
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->StartCameraFade(
				0.0f, 1.0f, 0.18f, FLinearColor::Black, false, true);
		}
		GetWorldTimerManager().SetTimer(
			CameraZoneTransitionTimer,
			this,
			&AConfigShowroomPlayerController::FinishInteriorExteriorCameraSwitch,
			0.18f,
			false);
		return true;
	}

	if (IsInteriorCamera(CameraIndex))
	{
		CurrentCameraIndex = CameraIndex;
		SetViewTargetWithBlend(ShowroomCameras[CameraIndex], 0.3f, VTBlend_EaseInOut, 2.0f);
		return true;
	}

	if (!IsValid(TransitionCamera))
	{
		TransitionCamera = GetWorld()->SpawnActor<AConfigTransitionCameraActor>();
	}
	if (!IsValid(TransitionCamera) || PlayerCameraManager == nullptr)
	{
		return false;
	}
	CameraTransitionStartPOV = PlayerCameraManager->GetCameraCacheView();
	ShowroomCameras[CameraIndex]->GetCameraComponent()->GetCameraView(
		0.0f,
		CameraTransitionEndPOV);
	CameraTransitionPivot = GetVehicleCameraPivot();
	CameraTransitionElapsed = 0.0f;
	CurrentCameraIndex = CameraIndex;
	TransitionCamera->ApplyCameraPOV(CameraTransitionStartPOV);
	SetViewTarget(TransitionCamera);
	bOrbitTransitionActive = true;
	return true;
}

void AConfigShowroomPlayerController::FinishInteriorExteriorCameraSwitch()
{
	if (!ShowroomCameras.IsValidIndex(PendingCameraIndex)
		|| !IsValid(ShowroomCameras[PendingCameraIndex]))
	{
		PendingCameraIndex = INDEX_NONE;
		return;
	}
	CurrentCameraIndex = PendingCameraIndex;
	PendingCameraIndex = INDEX_NONE;
	SetViewTarget(ShowroomCameras[CurrentCameraIndex]);
	if (PlayerCameraManager != nullptr)
	{
		PlayerCameraManager->StartCameraFade(
			1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
	}
}

bool AConfigShowroomPlayerController::IsInteriorCamera(const int32 CameraIndex) const
{
	return CameraIndex == 4;
}

FVector AConfigShowroomPlayerController::GetVehicleCameraPivot() const
{
	return IsValid(Vehicle)
		? Vehicle->GetActorLocation() + FVector(0.0, 0.0, 110.0)
		: FVector(0.0, 0.0, 110.0);
}

void AConfigShowroomPlayerController::ToggleEnvironment()
{
	if (IsValid(Environment))
	{
		Environment->ToggleEnvironment();
		FString IgnoredSaveError;
		SaveExperience(IgnoredSaveError);
	}
}

bool AConfigShowroomPlayerController::TogglePathTracing(FString& OutFailureReason)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		OutFailureReason = TEXT("GameInstance 不可用。");
		return false;
	}
	UPathTracingExperienceSubsystem* PathTracing =
		GameInstance->GetSubsystem<UPathTracingExperienceSubsystem>();
	if (IsValid(PathTracing) && !PathTracing->IsPathTracingEnabled() && IsValid(Vehicle))
	{
		// 持续运动会让 Path Tracing 每帧清空累积；产品模式进入前先平滑停轮。
		Vehicle->SetWheelMotion(0.0f, 0.0f);
	}
	return IsValid(PathTracing)
		&& PathTracing->SetPathTracingEnabled(!PathTracing->IsPathTracingEnabled(), OutFailureReason);
}

bool AConfigShowroomPlayerController::SaveExperience(FString& OutError)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		OutError = TEXT("GameInstance 不可用。");
		return false;
	}
	UCarConfiguratorSubsystem* Configurator =
		GameInstance->GetSubsystem<UCarConfiguratorSubsystem>();
	UConfiguratorPersistenceSubsystem* Persistence =
		GameInstance->GetSubsystem<UConfiguratorPersistenceSubsystem>();
	if (!IsValid(Configurator) || !IsValid(Persistence)
		|| !IsValid(Environment) || !IsValid(ConfiguratorPanel))
	{
		OutError = TEXT("配置、环境、面板或持久化子系统不可用。");
		return false;
	}

	UConfiguratorExperienceSaveGame* Snapshot =
		NewObject<UConfiguratorExperienceSaveGame>(this);
	Snapshot->Configuration = Configurator->GetSelection();
	if (USc01V2ConfigurationState* V2State = Configurator->GetSc01V2State())
	{
		Snapshot->bHasSc01V2State = V2State->IsInitialized();
		Snapshot->Sc01V2Selections = V2State->GetSelections();
		Snapshot->Sc01V2Customizations = V2State->GetCustomizations();
	}
	Snapshot->EnvironmentIndex = GetCurrentEnvironmentIndex();
	Snapshot->bPanelVisible = IsConfiguratorPanelVisible();
	return Persistence->SaveAtomic(TEXT("Experience"), Snapshot, OutError);
}

bool AConfigShowroomPlayerController::LoadExperience(FString& OutError)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		OutError = TEXT("GameInstance 不可用。");
		return false;
	}
	UCarConfiguratorSubsystem* Configurator =
		GameInstance->GetSubsystem<UCarConfiguratorSubsystem>();
	UConfiguratorPersistenceSubsystem* Persistence =
		GameInstance->GetSubsystem<UConfiguratorPersistenceSubsystem>();
	UConfiguratorExperienceSaveGame* Snapshot =
		IsValid(Persistence) ? Persistence->Load(TEXT("Experience"), OutError) : nullptr;
	USc01V2ConfigurationState* V2State =
		IsValid(Configurator) ? Configurator->GetSc01V2State() : nullptr;
	if (!IsValid(Snapshot) || !IsValid(Configurator)
		|| !IsValid(Environment) || !IsValid(ConfiguratorPanel)
		|| Snapshot->EnvironmentIndex < 0 || Snapshot->EnvironmentIndex > 1
		|| !Configurator->CanApplySelection(Snapshot->Configuration)
		|| (Snapshot->bHasSc01V2State
			&& (!IsValid(V2State)
				|| !V2State->CanApplyTransaction(
					Snapshot->Sc01V2Selections,
					Snapshot->Sc01V2Customizations))))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("存档内容校验失败。");
		}
		return false;
	}

	// 所有字段验证完成后才开始修改运行时状态。
	bApplyingLoadedSnapshot = true;
	if (!Configurator->ApplySelection(Snapshot->Configuration))
	{
		bApplyingLoadedSnapshot = false;
		OutError = TEXT("存档配置应用失败。");
		return false;
	}
	if (Snapshot->bHasSc01V2State
		&& !V2State->ApplyTransaction(
			Snapshot->Sc01V2Selections,
			Snapshot->Sc01V2Customizations))
	{
		bApplyingLoadedSnapshot = false;
		OutError = TEXT("SC01 v2 存档配置应用失败。");
		return false;
	}
	Environment->SetEnvironmentIndex(Snapshot->EnvironmentIndex);
	ConfiguratorPanel->SetVisibility(
		Snapshot->bPanelVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	bApplyingLoadedSnapshot = false;
	return true;
}

void AConfigShowroomPlayerController::HandlePersistentConfigurationChanged()
{
	if (!bApplyingLoadedSnapshot)
	{
		// 高频参数拖动只重置一次短延时存档，避免每个 slider 采样都写盘。
		GetWorldTimerManager().SetTimer(
			PersistenceDebounceTimer,
			this,
			&AConfigShowroomPlayerController::FlushPersistentConfiguration,
			0.35f,
			false);
	}
}

void AConfigShowroomPlayerController::FlushPersistentConfiguration()
{
	FString IgnoredSaveError;
	SaveExperience(IgnoredSaveError);
}

void AConfigShowroomPlayerController::ToggleVehiclePart(const FName PartId)
{
	if (IsValid(Vehicle))
	{
		Vehicle->TogglePart(PartId);
	}
}

void AConfigShowroomPlayerController::ToggleWheelSpin()
{
	if (IsValid(Vehicle))
	{
		Vehicle->ToggleWheelSpin();
	}
}

int32 AConfigShowroomPlayerController::GetCurrentEnvironmentIndex() const
{
	return IsValid(Environment) ? Environment->GetEnvironmentIndex() : 0;
}

bool AConfigShowroomPlayerController::IsConfiguratorPanelVisible() const
{
	return IsValid(ConfiguratorPanel) && ConfiguratorPanel->IsVisible();
}

void AConfigShowroomPlayerController::Camera0() { SwitchCamera(0); }
void AConfigShowroomPlayerController::Camera1() { SwitchCamera(1); }
void AConfigShowroomPlayerController::Camera2() { SwitchCamera(2); }
void AConfigShowroomPlayerController::Camera3() { SwitchCamera(3); }
void AConfigShowroomPlayerController::Camera4() { SwitchCamera(4); }
void AConfigShowroomPlayerController::ToggleEnvironmentInput() { ToggleEnvironment(); }
void AConfigShowroomPlayerController::TogglePathTracingInput()
{
	FString Ignored;
	TogglePathTracing(Ignored);
}
void AConfigShowroomPlayerController::SaveInput()
{
	FString Ignored;
	SaveExperience(Ignored);
}
void AConfigShowroomPlayerController::LoadInput()
{
	FString Ignored;
	LoadExperience(Ignored);
}
void AConfigShowroomPlayerController::ToggleLeftDoor() { ToggleVehiclePart(TEXT("door-left")); }
void AConfigShowroomPlayerController::ToggleRightDoor() { ToggleVehiclePart(TEXT("door-right")); }
void AConfigShowroomPlayerController::ToggleHood() { ToggleVehiclePart(TEXT("hood")); }
void AConfigShowroomPlayerController::ToggleTrunk() { ToggleVehiclePart(TEXT("trunk")); }
void AConfigShowroomPlayerController::ToggleWheelsInput() { ToggleWheelSpin(); }
