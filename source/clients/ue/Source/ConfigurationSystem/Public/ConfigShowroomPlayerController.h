#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ConfigShowroomPlayerController.generated.h"

class UConfiguratorPanel;

/** 创建纯 C++ 面板并启用鼠标的展厅控制器。 */
UCLASS()
class CONFIGURATIONSYSTEM_API AConfigShowroomPlayerController final
	: public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorPanel> ConfiguratorPanel;
};
