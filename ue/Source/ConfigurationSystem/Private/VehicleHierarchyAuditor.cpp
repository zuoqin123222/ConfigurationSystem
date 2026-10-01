#include "VehicleHierarchyAuditor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

namespace VehicleHierarchyAudit
{
	const TArray<FName>& PartTags()
	{
		static const TArray<FName> Tags{
			TEXT("Vehicle.Part.Door.FrontLeft"),
			TEXT("Vehicle.Part.Hood"),
			TEXT("Vehicle.Part.Trunk"),
			TEXT("Vehicle.Part.Wheel.FrontLeft"),
			TEXT("Vehicle.Part.Wheel.FrontRight"),
			TEXT("Vehicle.Part.Wheel.RearLeft"),
			TEXT("Vehicle.Part.Wheel.RearRight")
		};
		return Tags;
	}
}

const TArray<FName>& UVehicleHierarchyAuditor::GetRequiredTags()
{
	static const TArray<FName> Tags{
		TEXT("Vehicle.Root"),
		TEXT("Vehicle.Body"),
		TEXT("Vehicle.Part.Door.FrontLeft"),
		TEXT("Vehicle.Part.Hood"),
		TEXT("Vehicle.Part.Trunk"),
		TEXT("Vehicle.Part.Wheel.FrontLeft"),
		TEXT("Vehicle.Part.Wheel.FrontRight"),
		TEXT("Vehicle.Part.Wheel.RearLeft"),
		TEXT("Vehicle.Part.Wheel.RearRight")
	};
	return Tags;
}

FVehicleHierarchyAuditResult UVehicleHierarchyAuditor::AuditActor(const AActor* Actor)
{
	FVehicleHierarchyAuditResult Result;
	if (Actor == nullptr)
	{
		Result.Issues.Add(TEXT("待审计 Actor 为空。"));
		return Result;
	}

	TMap<FName, TArray<const USceneComponent*>> ComponentsByTag;
	for (const FName Tag : GetRequiredTags())
	{
		ComponentsByTag.Add(Tag);
	}

	TInlineComponentArray<USceneComponent*> SceneComponents;
	Actor->GetComponents(SceneComponents);
	for (const USceneComponent* Component : SceneComponents)
	{
		for (const FName RequiredTag : GetRequiredTags())
		{
			// 同一组件重复填写同一标签也属于重复标签。
			int32 Occurrences = 0;
			for (const FName ExistingTag : Component->ComponentTags)
			{
				Occurrences += ExistingTag == RequiredTag ? 1 : 0;
			}
			for (int32 Index = 0; Index < Occurrences; ++Index)
			{
				ComponentsByTag.FindChecked(RequiredTag).Add(Component);
			}
		}
	}

	for (const FName Tag : GetRequiredTags())
	{
		const TArray<const USceneComponent*>& TaggedComponents = ComponentsByTag.FindChecked(Tag);
		Result.TaggedComponentCounts.Add(Tag, TaggedComponents.Num());
		if (TaggedComponents.IsEmpty())
		{
			Result.Issues.Add(FString::Printf(TEXT("缺少必需 ComponentTag：%s。"), *Tag.ToString()));
		}
		else if (TaggedComponents.Num() > 1)
		{
			Result.Issues.Add(FString::Printf(
				TEXT("ComponentTag %s 重复，共 %d 处。"),
				*Tag.ToString(),
				TaggedComponents.Num()));
		}

		for (const USceneComponent* Component : TaggedComponents)
		{
			if (Component->Mobility != EComponentMobility::Movable)
			{
				Result.Issues.Add(FString::Printf(
					TEXT("组件 %s（标签 %s）的 Mobility 不是 Movable。"),
					*Component->GetName(),
					*Tag.ToString()));
			}
		}
	}

	auto UniqueComponent = [&ComponentsByTag](const FName Tag) -> const USceneComponent*
	{
		const TArray<const USceneComponent*>& Components = ComponentsByTag.FindChecked(Tag);
		return Components.Num() == 1 ? Components[0] : nullptr;
	};

	const USceneComponent* Root = UniqueComponent(TEXT("Vehicle.Root"));
	const USceneComponent* Body = UniqueComponent(TEXT("Vehicle.Body"));

	if (Root != nullptr && Actor->GetRootComponent() != Root)
	{
		Result.Issues.Add(TEXT("带 Vehicle.Root 标签的组件不是 Actor 根组件。"));
	}
	if (Root != nullptr && Root->GetAttachParent() != nullptr)
	{
		Result.Issues.Add(TEXT("Vehicle.Root 组件不应存在挂接父组件。"));
	}
	if (Body != nullptr && Root != nullptr && Body->GetAttachParent() != Root)
	{
		Result.Issues.Add(TEXT("Vehicle.Body 必须直接挂接到 Vehicle.Root。"));
	}

	Result.bPartPivotsDirectlyAttachedToBody = Body != nullptr;
	for (const FName PartTag : VehicleHierarchyAudit::PartTags())
	{
		const USceneComponent* PartPivot = UniqueComponent(PartTag);
		if (PartPivot == nullptr || Body == nullptr || PartPivot->GetAttachParent() != Body)
		{
			Result.bPartPivotsDirectlyAttachedToBody = false;
		}
		if (PartPivot != nullptr && Body != nullptr && PartPivot->GetAttachParent() != Body)
		{
			Result.Issues.Add(FString::Printf(
				TEXT("%s Pivot 必须直接挂接到 Vehicle.Body。"),
				*PartTag.ToString()));
		}
	}

	const USceneComponent* DoorPivot = UniqueComponent(TEXT("Vehicle.Part.Door.FrontLeft"));
	const UStaticMeshComponent* DoorMesh = nullptr;
	if (DoorPivot != nullptr)
	{
		for (const USceneComponent* Child : DoorPivot->GetAttachChildren())
		{
			if (Child != nullptr && Child->GetFName() == TEXT("DoorMesh"))
			{
				DoorMesh = Cast<UStaticMeshComponent>(Child);
				break;
			}
		}
	}
	Result.bDoorMeshIsPivotChild = DoorMesh != nullptr && DoorMesh->GetAttachParent() == DoorPivot;
	Result.bDoorMeshHasNoTags = DoorMesh != nullptr && DoorMesh->ComponentTags.IsEmpty();
	Result.bDoorMeshOffsetFromPivot =
		DoorMesh != nullptr && !DoorMesh->GetRelativeLocation().IsNearlyZero();

	if (!Result.bDoorMeshIsPivotChild)
	{
		Result.Issues.Add(TEXT("DoorMesh 必须是前左门 Pivot 的直接 UStaticMeshComponent 子项。"));
	}
	if (!Result.bDoorMeshHasNoTags)
	{
		Result.Issues.Add(TEXT("DoorMesh 必须保持无标签，标签只能位于 DoorPivot。"));
	}
	if (!Result.bDoorMeshOffsetFromPivot)
	{
		Result.Issues.Add(TEXT("DoorMesh 必须相对 DoorPivot 铰链位置存在非零偏移。"));
	}

	Result.bPassed = Result.Issues.IsEmpty();
	return Result;
}
