#include "ContentPackProviderManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "HAL/PlatformFileManager.h"
#include "IPlatformFilePak.h"
#include "Misc/CoreDelegates.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogContentPackProviderManager, Log, All);

namespace ContentPackProvider
{
	constexpr const TCHAR* RegistrySchemaVersion = TEXT("1.0.0");

	TSharedRef<FJsonObject> RecordToJson(const FContentPackProviderRecord& Record)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("packId"), Record.PackId);
		Json->SetStringField(TEXT("version"), Record.Version);
		Json->SetStringField(TEXT("manifestPath"), Record.ManifestPath);
		Json->SetStringField(TEXT("pakPath"), Record.PakPath);
		Json->SetStringField(TEXT("mountPoint"), Record.MountPoint);
		TArray<TSharedPtr<FJsonValue>> AssetIds;
		for (const FPrimaryAssetId& AssetId : Record.PrimaryAssetIds)
		{
			AssetIds.Add(MakeShared<FJsonValueString>(AssetId.ToString()));
		}
		Json->SetArrayField(TEXT("primaryAssetIds"), MoveTemp(AssetIds));
		return Json;
	}

	bool ReadString(
		const TSharedPtr<FJsonObject>& Json,
		const TCHAR* Field,
		FString& OutValue,
		FString& OutError)
	{
		if (!Json.IsValid() || !Json->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("active registry.%s 必须是非空字符串。"), Field);
			return false;
		}
		return true;
	}

	bool RecordFromJson(
		const TSharedPtr<FJsonObject>& Json,
		const EContentPackProviderType Type,
		FContentPackProviderRecord& OutRecord,
		FString& OutError)
	{
		OutRecord.Type = Type;
		if (!ReadString(Json, TEXT("packId"), OutRecord.PackId, OutError)
			|| !ReadString(Json, TEXT("version"), OutRecord.Version, OutError)
			|| !ReadString(Json, TEXT("manifestPath"), OutRecord.ManifestPath, OutError)
			|| !ReadString(Json, TEXT("pakPath"), OutRecord.PakPath, OutError)
			|| !ReadString(Json, TEXT("mountPoint"), OutRecord.MountPoint, OutError))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Json->TryGetArrayField(TEXT("primaryAssetIds"), Values)
			|| Values == nullptr
			|| Values->IsEmpty())
		{
			OutError = TEXT("active registry.primaryAssetIds 必须是非空数组。");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString Text;
			if (!Value.IsValid() || !Value->TryGetString(Text))
			{
				OutError = TEXT("active registry.primaryAssetIds 包含非字符串。");
				return false;
			}
			const FPrimaryAssetId AssetId = FPrimaryAssetId::FromString(Text);
			if (!AssetId.IsValid() || OutRecord.PrimaryAssetIds.Contains(AssetId))
			{
				OutError = TEXT("active registry.primaryAssetIds 包含无效或重复 ID。");
				return false;
			}
			OutRecord.PrimaryAssetIds.Add(AssetId);
		}
		return true;
	}

	bool DefaultScanMountedProvider(
		const FContentPackManifest& Manifest,
		FString& OutError)
	{
		FAssetRegistryModule& Module =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FString> PathsToScan{Manifest.MountPoint};
		Module.Get().ScanPathsSynchronous(PathsToScan, true);

		UAssetManager& AssetManager = UAssetManager::Get();
		FString RequiredRoot = Manifest.MountPoint;
		RequiredRoot.RemoveFromEnd(TEXT("/"));
		for (const FPrimaryAssetId& AssetId : Manifest.PrimaryAssetIds)
		{
			const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
			const FString PackageName = AssetPath.GetLongPackageName();
			if (!AssetPath.IsValid()
				|| !(PackageName == RequiredRoot
					|| PackageName.StartsWith(RequiredRoot + TEXT("/"))))
			{
				OutError = FString::Printf(
					TEXT("挂载后未在受限 root %s 下发现声明的 PrimaryAsset：%s"),
					*Manifest.MountPoint,
					*AssetId.ToString());
				return false;
			}
		}
		return true;
	}

	FContentPackProviderRecord MakeRecord(
		const EContentPackProviderType Type,
		const FContentPackManifest& Manifest,
		const FString& ManifestPath,
		const FString& PakPath)
	{
		FContentPackProviderRecord Record;
		Record.Type = Type;
		Record.PackId = Manifest.PackId;
		Record.Version = Manifest.Version;
		Record.ManifestPath = FPaths::ConvertRelativePathToFull(ManifestPath);
		Record.PakPath = FPaths::ConvertRelativePathToFull(PakPath);
		Record.MountPoint = Manifest.MountPoint;
		Record.PrimaryAssetIds = Manifest.PrimaryAssetIds;
		return Record;
	}
}

FContentPackProviderManager::FContentPackProviderManager(
	FContentPackProviderManagerPolicy InPolicy)
	: Policy(MoveTemp(InPolicy))
{
	if (Policy.RegistryPath.IsEmpty())
	{
		Policy.RegistryPath = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("ContentPackProviders"),
			TEXT("active-registry.json"));
	}
	MountPak = [](const FString& PakPath, const int32 PakOrder)
	{
		return FCoreDelegates::MountPak.IsBound()
			&& FCoreDelegates::MountPak.Execute(PakPath, PakOrder) != nullptr;
	};
	InspectPak = [](const FString& PakPath, FString& OutMountPoint, FString& OutError)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		TRefCountPtr<FPakFile> PakFile = new FPakFile(&PlatformFile, *PakPath, false);
		if (!PakFile->IsValid())
		{
			OutError = TEXT("pak 容器无效、索引不可读或需要未提供的密钥。");
			return false;
		}
		OutMountPoint = PakFile->GetMountPoint();
		FPaths::NormalizeDirectoryName(OutMountPoint);
		const int32 ContentIndex = OutMountPoint.Find(
			TEXT("/Content/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (ContentIndex != INDEX_NONE)
		{
			OutMountPoint = TEXT("/Game/")
				+ OutMountPoint.Mid(ContentIndex + FCString::Strlen(TEXT("/Content/")));
		}
		if (!OutMountPoint.EndsWith(TEXT("/")))
		{
			OutMountPoint += TEXT("/");
		}
		return true;
	};
	ScanMountedProvider = ContentPackProvider::DefaultScanMountedProvider;
}

void FContentPackProviderManager::SetMountPakForTesting(
	FContentPackMountService::FMountPak InMountPak)
{
	MountPak = MoveTemp(InMountPak);
}

void FContentPackProviderManager::SetInspectPakForTesting(
	FContentPackMountService::FInspectPak InInspectPak)
{
	InspectPak = MoveTemp(InInspectPak);
}

void FContentPackProviderManager::SetScanMountedProviderForTesting(
	FScanMountedProvider InScanMountedProvider)
{
	ScanMountedProvider = MoveTemp(InScanMountedProvider);
}

FString FContentPackProviderManager::LexToString(const EContentPackProviderType Type)
{
	switch (Type)
	{
	case EContentPackProviderType::Material:
		return TEXT("material");
	case EContentPackProviderType::Environment:
		return TEXT("environment");
	case EContentPackProviderType::Vehicle:
		return TEXT("vehicle");
	default:
		checkNoEntry();
		return FString();
	}
}

bool FContentPackProviderManager::TryParseProviderType(
	const FString& Value,
	EContentPackProviderType& OutType)
{
	if (Value == TEXT("material"))
	{
		OutType = EContentPackProviderType::Material;
		return true;
	}
	if (Value == TEXT("environment"))
	{
		OutType = EContentPackProviderType::Environment;
		return true;
	}
	if (Value == TEXT("vehicle"))
	{
		OutType = EContentPackProviderType::Vehicle;
		return true;
	}
	return false;
}

const FContentPackProviderSlot* FContentPackProviderManager::FindActive(
	const EContentPackProviderType Type) const
{
	return ActiveRegistry.Find(Type);
}

FContentPackMountResult FContentPackProviderManager::Preflight(
	const EContentPackProviderType Type,
	const FString& ManifestPath,
	const FString& PakPath,
	bool* bOutLegacyV1Compatibility) const
{
	if (bOutLegacyV1Compatibility != nullptr)
	{
		*bOutLegacyV1Compatibility = false;
	}

	FContentPackMountPolicy MountPolicy;
	MountPolicy.CatalogVersion = Policy.CatalogVersion;
	MountPolicy.EngineVersion = Policy.EngineVersion;
	MountPolicy.Platform = Policy.Platform;
	MountPolicy.AllowedMountRoots = Policy.AllowedMountRoots;
	MountPolicy.MaximumPakBytes = Policy.MaximumPakBytes;
	FContentPackMountService Service(MoveTemp(MountPolicy));
	Service.SetInspectPakForTesting(InspectPak);
	FContentPackMountResult Result = Service.Preflight(ManifestPath, PakPath);
	if (Result.bPreflightPassed)
	{
		EContentPackProviderType DeclaredType = Type;
		if (Result.Manifest.SchemaVersion != TEXT("2.0.0")
			|| !TryParseProviderType(Result.Manifest.ProviderType, DeclaredType)
			|| DeclaredType != Type)
		{
			Result.Errors.Add(
				TEXT("SC01 Provider manifest 必须使用 schemaVersion 2.0.0，且 providerType 与所选类型一致。"));
		}
		else
		{
			FString ConflictError;
			if (HasCrossTypeAssetConflict(
				Type, Result.Manifest.PrimaryAssetIds, ConflictError))
			{
				Result.Errors.Add(MoveTemp(ConflictError));
			}
		}
		Result.bPreflightPassed = Result.Errors.IsEmpty();
		return Result;
	}

	FContentPackMountPolicy LegacyPolicy;
	LegacyPolicy.CatalogVersion = TEXT("mvp-v1");
	LegacyPolicy.EngineVersion = Policy.EngineVersion;
	LegacyPolicy.Platform = Policy.Platform;
	LegacyPolicy.AllowedMountRoots = Policy.AllowedMountRoots;
	LegacyPolicy.MaximumPakBytes = Policy.MaximumPakBytes;
	FContentPackMountService LegacyService(MoveTemp(LegacyPolicy));
	LegacyService.SetInspectPakForTesting(InspectPak);
	FContentPackMountResult LegacyResult =
		LegacyService.Preflight(ManifestPath, PakPath);
	if (LegacyResult.bPreflightPassed
		&& LegacyResult.Manifest.SchemaVersion == TEXT("1.0.0")
		&& LegacyResult.Manifest.CatalogVersion == TEXT("mvp-v1"))
	{
		if (bOutLegacyV1Compatibility != nullptr)
		{
			*bOutLegacyV1Compatibility = true;
		}
		return LegacyResult;
	}
	return Result;
}

bool FContentPackProviderManager::HasCrossTypeAssetConflict(
	const EContentPackProviderType Type,
	const TArray<FPrimaryAssetId>& AssetIds,
	FString& OutError) const
{
	for (const TPair<EContentPackProviderType, FContentPackProviderSlot>& Pair : ActiveRegistry)
	{
		if (Pair.Key == Type)
		{
			continue;
		}
		for (const FPrimaryAssetId& AssetId : AssetIds)
		{
			if (Pair.Value.Current.PrimaryAssetIds.Contains(AssetId))
			{
				OutError = FString::Printf(
					TEXT("PrimaryAssetId 与活动的 %s Provider 冲突：%s"),
					*LexToString(Pair.Key),
					*AssetId.ToString());
				return true;
			}
		}
	}
	return false;
}

FContentPackProviderActivationResult FContentPackProviderManager::MountAndScan(
	const EContentPackProviderType Type,
	const FString& ManifestPath,
	const FString& PakPath)
{
	FContentPackProviderActivationResult Result;
	bool bLegacyV1Compatibility = false;
	Result.MountResult = Preflight(
		Type, ManifestPath, PakPath, &bLegacyV1Compatibility);
	if (bLegacyV1Compatibility)
	{
		Result.Errors.Add(TEXT("旧 mvp-v1 manifest 仅支持兼容预检，不能激活为 SC01 Provider。"));
		return Result;
	}
	if (!Result.MountResult.bPreflightPassed)
	{
		Result.Errors = Result.MountResult.Errors;
		return Result;
	}

	FContentPackMountPolicy MountPolicy;
	MountPolicy.CatalogVersion = Policy.CatalogVersion;
	MountPolicy.EngineVersion = Policy.EngineVersion;
	MountPolicy.Platform = Policy.Platform;
	MountPolicy.AllowedMountRoots = Policy.AllowedMountRoots;
	MountPolicy.MaximumPakBytes = Policy.MaximumPakBytes;

	FContentPackMountService Service(MoveTemp(MountPolicy));
	Service.SetMountPakForTesting(MountPak);
	Service.SetInspectPakForTesting(InspectPak);

	Result.MountResult = Service.PreflightAndMount(
		ManifestPath, PakPath, NextPakOrder++);
	if (!Result.MountResult.bMounted)
	{
		Result.Errors = Result.MountResult.Errors;
		return Result;
	}

	FString ScanError;
	if (!ScanMountedProvider(Result.MountResult.Manifest, ScanError))
	{
		Result.Errors.Add(ScanError.IsEmpty()
			? TEXT("挂载后 PrimaryAsset 扫描失败。")
			: MoveTemp(ScanError));
		return Result;
	}
	return Result;
}

FContentPackProviderActivationResult FContentPackProviderManager::Activate(
	const EContentPackProviderType Type,
	const FString& ManifestPath,
	const FString& PakPath)
{
	FContentPackProviderActivationResult Result =
		MountAndScan(Type, ManifestPath, PakPath);
	if (!Result.Errors.IsEmpty() || !Result.MountResult.bMounted)
	{
		return Result;
	}

	TMap<EContentPackProviderType, FContentPackProviderSlot> Staged = ActiveRegistry;
	FContentPackProviderSlot& Slot = Staged.FindOrAdd(Type);
	const FContentPackProviderRecord Candidate = ContentPackProvider::MakeRecord(
		Type, Result.MountResult.Manifest, ManifestPath, PakPath);
	if (!Slot.Current.Version.IsEmpty())
	{
		Slot.Previous = Slot.Current;
	}
	Slot.Current = Candidate;

	FString SaveError;
	if (!SaveRegistry(Staged, SaveError))
	{
		Result.Errors.Add(MoveTemp(SaveError));
		return Result;
	}
	ActiveRegistry = MoveTemp(Staged);
	Result.bActivated = true;
	return Result;
}

bool FContentPackProviderManager::Rollback(
	const EContentPackProviderType Type,
	TArray<FString>& OutErrors)
{
	const FContentPackProviderSlot* Existing = ActiveRegistry.Find(Type);
	if (Existing == nullptr || !Existing->Previous.IsSet())
	{
		OutErrors.Add(TEXT("没有可回滚的上一 Provider 版本。"));
		return false;
	}
	const FContentPackProviderRecord Target = Existing->Previous.GetValue();
	FContentPackProviderActivationResult Mounted =
		MountAndScan(Type, Target.ManifestPath, Target.PakPath);
	if (!Mounted.Errors.IsEmpty() || !Mounted.MountResult.bMounted)
	{
		OutErrors.Append(Mounted.Errors);
		return false;
	}

	TMap<EContentPackProviderType, FContentPackProviderSlot> Staged = ActiveRegistry;
	FContentPackProviderSlot& Slot = Staged.FindChecked(Type);
	const FContentPackProviderRecord Replaced = Slot.Current;
	Slot.Current = Target;
	Slot.Previous = Replaced;
	FString SaveError;
	if (!SaveRegistry(Staged, SaveError))
	{
		OutErrors.Add(MoveTemp(SaveError));
		return false;
	}
	ActiveRegistry = MoveTemp(Staged);
	return true;
}

bool FContentPackProviderManager::RestoreActiveProviders(TArray<FString>& OutErrors)
{
	TMap<EContentPackProviderType, FContentPackProviderSlot> Persisted;
	FString LoadError;
	if (!LoadRegistry(Persisted, LoadError))
	{
		OutErrors.Add(MoveTemp(LoadError));
		return false;
	}

	TSet<FPrimaryAssetId> Seen;
	for (const TPair<EContentPackProviderType, FContentPackProviderSlot>& Pair : Persisted)
	{
		for (const FPrimaryAssetId& AssetId : Pair.Value.Current.PrimaryAssetIds)
		{
			if (Seen.Contains(AssetId))
			{
				OutErrors.Add(FString::Printf(
					TEXT("active registry 的 Provider 间 PrimaryAssetId 冲突：%s"),
					*AssetId.ToString()));
				return false;
			}
			Seen.Add(AssetId);
		}
	}

	const TMap<EContentPackProviderType, FContentPackProviderSlot> Original =
		MoveTemp(ActiveRegistry);
	ActiveRegistry.Reset();
	for (const TPair<EContentPackProviderType, FContentPackProviderSlot>& Pair : Persisted)
	{
		const FContentPackProviderRecord& Record = Pair.Value.Current;
		FContentPackProviderActivationResult Mounted =
			MountAndScan(Pair.Key, Record.ManifestPath, Record.PakPath);
		if (!Mounted.Errors.IsEmpty() || !Mounted.MountResult.bMounted)
		{
			OutErrors.Append(Mounted.Errors);
			ActiveRegistry = Original;
			return false;
		}
	}
	ActiveRegistry = MoveTemp(Persisted);
	return true;
}

bool FContentPackProviderManager::SaveRegistry(
	const TMap<EContentPackProviderType, FContentPackProviderSlot>& Registry,
	FString& OutError) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schemaVersion"), ContentPackProvider::RegistrySchemaVersion);
	Root->SetStringField(TEXT("catalogVersion"), Policy.CatalogVersion);
	TSharedRef<FJsonObject> Providers = MakeShared<FJsonObject>();
	for (const TPair<EContentPackProviderType, FContentPackProviderSlot>& Pair : Registry)
	{
		TSharedRef<FJsonObject> Slot = MakeShared<FJsonObject>();
		Slot->SetObjectField(
			TEXT("current"),
			ContentPackProvider::RecordToJson(Pair.Value.Current));
		if (Pair.Value.Previous.IsSet())
		{
			Slot->SetObjectField(
				TEXT("previous"),
				ContentPackProvider::RecordToJson(Pair.Value.Previous.GetValue()));
		}
		Providers->SetObjectField(LexToString(Pair.Key), Slot);
	}
	Root->SetObjectField(TEXT("providers"), Providers);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("无法序列化 active registry。");
		return false;
	}

	const FString FullRegistryPath = FPaths::ConvertRelativePathToFull(Policy.RegistryPath);
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullRegistryPath), true))
	{
		OutError = TEXT("无法创建 active registry 目录。");
		return false;
	}
	const FString TemporaryPath = FullRegistryPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(
		JsonText,
		*TemporaryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = TEXT("无法写入 active registry 临时文件。");
		return false;
	}
	if (!IFileManager::Get().Move(
		*FullRegistryPath, *TemporaryPath, true, true, false, true))
	{
		IFileManager::Get().Delete(*TemporaryPath);
		OutError = TEXT("无法原子替换 active registry。");
		return false;
	}
	return true;
}

bool FContentPackProviderManager::LoadRegistry(
	TMap<EContentPackProviderType, FContentPackProviderSlot>& OutRegistry,
	FString& OutError) const
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *Policy.RegistryPath))
	{
		OutError = TEXT("无法读取 active registry。");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("active registry 不是有效 JSON object。");
		return false;
	}
	FString SchemaVersion;
	FString CatalogVersion;
	if (!Root->TryGetStringField(TEXT("schemaVersion"), SchemaVersion)
		|| SchemaVersion != ContentPackProvider::RegistrySchemaVersion
		|| !Root->TryGetStringField(TEXT("catalogVersion"), CatalogVersion)
		|| CatalogVersion != Policy.CatalogVersion)
	{
		OutError = TEXT("active registry schema 或 catalogVersion 不匹配。");
		return false;
	}
	const TSharedPtr<FJsonObject>* Providers = nullptr;
	if (!Root->TryGetObjectField(TEXT("providers"), Providers)
		|| Providers == nullptr
		|| !Providers->IsValid())
	{
		OutError = TEXT("active registry.providers 必须是 object。");
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Providers)->Values)
	{
		EContentPackProviderType Type;
		if (!TryParseProviderType(Pair.Key, Type))
		{
			OutError = FString::Printf(
				TEXT("active registry 包含未知 Provider 类型：%s"), *Pair.Key);
			return false;
		}
		const TSharedPtr<FJsonObject> SlotJson = Pair.Value->AsObject();
		const TSharedPtr<FJsonObject>* CurrentJson = nullptr;
		if (!SlotJson.IsValid()
			|| !SlotJson->TryGetObjectField(TEXT("current"), CurrentJson)
			|| CurrentJson == nullptr)
		{
			OutError = TEXT("active registry Provider 缺少 current。");
			return false;
		}
		FContentPackProviderSlot Slot;
		if (!ContentPackProvider::RecordFromJson(
			*CurrentJson, Type, Slot.Current, OutError))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* PreviousJson = nullptr;
		if (SlotJson->TryGetObjectField(TEXT("previous"), PreviousJson)
			&& PreviousJson != nullptr)
		{
			FContentPackProviderRecord Previous;
			if (!ContentPackProvider::RecordFromJson(
				*PreviousJson, Type, Previous, OutError))
			{
				return false;
			}
			Slot.Previous = MoveTemp(Previous);
		}
		OutRegistry.Add(Type, MoveTemp(Slot));
	}
	return true;
}
