#include "ConfigShowroomPlayerController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CarConfiguratorSubsystem.h"
#include "ConfiguratorExperienceSaveGame.h"
#include "ConfiguratorPanel.h"
#include "ConfiguratorVehicleActor.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "PathTracingExperienceSubsystem.h"
#include "AutomotiveConfigurationState.h"
#include "ShowroomEnvironmentActor.h"
#include "TimerManager.h"
#include "Widgets/SWindow.h"

namespace
{
	constexpr int32 RequiredWindowWidth = 1600;
	constexpr int32 RequiredWindowHeight = 900;
	constexpr float StageProjectionInterpolationSpeed = 8.0f;
	const TCHAR* CameraTagPrefix = TEXT("Configurator.Camera.");
	const FName InteriorCameraTag(TEXT("Configurator.Camera.Interior"));

	// 安全回退开关：设为 0 后目标偏移归零，镜头 Transform、OrbitPivot 和
	// 预设机位均不需要恢复或重建。控制台：
	// config.Camera.StageAwareProjection 0
	TAutoConsoleVariable<int32> CVarStageAwareProjection(
		TEXT("config.Camera.StageAwareProjection"),
		1,
		TEXT("Compensate the camera projection for configurator UI stage insets.\n")
		TEXT("0: legacy centered full-viewport projection, 1: stage-aware projection."),
		ECVF_Default);

	void ApplyMainWindowPolicy()
	{
		if (GEngine == nullptr || GEngine->GameViewport == nullptr)
		{
			return;
		}

		if (const TSharedPtr<SWindow> MainWindow = GEngine->GameViewport->GetWindow();
			MainWindow.IsValid())
		{
			const FVector2D NonClientSize = MainWindow->GetSizeInScreen()
				- MainWindow->GetClientSizeInScreen();
			FWindowSizeLimits SizeLimits = MainWindow->GetSizeLimits();
			SizeLimits
				.SetMinWidth(RequiredWindowWidth + FMath::Max(NonClientSize.X, 0.0))
				.SetMinHeight(RequiredWindowHeight + FMath::Max(NonClientSize.Y, 0.0));
			MainWindow->SetSizeLimits(SizeLimits);
		}

		if (UGameUserSettings* UserSettings = GEngine->GetGameUserSettings())
		{
			UserSettings->SetFullscreenMode(EWindowMode::Windowed);
			UserSettings->SetScreenResolution(
				FIntPoint(RequiredWindowWidth, RequiredWindowHeight));
			UserSettings->ApplyResolutionSettings(false);
			UserSettings->ConfirmVideoMode();
			UserSettings->SaveSettings();
		}
	}
}

AConfigShowroomPlayerController::AConfigShowroomPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
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
	// 不能直接 Slerp 两个机位的朝向：当机位位于车辆两侧、方位角
	// 相差约 180 度时，朝向最短路与位置圆弧会选择相反半圆，导致
	// 镜头在过渡中点朝向车外。分别还原起终点的构图焦点，再平滑
	// 插值焦点，可保证整个圆弧过程中始终围绕车辆构图。
	const FVector StartFocus = Start.Location
		+ Start.Rotation.Vector() * FVector::Distance(Start.Location, Pivot);
	const FVector EndFocus = End.Location
		+ End.Rotation.Vector() * FVector::Distance(End.Location, Pivot);
	const FVector Focus = FMath::Lerp(StartFocus, EndFocus, Eased);
	Result.Rotation = (Focus - Result.Location).Rotation();
	Result.Rotation.Roll = FMath::Lerp(
		Start.Rotation.Roll, End.Rotation.Roll, Eased);
	Result.FOV = FMath::Lerp(Start.FOV, End.FOV, Eased);
	return Result;
}

FMinimalViewInfo AConfigShowroomPlayerController::BuildRevealStartPOV(
	const FMinimalViewInfo& Target,
	const FVector& Pivot)
{
	FMinimalViewInfo Result = Target;
	const FVector TargetRelative = Target.Location - Pivot;
	const FVector OrbitedRelative =
		TargetRelative.RotateAngleAxis(-32.0f, FVector::UpVector) * 1.45f;
	Result.Location = Pivot + OrbitedRelative + FVector(0.0, 0.0, 90.0);
	Result.Rotation = (Pivot - Result.Location).Rotation();
	// Reveal 只改变轨道位置，不改变关卡预设的镜头光学参数。
	// 否则低 FOV 车型机位会在启动时被强行放大为广角，一旦过渡 Tick
	// 受阻，用户就会永久停留在错误的远景构图。
	Result.FOV = Target.FOV;
	return Result;
}

FVector AConfigShowroomPlayerController::CalculateOrbitPanDelta(
	const FRotator& CameraRotation,
	const float Horizontal,
	const float Vertical)
{
	return CameraRotation.RotateVector(FVector::RightVector) * Horizontal
		+ CameraRotation.RotateVector(FVector::UpVector) * Vertical;
}

FVector AConfigShowroomPlayerController::CalculateViewAlignedOrbitPivot(
	const FMinimalViewInfo& POV,
	const FVector& ReferencePivot)
{
	const FVector Forward = POV.Rotation.Vector().GetSafeNormal();
	const float ReferenceDistance = FVector::Distance(POV.Location, ReferencePivot);
	const float ForwardDepth = FVector::DotProduct(
		ReferencePivot - POV.Location, Forward);
	return POV.Location + Forward * FMath::Max(
		ForwardDepth,
		FMath::Max(ReferenceDistance, 180.0f));
}

bool AConfigShowroomPlayerController::IsInteriorCameraPreset(const int32 CameraIndex)
{
	return CameraIndex == 4 || CameraIndex == 5;
}

bool AConfigShowroomPlayerController::TryParseCameraIdTag(
	const FName Tag,
	FString& OutCameraId)
{
	OutCameraId.Reset();
	const FString TagString = Tag.ToString();
	if (!TagString.StartsWith(CameraTagPrefix, ESearchCase::CaseSensitive))
	{
		return false;
	}
	const FString Candidate = TagString.RightChop(FCString::Strlen(CameraTagPrefix));
	if (Candidate.IsEmpty() || Candidate.Len() > 64)
	{
		return false;
	}
	// FName 比较不区分大小写，因此保留 interior，避免与 companion tag 混淆。
	if (Candidate.Equals(TEXT("interior"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	for (const TCHAR Character : Candidate)
	{
		if (!FChar::IsLower(Character)
			&& !FChar::IsDigit(Character)
			&& Character != TEXT('-'))
		{
			return false;
		}
	}
	OutCameraId = Candidate;
	return true;
}

bool AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(
	const bool bFromInterior,
	const bool bToInterior,
	const bool bSameCamera)
{
	return !bSameCamera && (bFromInterior || bToInterior);
}

bool AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(
	const int32 FromCameraIndex,
	const int32 ToCameraIndex)
{
	return ShouldUseBlackCameraTransition(
		IsInteriorCameraPreset(FromCameraIndex),
		IsInteriorCameraPreset(ToCameraIndex),
		FromCameraIndex == ToCameraIndex);
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
	if (!IsValid(RuntimeCamera))
	{
		return;
	}
	UpdateStageProjectionOffset(DeltaSeconds);

	if (bOrbitTransitionActive)
	{
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
		ApplyRuntimeCameraPOV(POV);

		if (Alpha >= 1.0f)
		{
			if (bInitialRevealActive)
			{
				FinishInitialCameraReveal();
			}
			else
			{
				bOrbitTransitionActive = false;
				ApplyRuntimeCameraPOV(CameraTransitionEndPOV);
				ResetInteractiveOrbit(
					CameraTransitionEndPOV,
					!IsCurrentCameraInterior());
			}
		}
		return;
	}

	if (bInteractiveSmoothingActive)
	{
		const FMinimalViewInfo Current = RuntimeCamera->GetCameraPOV();
		FMinimalViewInfo Smoothed = InteractiveTargetPOV;
		Smoothed.Location = FMath::VInterpTo(
			Current.Location, InteractiveTargetPOV.Location, DeltaSeconds, 12.0f);
		Smoothed.Rotation = FMath::RInterpTo(
			Current.Rotation, InteractiveTargetPOV.Rotation, DeltaSeconds, 12.0f);
		if (Smoothed.Location.Equals(InteractiveTargetPOV.Location, 0.05)
			&& Smoothed.Rotation.Equals(InteractiveTargetPOV.Rotation, 0.02))
		{
			Smoothed = InteractiveTargetPOV;
			bInteractiveSmoothingActive = false;
		}
		ApplyRuntimeCameraPOV(Smoothed);
		return;
	}

	// 即使镜头静止，也要在面板折叠、全屏切换或窗口缩放时刷新投影。
	ApplyRuntimeCameraPOV(RuntimeCamera->GetCameraPOV());
}

void AConfigShowroomPlayerController::UpdateCameraManager(
	const float DeltaSeconds)
{
	Super::UpdateCameraManager(DeltaSeconds);
	if (PlayerCameraManager == nullptr
		|| !IsValid(RuntimeCamera)
		|| GetViewTarget() != RuntimeCamera)
	{
		return;
	}

	// 必须在 UE 完成默认 ViewTarget/CameraModifier 更新后再写入；若只把
	// OffCenterProjectionOffset 存在 RuntimeCamera 中，UE 5.8 的默认
	// PlayerCameraManager 会在同帧最终缓存中将该高级字段重置为零。
	FMinimalViewInfo FinalPOV = PlayerCameraManager->GetCameraCacheView();
	FinalPOV.OffCenterProjectionOffset.X = CurrentStageProjectionOffsetX;
	PlayerCameraManager->SetCameraCachePOV(FinalPOV);
}

void AConfigShowroomPlayerController::UpdateStageProjectionOffset(
	const float DeltaSeconds)
{
	const bool bStageAwareProjectionEnabled =
		CVarStageAwareProjection.GetValueOnGameThread() != 0;
	const float TargetOffset = bStageAwareProjectionEnabled
		&& IsValid(ConfiguratorPanel)
		? ConfiguratorPanel->GetStageProjectionOffsetX()
		: 0.0f;

	// UI 显隐时平滑改变构图，避免车体在全屏按钮点击后横向跳变。
	CurrentStageProjectionOffsetX = FMath::FInterpTo(
		CurrentStageProjectionOffsetX,
		TargetOffset,
		DeltaSeconds,
		StageProjectionInterpolationSpeed);
	if (FMath::IsNearlyEqual(CurrentStageProjectionOffsetX, TargetOffset, 0.0001f))
	{
		CurrentStageProjectionOffsetX = TargetOffset;
	}
}

void AConfigShowroomPlayerController::ApplyRuntimeCameraPOV(
	const FMinimalViewInfo& POV)
{
	if (!IsValid(RuntimeCamera))
	{
		return;
	}

	// 只覆盖投影中心，不修改 Location、Rotation、FOV 或 OrbitPivot。
	// 所有镜头路径都必须经过这里；关闭 CVar 后该值平滑回到 0，
	// 即恢复改动前的全视口居中投影。
	FMinimalViewInfo StageAwarePOV = POV;
	StageAwarePOV.OffCenterProjectionOffset.X = CurrentStageProjectionOffsetX;
	RuntimeCamera->ApplyCameraPOV(StageAwarePOV);
}

void AConfigShowroomPlayerController::DiscoverCameraPresets()
{
	ShowroomCameras.SetNum(6);
	ShowroomCamerasById.Reset();
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		ACameraActor* Camera = *It;
		for (int32 Index = 0; Index < ShowroomCameras.Num(); ++Index)
		{
			const FName LegacyTag(*FString::Printf(
				TEXT("Configurator.Camera.%d"), Index));
			if (Camera->ActorHasTag(LegacyTag))
			{
				ShowroomCameras[Index] = Camera;
			}
		}
		for (const FName Tag : Camera->Tags)
		{
			FString CameraId;
			if (TryParseCameraIdTag(Tag, CameraId)
				&& !CameraId.IsNumeric()
				&& !ShowroomCamerasById.Contains(CameraId))
			{
				ShowroomCamerasById.Add(CameraId, Camera);
			}
		}
	}
}

FString AConfigShowroomPlayerController::FindSemanticCameraId(
	const ACameraActor* Camera) const
{
	if (!IsValid(Camera))
	{
		return FString();
	}
	for (const TPair<FString, TObjectPtr<ACameraActor>>& Pair : ShowroomCamerasById)
	{
		if (Pair.Value == Camera)
		{
			return Pair.Key;
		}
	}
	return FString();
}

bool AConfigShowroomPlayerController::IsInteriorCamera(
	const ACameraActor* Camera) const
{
	return IsValid(Camera) && Camera->ActorHasTag(InteriorCameraTag);
}

bool AConfigShowroomPlayerController::IsCurrentCameraInterior() const
{
	return IsInteriorCamera(CurrentCameraPreset);
}

void AConfigShowroomPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	if (GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Game)
	{
		ApplyMainWindowPolicy();
	}

	DiscoverCameraPresets();

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

	RuntimeCamera = GetWorld()->SpawnActor<AConfigRuntimeCameraActor>();
	FMinimalViewInfo InitialCameraPOV;
	if (IsValid(RuntimeCamera) && GetCameraPresetPOV(0, InitialCameraPOV))
	{
		CurrentCameraIndex = 0;
		CurrentCameraPreset = ShowroomCameras[0];
		CurrentCameraId = FindSemanticCameraId(CurrentCameraPreset);
		OrbitPivot = GetVehicleCameraPivot();
		CameraTransitionStartPOV = BuildRevealStartPOV(InitialCameraPOV, OrbitPivot);
		CameraTransitionEndPOV = InitialCameraPOV;
		CameraTransitionPivot = OrbitPivot;
		CameraTransitionElapsed = 0.0f;
		CameraTransitionDuration = 2.0f;
		ApplyRuntimeCameraPOV(CameraTransitionStartPOV);
		SetViewTarget(RuntimeCamera);
		bInitialRevealPending = true;
		GetWorldTimerManager().SetTimer(
			InitialRevealTimer,
			this,
			&AConfigShowroomPlayerController::StartInitialCameraReveal,
			1.2f,
			false);
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
			if (UAutomotiveConfigurationState* CatalogState =
				Configurator->GetAutomotiveConfigurationState())
			{
				CatalogState->OnChanged.AddDynamic(
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
			if (UAutomotiveConfigurationState* CatalogState =
				Configurator->GetAutomotiveConfigurationState())
			{
				CatalogState->OnChanged.RemoveDynamic(
					this, &AConfigShowroomPlayerController::HandlePersistentConfigurationChanged);
			}
		}
	}
	if (GetWorldTimerManager().IsTimerActive(PersistenceDebounceTimer))
	{
		FlushPersistentConfiguration();
	}
	GetWorldTimerManager().ClearTimer(InitialRevealTimer);
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
	InputComponent->BindKey(
		EKeys::LeftMouseButton, IE_Pressed, this,
		&AConfigShowroomPlayerController::HandleOrbitPressed);
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
	return SwitchToCamera(
		ShowroomCameras[CameraIndex],
		CameraIndex,
		FindSemanticCameraId(ShowroomCameras[CameraIndex]));
}

bool AConfigShowroomPlayerController::SetCameraId(const FString& CameraId)
{
	const TObjectPtr<ACameraActor>* Camera = ShowroomCamerasById.Find(CameraId);
	if (Camera == nullptr || !IsValid(Camera->Get()))
	{
		return false;
	}
	return SwitchToCamera(
		Camera->Get(),
		ShowroomCameras.IndexOfByKey(Camera->Get()),
		CameraId);
}

bool AConfigShowroomPlayerController::SwitchToCamera(
	ACameraActor* Camera,
	const int32 LegacyCameraIndex,
	const FString& CameraId)
{
	if (!IsValid(Camera))
	{
		return false;
	}
	const bool bSameCamera = Camera == CurrentCameraPreset;
	if ((bInitialRevealPending || bInitialRevealActive)
		&& bSameCamera)
	{
		CancelInitialCameraReveal(true);
		return true;
	}
	if (IsValid(PendingCameraPreset) && bSameCamera)
	{
		GetWorldTimerManager().ClearTimer(CameraZoneTransitionTimer);
		PendingCameraPreset = nullptr;
		PendingCameraIndex = INDEX_NONE;
		PendingCameraId.Reset();
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->StartCameraFade(
				1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
		}
		return true;
	}
	if (bSameCamera
		&& !IsValid(PendingCameraPreset)
		&& IsValid(RuntimeCamera)
		&& GetViewTarget() == RuntimeCamera)
	{
		FMinimalViewInfo PresetPOV;
		if (!GetCameraPresetPOV(Camera, PresetPOV))
		{
			return false;
		}
		const FMinimalViewInfo CurrentPOV = RuntimeCamera->GetCameraPOV();
		const bool bAlreadyAtPreset =
			!bInteractiveSmoothingActive
			&& CurrentPOV.Location.Equals(PresetPOV.Location, 0.05)
			&& CurrentPOV.Rotation.Equals(PresetPOV.Rotation, 0.02)
			&& FMath::IsNearlyEqual(CurrentPOV.FOV, PresetPOV.FOV, 0.01f);
		if (bAlreadyAtPreset)
		{
			return true;
		}
		if (IsInteriorCamera(Camera))
		{
			ApplyRuntimeCameraPOV(PresetPOV);
			ResetInteractiveOrbit(PresetPOV, false);
			return true;
		}
	}

	const bool bReplacingPendingBlackTransition =
		IsValid(PendingCameraPreset) && !bSameCamera;
	GetWorldTimerManager().ClearTimer(InitialRevealTimer);
	GetWorldTimerManager().ClearTimer(InitialRevealCompletionTimer);
	bInitialRevealPending = false;
	bInitialRevealActive = false;
	bOrbitTransitionActive = false;
	bInteractiveSmoothingActive = false;
	GetWorldTimerManager().ClearTimer(CameraZoneTransitionTimer);
	PendingCameraPreset = nullptr;
	PendingCameraIndex = INDEX_NONE;
	PendingCameraId.Reset();

	if (!IsValid(RuntimeCamera))
	{
		RuntimeCamera = GetWorld()->SpawnActor<AConfigRuntimeCameraActor>();
	}
	if (!IsValid(RuntimeCamera) || PlayerCameraManager == nullptr)
	{
		return false;
	}

	if (ShouldUseBlackCameraTransition(
		IsCurrentCameraInterior(),
		IsInteriorCamera(Camera),
		bSameCamera))
	{
		PendingCameraPreset = Camera;
		PendingCameraIndex = LegacyCameraIndex;
		PendingCameraId = CameraId;
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

	if (!GetCameraPresetPOV(Camera, CameraTransitionEndPOV))
	{
		return false;
	}
	CameraTransitionStartPOV = PlayerCameraManager->GetCameraCacheView();
	OrbitPivot = GetVehicleCameraPivot();
	CameraTransitionPivot = OrbitPivot;
	CameraTransitionElapsed = 0.0f;
	CameraTransitionDuration = 0.85f;
	CurrentCameraPreset = Camera;
	CurrentCameraIndex = LegacyCameraIndex;
	CurrentCameraId = CameraId;
	ApplyRuntimeCameraPOV(CameraTransitionStartPOV);
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
	if (!IsValid(PendingCameraPreset))
	{
		PendingCameraPreset = nullptr;
		PendingCameraIndex = INDEX_NONE;
		PendingCameraId.Reset();
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->StartCameraFade(
				1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
		}
		return;
	}
	FMinimalViewInfo PresetPOV;
	if (!IsValid(RuntimeCamera) || !GetCameraPresetPOV(PendingCameraPreset, PresetPOV))
	{
		PendingCameraPreset = nullptr;
		PendingCameraIndex = INDEX_NONE;
		PendingCameraId.Reset();
		if (PlayerCameraManager != nullptr)
		{
			PlayerCameraManager->StartCameraFade(
				1.0f, 0.0f, 0.22f, FLinearColor::Black, false, false);
		}
		return;
	}
	CurrentCameraPreset = PendingCameraPreset;
	CurrentCameraIndex = PendingCameraIndex;
	CurrentCameraId = PendingCameraId;
	PendingCameraPreset = nullptr;
	PendingCameraIndex = INDEX_NONE;
	PendingCameraId.Reset();
	ApplyRuntimeCameraPOV(PresetPOV);
	ResetInteractiveOrbit(PresetPOV, !IsCurrentCameraInterior());
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
		|| !IsValid(ShowroomCameras[CameraIndex]))
	{
		return false;
	}
	return GetCameraPresetPOV(ShowroomCameras[CameraIndex], OutPOV);
}

bool AConfigShowroomPlayerController::GetCameraPresetPOV(
	const ACameraActor* Camera,
	FMinimalViewInfo& OutPOV) const
{
	if (!IsValid(Camera) || !IsValid(Camera->GetCameraComponent()))
	{
		return false;
	}
	Camera->GetCameraComponent()->GetCameraView(0.0f, OutPOV);
	return true;
}

AConfigRuntimeCameraActor* AConfigShowroomPlayerController::GetInteractiveCamera() const
{
	if (bInitialRevealPending
		|| bOrbitTransitionActive
		|| IsValid(PendingCameraPreset)
		|| !IsValid(CurrentCameraPreset)
		|| !IsValid(RuntimeCamera)
		|| GetViewTarget() != RuntimeCamera)
	{
		return nullptr;
	}
	return RuntimeCamera;
}

void AConfigShowroomPlayerController::StartInitialCameraReveal()
{
	bInitialRevealPending = false;
	if (!IsValid(RuntimeCamera)
		|| !ShowroomCameras.IsValidIndex(0)
		|| CurrentCameraPreset != ShowroomCameras[0])
	{
		return;
	}
	FMinimalViewInfo LatestPresetPOV;
	if (!GetCameraPresetPOV(CurrentCameraPreset, LatestPresetPOV))
	{
		return;
	}
	CameraTransitionEndPOV = LatestPresetPOV;
	CameraTransitionElapsed = 0.0f;
	CameraTransitionDuration = 2.0f;
	bInitialRevealActive = true;
	bOrbitTransitionActive = true;
	GetWorldTimerManager().SetTimer(
		InitialRevealCompletionTimer,
		this,
		&AConfigShowroomPlayerController::FinishInitialCameraReveal,
		CameraTransitionDuration + 0.05f,
		false);
}

void AConfigShowroomPlayerController::FinishInitialCameraReveal()
{
	GetWorldTimerManager().ClearTimer(InitialRevealCompletionTimer);
	if (!bInitialRevealActive || !IsValid(RuntimeCamera))
	{
		return;
	}
	FMinimalViewInfo LatestPresetPOV;
	if (GetCameraPresetPOV(CurrentCameraPreset, LatestPresetPOV))
	{
		CameraTransitionEndPOV = LatestPresetPOV;
		ApplyRuntimeCameraPOV(CameraTransitionEndPOV);
		ResetInteractiveOrbit(
			CameraTransitionEndPOV,
			!IsCurrentCameraInterior());
	}
	bInitialRevealActive = false;
	bOrbitTransitionActive = false;
}

void AConfigShowroomPlayerController::CancelInitialCameraReveal(
	const bool bSnapToPreset)
{
	GetWorldTimerManager().ClearTimer(InitialRevealTimer);
	GetWorldTimerManager().ClearTimer(InitialRevealCompletionTimer);
	bInitialRevealPending = false;
	bInitialRevealActive = false;
	bOrbitTransitionActive = false;
	if (!bSnapToPreset || !IsValid(RuntimeCamera))
	{
		return;
	}
	FMinimalViewInfo PresetPOV;
	if (GetCameraPresetPOV(CurrentCameraIndex, PresetPOV))
	{
		ApplyRuntimeCameraPOV(PresetPOV);
		ResetInteractiveOrbit(PresetPOV, !IsCurrentCameraInterior());
	}
}

void AConfigShowroomPlayerController::SetInteractiveTargetPOV(
	const FMinimalViewInfo& POV)
{
	InteractiveTargetPOV = POV;
	bInteractiveSmoothingActive = true;
}

void AConfigShowroomPlayerController::ResetInteractiveOrbit(
	const FMinimalViewInfo& POV,
	const bool bResetPivot)
{
	InteractiveTargetPOV = POV;
	bInteractiveSmoothingActive = false;
	if (bResetPivot)
	{
		OrbitPivot = CalculateViewAlignedOrbitPivot(POV, GetVehicleCameraPivot());
	}
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

	FMinimalViewInfo POV = bInteractiveSmoothingActive
		? InteractiveTargetPOV
		: Camera->GetCameraPOV();
	if (IsCurrentCameraInterior())
	{
		POV.Rotation.Yaw = FRotator::NormalizeAxis(POV.Rotation.Yaw + YawDegrees);
		POV.Rotation.Pitch = FMath::Clamp(
			POV.Rotation.Pitch + PitchDegrees, -80.0f, 80.0f);
		POV.Rotation.Roll = 0.0f;
		SetInteractiveTargetPOV(POV);
		return;
	}

	const FVector Location = RotateExteriorCameraLocation(
		POV.Location,
		OrbitPivot,
		YawDegrees,
		PitchDegrees);
	POV.Location = Location;
	POV.Rotation = (OrbitPivot - Location).Rotation();
	SetInteractiveTargetPOV(POV);
}

void AConfigShowroomPlayerController::PanInteractiveCamera(
	const float Horizontal,
	const float Vertical)
{
	AConfigRuntimeCameraActor* Camera = GetInteractiveCamera();
	if (!IsValid(Camera) || IsCurrentCameraInterior())
	{
		return;
	}
	FMinimalViewInfo POV = bInteractiveSmoothingActive
		? InteractiveTargetPOV
		: Camera->GetCameraPOV();
	const FVector PanDelta = CalculateOrbitPanDelta(
		POV.Rotation, Horizontal, Vertical);
	POV.Location += PanDelta;
	OrbitPivot += PanDelta;
	SetInteractiveTargetPOV(POV);
}

void AConfigShowroomPlayerController::DollyInteractiveCamera(const float Amount)
{
	AConfigRuntimeCameraActor* Camera = GetInteractiveCamera();
	if (!IsValid(Camera))
	{
		return;
	}

	FMinimalViewInfo POV = bInteractiveSmoothingActive
		? InteractiveTargetPOV
		: Camera->GetCameraPOV();
	const FVector Candidate =
		POV.Location + POV.Rotation.RotateVector(FVector::ForwardVector) * Amount;
	if (IsCurrentCameraInterior())
	{
		FMinimalViewInfo PresetPOV;
		if (GetCameraPresetPOV(CurrentCameraPreset, PresetPOV))
		{
			POV.Location = ClampInteriorCameraLocation(
				PresetPOV.Location, Candidate, 120.0f);
			SetInteractiveTargetPOV(POV);
		}
		return;
	}

	const float CandidateRadius = FVector::Distance(Candidate, OrbitPivot);
	if (CandidateRadius >= 180.0f && CandidateRadius <= 3000.0f)
	{
		POV.Location = Candidate;
		SetInteractiveTargetPOV(POV);
	}
}

void AConfigShowroomPlayerController::HandleCameraHorizontal(const float Value)
{
	if (FMath::IsNearlyZero(Value))
	{
		return;
	}
	if (bInitialRevealPending || bInitialRevealActive)
	{
		CancelInitialCameraReveal(true);
	}
	if (IsInputKeyDown(EKeys::LeftMouseButton))
	{
		RotateInteractiveCamera(FMath::Clamp(Value, -12.0f, 12.0f) * 0.35f, 0.0f);
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
	if (bInitialRevealPending || bInitialRevealActive)
	{
		CancelInitialCameraReveal(true);
	}
	if (IsInputKeyDown(EKeys::LeftMouseButton))
	{
		RotateInteractiveCamera(
			0.0f, -FMath::Clamp(Value, -12.0f, 12.0f) * 0.35f);
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
		if (bInitialRevealPending || bInitialRevealActive)
		{
			CancelInitialCameraReveal(true);
		}
		DollyInteractiveCamera(Value * 35.0f);
	}
}

void AConfigShowroomPlayerController::HandleOrbitPressed()
{
	if (bInitialRevealPending || bInitialRevealActive)
	{
		CancelInitialCameraReveal(true);
	}
	AConfigRuntimeCameraActor* Camera = GetInteractiveCamera();
	if (!IsValid(Camera))
	{
		return;
	}

	// 以当前画面为交互起点。ResetInteractiveOrbit 已建立视线对齐的
	// 初始 Pivot；后续右键平移会
	// 同步移动该 Pivot，因此这里不能再次使用车辆中心覆盖它。
	InteractiveTargetPOV = Camera->GetCameraPOV();
	bInteractiveSmoothingActive = false;
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
	return IsValid(Vehicle)
		&& (Vehicle->IsVehicleAnimationPlaying()
			|| Vehicle->IsWheelAnimationEnabled());
}

bool AConfigShowroomPlayerController::PlayAnimation(const FName AnimationId)
{
	return IsValid(Vehicle) && Vehicle->PlayVehicleAnimation(AnimationId);
}

bool AConfigShowroomPlayerController::CloseAnimation(const FName AnimationId)
{
	return IsValid(Vehicle) && Vehicle->CloseVehicleAnimation(AnimationId);
}

bool AConfigShowroomPlayerController::FocusAnimation(const FName NextAnimationId)
{
	return IsValid(Vehicle) && Vehicle->FocusVehicleAnimation(NextAnimationId);
}

bool AConfigShowroomPlayerController::CanPlayAnimation(
	const FName AnimationId) const
{
	return IsValid(Vehicle) && Vehicle->CanPlayVehicleAnimation(AnimationId);
}

FName AConfigShowroomPlayerController::GetActiveAnimationId() const
{
	return IsValid(Vehicle) ? Vehicle->GetActiveVehicleAnimationId() : NAME_None;
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
		// Path Tracing 从本帧开始累积，不能等待反播或平滑减速结束。
		Vehicle->FreezeAllVehicleMotion();
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
	const FName ActiveAnimationId = GetActiveAnimationId();
	if (!ActiveAnimationId.IsNone())
	{
		CloseAnimation(ActiveAnimationId);
	}
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
	if (UAutomotiveConfigurationState* CatalogState =
		Configurator->GetAutomotiveConfigurationState())
	{
		Snapshot->bHasAutomotiveState = CatalogState->IsInitialized();
		Snapshot->AutomotiveSelections = CatalogState->GetSelections();
		Snapshot->AutomotiveCustomizations = CatalogState->GetCustomizations();
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
	UAutomotiveConfigurationState* CatalogState = IsValid(Configurator)
		? Configurator->GetAutomotiveConfigurationState()
		: nullptr;
	if (!IsValid(Snapshot) || !IsValid(Configurator)
		|| !IsValid(Environment) || !IsValid(ConfiguratorPanel)
		|| Snapshot->EnvironmentIndex < 0 || Snapshot->EnvironmentIndex > 1
		|| !Configurator->CanApplySelection(Snapshot->Configuration)
		|| (Snapshot->bHasAutomotiveState
			&& (!IsValid(CatalogState)
				|| !CatalogState->CanApplyTransaction(
					Snapshot->AutomotiveSelections,
					Snapshot->AutomotiveCustomizations))))
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
	if (Snapshot->bHasAutomotiveState
		&& !CatalogState->ApplyTransaction(
			Snapshot->AutomotiveSelections,
			Snapshot->AutomotiveCustomizations))
	{
		bApplyingLoadedSnapshot = false;
		OutError = TEXT("车型目录 v2 存档配置应用失败。");
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
