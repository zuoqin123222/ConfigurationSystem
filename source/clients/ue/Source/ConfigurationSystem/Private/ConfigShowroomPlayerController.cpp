#include "ConfigShowroomPlayerController.h"

#include "Camera/CameraActor.h"
#include "ConfiguratorPanel.h"
#include "Blueprint/UserWidget.h"
#include "EngineUtils.h"

void AConfigShowroomPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("Configurator.ShowroomCamera")))
		{
			SetViewTarget(*It);
			break;
		}
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
}
