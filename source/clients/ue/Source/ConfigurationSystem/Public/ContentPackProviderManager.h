#pragma once

#include "CoreMinimal.h"
#include "ContentPackMountService.h"

enum class EContentPackProviderType : uint8
{
	Material,
	Environment,
	Vehicle
};

struct FContentPackProviderRecord
{
	EContentPackProviderType Type = EContentPackProviderType::Material;
	FString PackId;
	FString Version;
	FString ManifestPath;
	FString PakPath;
	FString MountPoint;
	TArray<FPrimaryAssetId> PrimaryAssetIds;
};

struct FContentPackProviderSlot
{
	FContentPackProviderRecord Current;
	TOptional<FContentPackProviderRecord> Previous;
};

struct FContentPackProviderManagerPolicy
{
	FString CatalogVersion = TEXT("sc01-draft-20261007");
	FString EngineVersion;
	FString Platform = TEXT("Win64");
	FString RegistryPath;
	TArray<FString> AllowedMountRoots{TEXT("/Game/ContentPacks/")};
	int64 MaximumPakBytes = 10LL * 1024LL * 1024LL * 1024LL;
};

struct FContentPackProviderActivationResult
{
	bool bActivated = false;
	FContentPackMountResult MountResult;
	TArray<FString> Errors;
};

/**
 * Automotive content-provider switcher. A candidate is only published to the
 * in-memory/disk active registry after mount and PrimaryAsset scanning succeed.
 */
class CONFIGURATIONSYSTEM_API FContentPackProviderManager
{
public:
	using FScanMountedProvider = TFunction<bool(
		const FContentPackManifest& Manifest,
		FString& OutError)>;

	explicit FContentPackProviderManager(FContentPackProviderManagerPolicy InPolicy);

	FContentPackMountResult Preflight(
		EContentPackProviderType Type,
		const FString& ManifestPath,
		const FString& PakPath,
		bool* bOutLegacyV1Compatibility = nullptr) const;

	FContentPackProviderActivationResult Activate(
		EContentPackProviderType Type,
		const FString& ManifestPath,
		const FString& PakPath);

	bool RestoreActiveProviders(TArray<FString>& OutErrors);
	bool Rollback(EContentPackProviderType Type, TArray<FString>& OutErrors);

	const FContentPackProviderSlot* FindActive(EContentPackProviderType Type) const;

	void SetMountPakForTesting(FContentPackMountService::FMountPak InMountPak);
	void SetInspectPakForTesting(FContentPackMountService::FInspectPak InInspectPak);
	void SetScanMountedProviderForTesting(FScanMountedProvider InScanMountedProvider);

	static FString LexToString(EContentPackProviderType Type);
	static bool TryParseProviderType(const FString& Value, EContentPackProviderType& OutType);

private:
	FContentPackProviderActivationResult MountAndScan(
		EContentPackProviderType Type,
		const FString& ManifestPath,
		const FString& PakPath);
	bool SaveRegistry(
		const TMap<EContentPackProviderType, FContentPackProviderSlot>& Registry,
		FString& OutError) const;
	bool LoadRegistry(
		TMap<EContentPackProviderType, FContentPackProviderSlot>& OutRegistry,
		FString& OutError) const;
	bool HasCrossTypeAssetConflict(
		EContentPackProviderType Type,
		const TArray<FPrimaryAssetId>& AssetIds,
		FString& OutError) const;

	FContentPackProviderManagerPolicy Policy;
	TMap<EContentPackProviderType, FContentPackProviderSlot> ActiveRegistry;
	FContentPackMountService::FMountPak MountPak;
	FContentPackMountService::FInspectPak InspectPak;
	FScanMountedProvider ScanMountedProvider;
	int32 NextPakOrder = 100;
};
