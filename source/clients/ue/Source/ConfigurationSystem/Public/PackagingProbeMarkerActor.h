#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PackagingProbeMarkerActor.generated.h"

class UStaticMeshComponent;

/**
 * 打包边界探针地图中的 Runtime 标记。
 * 构造函数硬引用 Engine 基础 Cube，用于确认地图硬依赖在 Cook 后仍然完整。
 */
UCLASS()
class CONFIGURATIONSYSTEM_API APackagingProbeMarkerActor final : public AActor
{
	GENERATED_BODY()

public:
	APackagingProbeMarkerActor();

	/** 固定哨兵值；运行时探针要求地图中恰好有一个值为 1 的标记。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Packaging Probe")
	int32 Marker = 1;

	/** 默认显示并硬引用 /Engine/BasicShapes/Cube。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Packaging Probe")
	TObjectPtr<UStaticMeshComponent> CubeComponent;
};
