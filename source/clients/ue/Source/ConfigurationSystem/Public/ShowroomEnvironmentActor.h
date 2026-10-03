#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShowroomEnvironmentActor.generated.h"

/** 主关卡中的无可视环境控制器；两套灯光与地面由独立流式关卡承载。 */
UCLASS()
class CONFIGURATIONSYSTEM_API AShowroomEnvironmentActor final : public AActor
{
	GENERATED_BODY()

public:
	AShowroomEnvironmentActor();
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category="Configurator|Environment")
	void SetEnvironmentIndex(int32 InIndex);

	UFUNCTION(BlueprintCallable, Category="Configurator|Environment")
	void ToggleEnvironment();

	UFUNCTION(BlueprintPure, Category="Configurator|Environment")
	int32 GetEnvironmentIndex() const { return EnvironmentIndex; }

	static const FName EnvironmentActorTag;
	static const FName TemporaryEnvironmentTag;
	static const FName StudioLevelName;
	static const FName OutdoorLevelName;

private:
	UPROPERTY(VisibleInstanceOnly)
	int32 EnvironmentIndex = 0;
};
