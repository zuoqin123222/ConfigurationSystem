#include "ConfigShowroomGameMode.h"

#include "ConfigShowroomPlayerController.h"

AConfigShowroomGameMode::AConfigShowroomGameMode()
{
	PlayerControllerClass = AConfigShowroomPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}
