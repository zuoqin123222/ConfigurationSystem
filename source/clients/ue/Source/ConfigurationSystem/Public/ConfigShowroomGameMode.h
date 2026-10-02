#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ConfigShowroomGameMode.generated.h"

/** 首个展厅原子阶段的无 Pawn GameMode。 */
UCLASS()
class CONFIGURATIONSYSTEM_API AConfigShowroomGameMode final : public AGameModeBase
{
	GENERATED_BODY()

public:
	AConfigShowroomGameMode();
};
