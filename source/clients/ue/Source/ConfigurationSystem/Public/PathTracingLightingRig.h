#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PathTracingLightingRig.generated.h"

class UDirectionalLightComponent;
class UPointLightComponent;
class URectLightComponent;
class USceneComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UTextureCube;

/** PT 专用灯光参数；不依赖地图资产，便于运行时切换和自动化验证。 */
struct CONFIGURATIONSYSTEM_API FPathTracingLightingPreset
{
	FString CubemapPath;
	float SkyIntensity = 1.0f;
	float DirectionalIntensity = 50000.0f;
	float PointIntensity = 2500.0f;
	float RectIntensity = 8000.0f;
	FRotator DirectionalRotation = FRotator::ZeroRotator;

	static FPathTracingLightingPreset ForEnvironment(int32 EnvironmentIndex);
};

/**
 * 仅在 Path Tracing 生效期间存在的瞬态灯光 Rig。
 * Actor 销毁时 SkyLight、补光和手动曝光一并移除，因此 Lit 不会被改写。
 */
UCLASS(NotBlueprintable, Transient)
class CONFIGURATIONSYSTEM_API APathTracingLightingRig final : public AActor
{
	GENERATED_BODY()

public:
	APathTracingLightingRig();

	bool ApplyEnvironment(int32 EnvironmentIndex);
	int32 GetEnvironmentIndex() const { return EnvironmentIndex; }

	USkyLightComponent* GetSkyLightComponent() const { return SkyLight; }
	UDirectionalLightComponent* GetDirectionalLightComponent() const { return DirectionalLight; }
	UPointLightComponent* GetPointLightComponent() const { return PointLight; }
	URectLightComponent* GetRectLightComponent() const { return RectLight; }
	UStaticMeshComponent* GetFloorComponent() const { return Floor; }
	static const FName RigActorTag;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> PointLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<URectLightComponent> RectLight;

	/** 仅随 PT Rig 存续的 18m × 18m 薄地板，顶面位于世界 Z=0。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Floor;

	int32 EnvironmentIndex = 0;
};
