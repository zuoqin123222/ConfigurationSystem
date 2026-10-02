#pragma once

#include "CoreMinimal.h"

enum class EAdminImportAssetKind : uint8
{
	Model,
	Animation
};

struct FAdminImportSelection
{
	EAdminImportAssetKind Kind = EAdminImportAssetKind::Model;
	FString FbxFile;
	FString SidecarFile;
};

struct FAdminImportItemResult
{
	EAdminImportAssetKind Kind = EAdminImportAssetKind::Model;
	FString FbxFile;
	FString SidecarFile;
	FString DeclaredArtifactPath;
	FString ExpectedSha256;
	FString ActualSha256;
	int64 ExpectedBytes = 0;
	int64 ActualBytes = 0;
	bool bPassed = false;
	TArray<FString> Errors;
	TArray<FString> Warnings;
};

struct FAdminImportPreflightResult
{
	FString SessionId;
	FString TimestampUtc;
	FString StagingPath;
	FString ReportPath;
	bool bPassed = false;
	bool bImportAttempted = false;
	bool bImportSucceeded = false;
	TArray<FAdminImportItemResult> Items;
	TArray<FString> ImportedObjectPaths;
};

/**
 * Editor-only gate for administrator FBX intake.
 *
 * The selected file, rather than a path inferred from JSON, is hashed.  A caller
 * must retain the returned session and result unchanged before importing.
 */
class FAdminImportPreflight
{
public:
	static FAdminImportPreflightResult Run(
		const TArray<FAdminImportSelection>& Selections,
		const FString& SessionId = FString());

	static bool ComputeFileSha256(
		const FString& Filename,
		FString& OutSha256,
		FString& OutError);

	static bool WriteJsonReport(FAdminImportPreflightResult& Result);
};

class FAdminImportService
{
public:
	/** Imports only a successful preflight into its unique staging session. */
	static bool ImportApproved(FAdminImportPreflightResult& Result);
};
