#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VehicleHierarchyAuditor.generated.h"

class AActor;

/** Runtime 车辆层级审计结果。 */
USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FVehicleHierarchyAuditResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Vehicle|Hierarchy Audit")
	bool bPassed = false;

	UPROPERTY(BlueprintReadOnly, Category="Vehicle|Hierarchy Audit")
	TArray<FString> Issues;

	UPROPERTY(BlueprintReadOnly, Category="Vehicle|Hierarchy Audit")
	TMap<FName, int32> TaggedComponentCounts;

	UPROPERTY(BlueprintReadOnly, Category="Vehicle|Hierarchy Audit")
	bool bPartPivotsDirectlyAttachedToBody = false;

	UPROPERTY(BlueprintReadOnly, Category="Vehicle|Hierarchy Audit")
	bool bDoorMeshIsPivotChild = false;

	UPROPERTY(BlueprintReadOnly, Category="Vehicle|Hierarchy Audit")
	bool bDoorMeshHasNoTags = false;

	UPROPERTY(BlueprintReadOnly, Category="Vehicle|Hierarchy Audit")
	bool bDoorMeshOffsetFromPivot = false;
};

/**
 * 按 USceneComponent::ComponentTags 审计车辆层级。
 *
 * 必需标签遵循 Vehicle.Root / Vehicle.Body / Vehicle.Part.* Schema；每个标签必须唯一，
 * 所有对应组件必须为 Movable，Root 必须是 Actor 根组件，Body 必须直接挂到 Root，
 * 所有部件 Pivot 必须直接挂到 Body。前左门还必须使用无标签 DoorMesh 子项并相对铰链偏移。
 */
UCLASS()
class CONFIGURATIONSYSTEM_API UVehicleHierarchyAuditor final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	static const TArray<FName>& GetRequiredTags();

	UFUNCTION(BlueprintCallable, Category="Vehicle|Hierarchy Audit")
	static FVehicleHierarchyAuditResult AuditActor(const AActor* Actor);
};
