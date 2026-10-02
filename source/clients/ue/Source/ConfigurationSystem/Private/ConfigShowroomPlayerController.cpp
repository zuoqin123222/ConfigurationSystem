#include "ConfigShowroomPlayerController.h"

#include "Camera/CameraActor.h"
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
	const FVector Location = InterpolateOrbitLocation(
		CameraTransitionStart,
		CameraTransitionEnd,
		CameraTransitionPivot,
		Alpha);
	TransitionCamera->SetActorLocation(Location);
	TransitionCamera->SetActorRotation((CameraTransitionPivot - Location).Rotation());

	if (Alpha >= 1.0f)
	{
		bOrbitTransitionActive = false;
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
		}
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
		TransitionCamera = GetWorld()->SpawnActor<ACameraActor>();
	}
	if (!IsValid(TransitionCamera) || PlayerCameraManager == nullptr)
	{
		return false;
	}
	CameraTransitionStart = PlayerCameraManager->GetCameraLocation();
	CameraTransitionEnd = ShowroomCameras[CameraIndex]->GetActorLocation();
	CameraTransitionPivot = GetVehicleCameraPivot();
	CameraTransitionElapsed = 0.0f;
	CurrentCameraIndex = CameraIndex;
	TransitionCamera->SetActorLocation(CameraTransitionStart);
	TransitionCamera->SetActorRotation(
		(CameraTransitionPivot - CameraTransitionStart).Rotation());
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
	if (!IsValid(Snapshot) || !IsValid(Configurator)
		|| !IsValid(Environment) || !IsValid(ConfiguratorPanel)
		|| Snapshot->EnvironmentIndex < 0 || Snapshot->EnvironmentIndex > 1
		|| !Configurator->CanApplySelection(Snapshot->Configuration))
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
		FString IgnoredSaveError;
		SaveExperience(IgnoredSaveError);
	}
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
