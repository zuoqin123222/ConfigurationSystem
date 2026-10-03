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

bool AConfigShowroomPlayerController::IsInteriorCameraPreset(const int32 CameraIndex)
{
	return CameraIndex == 4 || CameraIndex == 5;
}

bool AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(
	const int32 FromCameraIndex,
	const int32 ToCameraIndex)
{
	return IsInteriorCameraPreset(FromCameraIndex)
		|| IsInteriorCameraPreset(ToCameraIndex);
}

bool AConfigShowroomPlayerController::ShouldCancelPendingCameraTransition(
	const int32 CurrentCameraIndex,
	const int32 PendingCameraIndex,
	const int32 RequestedCameraIndex)
{
	return PendingCameraIndex != INDEX_NONE
		&& RequestedCameraIndex == CurrentCameraIndex;
}

bool AConfigShowroomPlayerController::ShouldReplacePendingCameraTransition(
	const int32 CurrentCameraIndex,
	const int32 PendingCameraIndex,
	const int32 RequestedCameraIndex)
{
	return PendingCameraIndex != INDEX_NONE
		&& RequestedCameraIndex != CurrentCameraIndex;
}

bool AConfigShowroomPlayerController::IsCameraPanAllowed(const int32 CameraIndex)
{
	return !IsInteriorCameraPreset(CameraIndex);
}

FVector AConfigShowroomPlayerController::RotateExteriorCameraLocation(
	const FVector& Location,
	const FVector& Pivot,
	const float YawDegrees,
	const float PitchDegrees)
{
	const FVector Relative = Location - Pivot;
	const float Radius = Relative.Size();
	if (Radius <= KINDA_SMALL_NUMBER)
	{
		return Location;
	}

	FRotator OrbitRotation = Relative.Rotation();
	OrbitRotation.Yaw = FRotator::NormalizeAxis(OrbitRotation.Yaw + YawDegrees);
	OrbitRotation.Pitch = FMath::Clamp(
		OrbitRotation.Pitch + PitchDegrees,
		-80.0f,
		80.0f);
	return Pivot + OrbitRotation.Vector() * Radius;
}

FVector AConfigShowroomPlayerController::ClampInteriorCameraLocation(
	const FVector& PresetLocation,
	const FVector& CandidateLocation,
	const float MaxDistance)
{
	const FVector Offset = CandidateLocation - PresetLocation;
	return PresetLocation + Offset.GetClampedToMaxSize(FMath::Max(0.0f, MaxDistance));
}

void AConfigShowroomPlayerController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOrbitTransitionActive || !IsValid(RuntimeCamera))
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
	RuntimeCamera->ApplyCameraPOV(POV);

	if (Alpha >= 1.0f)
	{
		bOrbitTransitionActive = false;
		RuntimeCamera->ApplyCameraPOV(CameraTransitionEndPOV);
	}
}

void AConfigShowroomPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	ShowroomCameras.SetNum(6);
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
	InputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AConfigShowroomPlayerController::Camera5);
	InputComponent->BindAxisKey(
		EKeys::MouseX, this, &AConfigShowroomPlayerController::HandleCameraHorizontal);
	InputComponent->BindAxisKey(
		EKeys::MouseY, this, &AConfigShowroomPlayerController::HandleCameraVertical);
	InputComponent->BindAxisKey(
		EKeys::MouseWheelAxis, this, &AConfigShowroomPlayerController::HandleCameraZoom);
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
	if (ShouldCancelPendingCameraTransition(
		CurrentCameraIndex, PendingCameraIndex, CameraIndex))
	{
		GetWorldTimerManager().ClearTimer(CameraZoneTransitionTimer);
		PendingCameraIndex = INDEX_NONE;
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->StartCameraFade(
				1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
		}
		return true;
	}
	if (CameraIndex == CurrentCameraIndex
		&& PendingCameraIndex == INDEX_NONE
		&& IsValid(RuntimeCamera)
		&& GetViewTarget() == RuntimeCamera)
	{
		return true;
	}

	const bool bReplacingPendingBlackTransition =
		ShouldReplacePendingCameraTransition(
			CurrentCameraIndex, PendingCameraIndex, CameraIndex);
	bOrbitTransitionActive = false;
	GetWorldTimerManager().ClearTimer(CameraZoneTransitionTimer);
	PendingCameraIndex = INDEX_NONE;

	if (!IsValid(RuntimeCamera))
	{
		RuntimeCamera = GetWorld()->SpawnActor<AConfigRuntimeCameraActor>();
	}
	if (!IsValid(RuntimeCamera) || PlayerCameraManager == nullptr)
	{
		return false;
	}

	if (ShouldUseBlackCameraTransition(CurrentCameraIndex, CameraIndex))
	{
		PendingCameraIndex = CameraIndex;
		PlayerCameraManager->StartCameraFade(
			0.0f, 1.0f, 0.18f, FLinearColor::Black, false, true);
		GetWorldTimerManager().SetTimer(
			CameraZoneTransitionTimer,
			this,
			&AConfigShowroomPlayerController::FinishInteriorExteriorCameraSwitch,
			0.18f,
			false);
		return true;
	}
	if (bReplacingPendingBlackTransition)
	{
		PlayerCameraManager->StartCameraFade(
			1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
	}

	if (!GetCameraPresetPOV(CameraIndex, CameraTransitionEndPOV))
	{
		return false;
	}
	CameraTransitionStartPOV = PlayerCameraManager->GetCameraCacheView();
	CameraTransitionPivot = GetVehicleCameraPivot();
	CameraTransitionElapsed = 0.0f;
	CurrentCameraIndex = CameraIndex;
	RuntimeCamera->ApplyCameraPOV(CameraTransitionStartPOV);
	SetViewTarget(RuntimeCamera);
	bOrbitTransitionActive = true;
	return true;
}

bool AConfigShowroomPlayerController::SetCamera(const int32 CameraIndex)
{
	return SwitchCamera(CameraIndex);
}

void AConfigShowroomPlayerController::FinishInteriorExteriorCameraSwitch()
{
	if (!ShowroomCameras.IsValidIndex(PendingCameraIndex)
		|| !IsValid(ShowroomCameras[PendingCameraIndex]))
	{
		PendingCameraIndex = INDEX_NONE;
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->StartCameraFade(
				1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
		}
		return;
	}
	FMinimalViewInfo PresetPOV;
	if (!IsValid(RuntimeCamera) || !GetCameraPresetPOV(PendingCameraIndex, PresetPOV))
	{
		PendingCameraIndex = INDEX_NONE;
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->StartCameraFade(
				1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
		}
		return;
	}
	CurrentCameraIndex = PendingCameraIndex;
	PendingCameraIndex = INDEX_NONE;
	RuntimeCamera->ApplyCameraPOV(PresetPOV);
	SetViewTarget(RuntimeCamera);
	if (PlayerCameraManager != nullptr)
	{
		PlayerCameraManager->StartCameraFade(
			1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
	}
}

FVector AConfigShowroomPlayerController::GetVehicleCameraPivot() const
{
	return IsValid(Vehicle)
		? Vehicle->GetActorLocation() + FVector(0.0, 0.0, 110.0)
		: FVector(0.0, 0.0, 110.0);
}

bool AConfigShowroomPlayerController::GetCameraPresetPOV(
	const int32 CameraIndex,
	FMinimalViewInfo& OutPOV) const
{
	if (!ShowroomCameras.IsValidIndex(CameraIndex)
		|| !IsValid(ShowroomCameras[CameraIndex])
		|| !IsValid(ShowroomCameras[CameraIndex]->GetCameraComponent()))
	{
		return false;
	}
	ShowroomCameras[CameraIndex]->GetCameraComponent()->GetCameraView(0.0f, OutPOV);
	return true;
}

AConfigRuntimeCameraActor* AConfigShowroomPlayerController::GetInteractiveCamera() const
{
	if (bOrbitTransitionActive
		|| PendingCameraIndex != INDEX_NONE
		|| !ShowroomCameras.IsValidIndex(CurrentCameraIndex)
		|| !IsValid(RuntimeCamera)
		|| GetViewTarget() != RuntimeCamera)
	{
		return nullptr;
	}
	return RuntimeCamera;
}

void AConfigShowroomPlayerController::RotateInteractiveCamera(
	const float YawDegrees,
	const float PitchDegrees)
{
	AConfigRuntimeCameraActor* Camera = GetInteractiveCamera();
	if (!IsValid(Camera))
	{
		return;
	}

	FMinimalViewInfo POV = Camera->GetCameraPOV();
	if (IsInteriorCameraPreset(CurrentCameraIndex))
	{
		POV.Rotation.Yaw = FRotator::NormalizeAxis(POV.Rotation.Yaw + YawDegrees);
		POV.Rotation.Pitch = FMath::Clamp(
			POV.Rotation.Pitch + PitchDegrees, -80.0f, 80.0f);
		POV.Rotation.Roll = 0.0f;
		Camera->ApplyCameraPOV(POV);
		return;
	}

	const FVector Pivot = GetVehicleCameraPivot();
	const FVector Location = RotateExteriorCameraLocation(
		POV.Location,
		Pivot,
		YawDegrees,
		PitchDegrees);
	POV.Location = Location;
	POV.Rotation = (Pivot - Location).Rotation();
	Camera->ApplyCameraPOV(POV);
}

void AConfigShowroomPlayerController::PanInteractiveCamera(
	const float Horizontal,
	const float Vertical)
{
	AConfigRuntimeCameraActor* Camera = GetInteractiveCamera();
	if (!IsValid(Camera) || !IsCameraPanAllowed(CurrentCameraIndex))
	{
		return;
	}
	FMinimalViewInfo POV = Camera->GetCameraPOV();
	POV.Location += POV.Rotation.RotateVector(FVector::RightVector) * Horizontal
		+ POV.Rotation.RotateVector(FVector::UpVector) * Vertical;
	Camera->ApplyCameraPOV(POV);
}

void AConfigShowroomPlayerController::DollyInteractiveCamera(const float Amount)
{
	AConfigRuntimeCameraActor* Camera = GetInteractiveCamera();
	if (!IsValid(Camera))
	{
		return;
	}

	FMinimalViewInfo POV = Camera->GetCameraPOV();
	const FVector Candidate =
		POV.Location + POV.Rotation.RotateVector(FVector::ForwardVector) * Amount;
	if (IsInteriorCameraPreset(CurrentCameraIndex))
	{
		FMinimalViewInfo PresetPOV;
		if (GetCameraPresetPOV(CurrentCameraIndex, PresetPOV))
		{
			POV.Location = ClampInteriorCameraLocation(
				PresetPOV.Location, Candidate, 120.0f);
			Camera->ApplyCameraPOV(POV);
		}
		return;
	}

	const float CandidateRadius = FVector::Distance(Candidate, GetVehicleCameraPivot());
	if (CandidateRadius >= 180.0f && CandidateRadius <= 3000.0f)
	{
		POV.Location = Candidate;
		Camera->ApplyCameraPOV(POV);
	}
}

void AConfigShowroomPlayerController::HandleCameraHorizontal(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	if (IsInputKeyDown(EKeys::LeftMouseButton))
	{
		RotateInteractiveCamera(Value * 0.35f, 0.0f);
	}
	else if (IsInputKeyDown(EKeys::RightMouseButton)
		|| IsInputKeyDown(EKeys::MiddleMouseButton))
	{
		PanInteractiveCamera(Value * 2.0f, 0.0f);
	}
}

void AConfigShowroomPlayerController::HandleCameraVertical(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	if (IsInputKeyDown(EKeys::LeftMouseButton))
	{
		RotateInteractiveCamera(0.0f, -Value * 0.35f);
	}
	else if (IsInputKeyDown(EKeys::RightMouseButton)
		|| IsInputKeyDown(EKeys::MiddleMouseButton))
	{
		PanInteractiveCamera(0.0f, -Value * 2.0f);
	}
}

void AConfigShowroomPlayerController::HandleCameraZoom(const float Value)
{
	if (!FMath::IsNearlyZero(Value))
	{
		DollyInteractiveCamera(Value * 35.0f);
	}
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

bool AConfigShowroomPlayerController::SetAnimationEnabled(const bool bEnabled)
{
	if (!IsValid(Vehicle))
	{
		return false;
	}
	Vehicle->SetWheelAnimationEnabled(bEnabled);
	return Vehicle->IsWheelAnimationEnabled() == bEnabled;
}

bool AConfigShowroomPlayerController::IsAnimationEnabled() const
{
	return IsValid(Vehicle) && Vehicle->IsWheelAnimationEnabled();
}

bool AConfigShowroomPlayerController::SetLightPreset(const FString& Preset)
{
	if (!IsValid(Environment))
	{
		return false;
	}
	if (Preset == TEXT("studio"))
	{
		Environment->SetEnvironmentIndex(0);
	}
	else if (Preset == TEXT("outdoor"))
	{
		Environment->SetEnvironmentIndex(1);
	}
	else
	{
		return false;
	}
	FString IgnoredSaveError;
	SaveExperience(IgnoredSaveError);
	return GetLightPreset() == Preset;
}

FString AConfigShowroomPlayerController::GetLightPreset() const
{
	return GetCurrentEnvironmentIndex() == 1 ? TEXT("outdoor") : TEXT("studio");
}

bool AConfigShowroomPlayerController::TogglePathTracing(FString& OutFailureReason)
{
	return SetRenderMode(
		GetRenderMode() == TEXT("path-tracing") ? TEXT("realtime") : TEXT("path-tracing"),
		OutFailureReason);
}

bool AConfigShowroomPlayerController::SetRenderMode(
	const FString& Mode,
	FString& OutFailureReason)
{
	const bool bEnablePathTracing = Mode == TEXT("path-tracing");
	if (!bEnablePathTracing && Mode != TEXT("realtime"))
	{
		OutFailureReason = TEXT("不支持的渲染模式。");
		return false;
	}
	UGameInstance* GameInstance = GetGameInstance();
	if (!IsValid(GameInstance))
	{
		OutFailureReason = TEXT("GameInstance 不可用。");
		return false;
	}
	UPathTracingExperienceSubsystem* PathTracing =
		GameInstance->GetSubsystem<UPathTracingExperienceSubsystem>();
	if (IsValid(PathTracing) && bEnablePathTracing && IsValid(Vehicle))
	{
		// 持续运动会让 Path Tracing 每帧清空累积；产品模式进入前先平滑停轮。
		Vehicle->SetWheelAnimationEnabled(false);
	}
	return IsValid(PathTracing)
		&& PathTracing->SetPathTracingEnabled(bEnablePathTracing, OutFailureReason);
}

FString AConfigShowroomPlayerController::GetRenderMode() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UPathTracingExperienceSubsystem* PathTracing = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UPathTracingExperienceSubsystem>()
		: nullptr;
	return IsValid(PathTracing) && PathTracing->IsPathTracingEnabled()
		? TEXT("path-tracing")
		: TEXT("realtime");
}

void AConfigShowroomPlayerController::ResetPresentation()
{
	SwitchCamera(0);
	SetAnimationEnabled(false);
	SetLightPreset(TEXT("studio"));
	FString IgnoredFailureReason;
	SetRenderMode(TEXT("realtime"), IgnoredFailureReason);
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
void AConfigShowroomPlayerController::Camera5() { SwitchCamera(5); }
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
