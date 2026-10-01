#include "PackagingProbeMarkerActor.h"

#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

APackagingProbeMarkerActor::APackagingProbeMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;
	CubeComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cube"));
	SetRootComponent(CubeComponent);

	// C++ 构造器硬引用保证 Cube 成为类默认对象的直接依赖，而不是运行时软加载。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		CubeComponent->SetStaticMesh(CubeMesh.Object);
	}
}
