#include "VehicleSurfaceBindingAudit.h"

#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace VehicleSurfaceBinding
{
	constexpr int32 Sc01SurfaceCount = 38;

	const TArray<FString>& Sc01SurfaceIds()
	{
		static const TArray<FString> SurfaceIds{
			TEXT("exterior-body-cover"), TEXT("engine-bay-cover"),
			TEXT("wheel-material"), TEXT("wheel-style"), TEXT("wheel-color"),
			TEXT("front-caliper-color"), TEXT("rear-caliper-color"),
			TEXT("steering-wheel-skin"), TEXT("steering-wheel-addon"),
			TEXT("steering-center-mark"), TEXT("ip-wings"), TEXT("ip-middle"),
			TEXT("ip-instrument-cover"), TEXT("ip-upper-trim"),
			TEXT("ip-lower-trim"), TEXT("ip-center-mark"),
			TEXT("a-pillar-surface"), TEXT("seat-backrest"), TEXT("seat-bolster"),
			TEXT("seat-shell-back"), TEXT("seat-headrest-mark"),
			TEXT("door-upper"), TEXT("door-middle"), TEXT("door-armrest"),
			TEXT("door-armrest-skin"), TEXT("storage-soft-bag"),
			TEXT("console-armrest-cover"), TEXT("console-armrest-side"),
			TEXT("handbrake"), TEXT("roof-surface"), TEXT("lower-skirt"),
			TEXT("interior-painted-parts"), TEXT("door-sill"),
			TEXT("embroidered-logo"), TEXT("center-panel-trim"),
			TEXT("shift-knob"), TEXT("brake-handle"), TEXT("pedal")
		};
		return SurfaceIds;
	}

	void AddMissingAndDuplicateIssues(
		const TArray<FName>& Expected,
		const TArray<FName>& Actual,
		const FString& Context,
		const bool bRejectDuplicates,
		TArray<FString>& Issues)
	{
		TMap<FName, int32> Counts;
		for (const FName Slot : Actual)
		{
			Counts.FindOrAdd(Slot)++;
		}
		for (const FName Slot : Expected)
		{
			const int32 Count = Counts.FindRef(Slot);
			if (Count == 0)
			{
				Issues.Add(FString::Printf(
					TEXT("%s 缺少 surface material slot：%s。"),
					*Context,
					*Slot.ToString()));
			}
			else if (bRejectDuplicates && Count > 1)
			{
				Issues.Add(FString::Printf(
					TEXT("%s material slot 重复：%s（%d 次）。"),
					*Context,
					*Slot.ToString(),
					Count));
			}
		}
	}
}

bool FVehicleSurfaceBindingAudit::LoadContractFile(
	const FString& Filename,
	FVehicleSurfaceBindingContract& OutContract,
	TArray<FString>& OutErrors)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Filename))
	{
		OutErrors.Add(FString::Printf(TEXT("无法读取 surface-binding 契约：%s"), *Filename));
		return false;
	}
	return LoadContractJson(JsonText, OutContract, OutErrors);
}

bool FVehicleSurfaceBindingAudit::LoadContractJson(
	const FString& JsonText,
	FVehicleSurfaceBindingContract& OutContract,
	TArray<FString>& OutErrors)
{
	OutContract = {};
	OutErrors.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(FString::Printf(
			TEXT("surface-binding JSON 无效：%s"),
			*Reader->GetErrorMessage()));
		return false;
	}

	FString SchemaVersion;
	FString Kind;
	if (!Root->TryGetStringField(TEXT("schemaVersion"), SchemaVersion)
		|| SchemaVersion != TEXT("1.0.0"))
	{
		OutErrors.Add(TEXT("$.schemaVersion 必须为 1.0.0。"));
	}
	if (!Root->TryGetStringField(TEXT("kind"), Kind)
		|| Kind != TEXT("vehicle-surface-binding"))
	{
		OutErrors.Add(TEXT("$.kind 必须为 vehicle-surface-binding。"));
	}
	if (!Root->TryGetStringField(TEXT("vehicleId"), OutContract.VehicleId)
		|| OutContract.VehicleId.IsEmpty())
	{
		OutErrors.Add(TEXT("$.vehicleId 必须是非空字符串。"));
	}
	if (!Root->TryGetStringField(TEXT("catalogVersion"), OutContract.CatalogVersion)
		|| OutContract.CatalogVersion.IsEmpty())
	{
		OutErrors.Add(TEXT("$.catalogVersion 必须是非空字符串。"));
	}
	if (!Root->TryGetStringField(TEXT("modelVersion"), OutContract.ModelVersion)
		|| OutContract.ModelVersion.IsEmpty())
	{
		OutErrors.Add(TEXT("$.modelVersion 必须是非空字符串。"));
	}

	const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
	if (!Root->TryGetArrayField(TEXT("bindings"), Bindings) || Bindings == nullptr
		|| Bindings->Num() != VehicleSurfaceBinding::Sc01SurfaceCount)
	{
		OutErrors.Add(FString::Printf(
			TEXT("$.bindings 必须包含 SC01 的 %d 个 surface。"),
			VehicleSurfaceBinding::Sc01SurfaceCount));
		return false;
	}

	TSet<FString> SeenSurfaces;
	TSet<FName> SeenSlots;
	for (int32 Index = 0; Index < Bindings->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject> Binding =
			(*Bindings)[Index].IsValid() ? (*Bindings)[Index]->AsObject() : nullptr;
		if (!Binding.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("$.bindings[%d] 必须是对象。"), Index));
			continue;
		}
		FString SurfaceId;
		FString SlotId;
		if (!Binding->TryGetStringField(TEXT("surfaceId"), SurfaceId) || SurfaceId.IsEmpty())
		{
			OutErrors.Add(FString::Printf(
				TEXT("$.bindings[%d].surfaceId 必须非空。"), Index));
		}
		else if (SeenSurfaces.Contains(SurfaceId))
		{
			OutErrors.Add(FString::Printf(
				TEXT("$.bindings[%d].surfaceId 重复：%s。"), Index, *SurfaceId));
		}
		else
		{
			SeenSurfaces.Add(SurfaceId);
			OutContract.SurfaceIds.Add(SurfaceId);
			if (SurfaceId != VehicleSurfaceBinding::Sc01SurfaceIds()[Index])
			{
				OutErrors.Add(FString::Printf(
					TEXT("$.bindings[%d].surfaceId 必须按 SC01 selectionOrder 为 %s，实际为 %s。"),
					Index,
					*VehicleSurfaceBinding::Sc01SurfaceIds()[Index],
					*SurfaceId));
			}
		}

		if (!Binding->TryGetStringField(TEXT("materialSlotId"), SlotId)
			|| !SlotId.StartsWith(TEXT("sc01_")))
		{
			OutErrors.Add(FString::Printf(
				TEXT("$.bindings[%d].materialSlotId 必须是 sc01_ 前缀的稳定槽名。"), Index));
			continue;
		}
		const FName SlotName(*SlotId);
		if (SeenSlots.Contains(SlotName))
		{
			OutErrors.Add(FString::Printf(
				TEXT("$.bindings[%d].materialSlotId 重复：%s。"), Index, *SlotId));
		}
		else
		{
			SeenSlots.Add(SlotName);
			OutContract.MaterialSlotIds.Add(SlotName);
		}
	}
	return OutErrors.IsEmpty();
}

FVehicleSurfaceBindingAuditResult FVehicleSurfaceBindingAudit::AuditSnapshot(
	const FVehicleSurfaceBindingContract& Contract,
	const FVehicleSurfaceBindingMeshSnapshot& Snapshot)
{
	FVehicleSurfaceBindingAuditResult Result;
	Result.SurfaceCount = Contract.MaterialSlotIds.Num();
	Result.LodCount = Snapshot.LodMaterialSlotIds.Num();
	if (Contract.MaterialSlotIds.Num() != VehicleSurfaceBinding::Sc01SurfaceCount)
	{
		Result.Issues.Add(FString::Printf(
			TEXT("surface-binding 必须包含 %d 个唯一槽，实际为 %d。"),
			VehicleSurfaceBinding::Sc01SurfaceCount,
			Contract.MaterialSlotIds.Num()));
	}
	VehicleSurfaceBinding::AddMissingAndDuplicateIssues(
		Contract.MaterialSlotIds,
		Snapshot.MaterialSlotIds,
		TEXT("SkeletalMesh"),
		true,
		Result.Issues);
	if (Snapshot.LodMaterialSlotIds.IsEmpty())
	{
		Result.Issues.Add(TEXT("SkeletalMesh 没有可审计的 LOD。"));
	}
	for (int32 LodIndex = 0; LodIndex < Snapshot.LodMaterialSlotIds.Num(); ++LodIndex)
	{
		VehicleSurfaceBinding::AddMissingAndDuplicateIssues(
			Contract.MaterialSlotIds,
			Snapshot.LodMaterialSlotIds[LodIndex],
			FString::Printf(TEXT("LOD%d"), LodIndex),
			false,
			Result.Issues);
	}
	Result.bPassed = Result.Issues.IsEmpty();
	return Result;
}

FVehicleSurfaceBindingAuditResult FVehicleSurfaceBindingAudit::AuditSkeletalMesh(
	const FVehicleSurfaceBindingContract& Contract,
	const USkeletalMesh* Mesh)
{
	if (Mesh == nullptr)
	{
		FVehicleSurfaceBindingAuditResult Result;
		Result.Issues.Add(TEXT("待审计 SkeletalMesh 为空。"));
		return Result;
	}

	FVehicleSurfaceBindingMeshSnapshot Snapshot;
	for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
	{
		Snapshot.MaterialSlotIds.Add(Material.MaterialSlotName);
	}
	const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	if (RenderData != nullptr)
	{
		for (const FSkeletalMeshLODRenderData& Lod : RenderData->LODRenderData)
		{
			TArray<FName>& LodSlots = Snapshot.LodMaterialSlotIds.AddDefaulted_GetRef();
			for (const FSkelMeshRenderSection& Section : Lod.RenderSections)
			{
				if (Snapshot.MaterialSlotIds.IsValidIndex(Section.MaterialIndex))
				{
					LodSlots.Add(Snapshot.MaterialSlotIds[Section.MaterialIndex]);
				}
			}
		}
	}
	return AuditSnapshot(Contract, Snapshot);
}
