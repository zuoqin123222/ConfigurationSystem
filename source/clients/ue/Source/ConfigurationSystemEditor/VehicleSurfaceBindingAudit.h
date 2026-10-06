#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct FVehicleSurfaceBindingContract
{
	FString VehicleId;
	FString CatalogVersion;
	FString ModelVersion;
	TArray<FString> SurfaceIds;
	TArray<FName> MaterialSlotIds;
};

struct FVehicleSurfaceBindingMeshSnapshot
{
	TArray<FName> MaterialSlotIds;
	TArray<TArray<FName>> LodMaterialSlotIds;
};

struct FVehicleSurfaceBindingAuditResult
{
	bool bPassed = false;
	int32 SurfaceCount = 0;
	int32 LodCount = 0;
	TArray<FString> Issues;
};

/** Editor-only parser and post-import slot/LOD gate for vehicle surface bindings. */
class FVehicleSurfaceBindingAudit
{
public:
	static bool LoadContractFile(
		const FString& Filename,
		FVehicleSurfaceBindingContract& OutContract,
		TArray<FString>& OutErrors);

	static bool LoadContractJson(
		const FString& JsonText,
		FVehicleSurfaceBindingContract& OutContract,
		TArray<FString>& OutErrors);

	static FVehicleSurfaceBindingAuditResult AuditSnapshot(
		const FVehicleSurfaceBindingContract& Contract,
		const FVehicleSurfaceBindingMeshSnapshot& Snapshot);

	static FVehicleSurfaceBindingAuditResult AuditSkeletalMesh(
		const FVehicleSurfaceBindingContract& Contract,
		const USkeletalMesh* Mesh);
};
