#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShowroomEnvironmentActor.generated.h"

class UDirectionalLightComponent;
class USceneComponent;
class USkyLightComponent;

/** 两套纯 C++ 展厅灯光环境；几何/灯光方案均标记为 UE 体验阶段临时方案。 */
UCLASS()
class CONFIGURATIONSYSTEM_API AShowroomEnvironmentActor final : public AActor
{
	GENERATED_BODY()

public:
	AShowroomEnvironmentActor();

	UFUNCTION(BlueprintCallable, Category="Configurator|Environment")
	void SetEnvironmentIndex(int32 InIndex);

	UFUNCTION(BlueprintCallable, Category="Configurator|Environment")
	void ToggleEnvironment();

	UFUNCTION(BlueprintPure, Category="Configurator|Environment")
	int32 GetEnvironmentIndex() const { return EnvironmentIndex; }

	static const FName EnvironmentActorTag;
	static const FName TemporaryEnvironmentTag;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> StudioKey;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> StudioSky;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> OutdoorSun;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> OutdoorSky;

	UPROPERTY(VisibleInstanceOnly)
	int32 EnvironmentIndex = 0;
};
