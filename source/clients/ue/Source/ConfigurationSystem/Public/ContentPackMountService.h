#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetManagerTypes.h"

struct FContentPackManifest
{
	FString SchemaVersion;
	FString PackId;
	FString Version;
	FString CatalogVersion;
	FString ProviderType;
	FString EngineVersion;
	FString Platform;
	FString MountPoint;
	FString PakFileName;
	int64 PakBytes = 0;
	FString PakSha256;
	TArray<FPrimaryAssetId> PrimaryAssetIds;
};

struct FContentPackMountPolicy
{
	FString CatalogVersion;
	FString EngineVersion;
	FString Platform = TEXT("Win64");
	TArray<FString> AllowedMountRoots{TEXT("/Game/ContentPacks/")};
	int64 MaximumPakBytes = 10LL * 1024LL * 1024LL * 1024LL;
};

struct FContentPackMountResult
{
	bool bPreflightPassed = false;
	bool bMounted = false;
	FContentPackManifest Manifest;
	FString ActualPakSha256;
	TArray<FString> Errors;
};

/**
 * Runtime-only content pack gate. It consumes a generic .pak plus manifest and
 * intentionally has no dependency on HotPatcher APIs.
 */
class CONFIGURATIONSYSTEM_API FContentPackMountService
{
public:
	using FMountPak = TFunction<bool(const FString& PakPath, int32 PakOrder)>;
	using FInspectPak = TFunction<bool(
		const FString& PakPath,
		FString& OutEmbeddedMountPoint,
		FString& OutError)>;

	explicit FContentPackMountService(FContentPackMountPolicy InPolicy);

	FContentPackMountResult Preflight(
		const FString& ManifestPath,
		const FString& PakPath) const;

	FContentPackMountResult PreflightAndMount(
		const FString& ManifestPath,
		const FString& PakPath,
		int32 PakOrder = 0);

	void SetMountPakForTesting(FMountPak InMountPak);
	void SetInspectPakForTesting(FInspectPak InInspectPak);
	const TSet<FPrimaryAssetId>& GetMountedPrimaryAssetIds() const;

	static bool ComputeFileSha256(
		const FString& Filename,
		FString& OutSha256,
		FString& OutError);

private:
	FContentPackMountPolicy Policy;
	FMountPak MountPak;
	FInspectPak InspectPak;
	TSet<FPrimaryAssetId> MountedPrimaryAssetIds;
};
