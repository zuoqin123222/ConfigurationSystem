#include "ConfiguratorExperienceSaveGame.h"

#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ConfiguratorPersistence
{
	FString SanitizeSlot(const FString& SlotName)
	{
		FString Result = FPaths::MakeValidFileName(SlotName);
		return Result.IsEmpty() ? TEXT("Experience") : Result;
	}
}

FString UConfiguratorPersistenceSubsystem::GetSlotFilename(const FString& SlotName)
{
	return FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("SaveGames"),
		ConfiguratorPersistence::SanitizeSlot(SlotName) + TEXT(".sav"));
}

bool UConfiguratorPersistenceSubsystem::SaveAtomic(
	const FString& SlotName,
	const UConfiguratorExperienceSaveGame* Snapshot,
	FString& OutError) const
{
	OutError.Reset();
	if (Snapshot == nullptr)
	{
		OutError = TEXT("快照为空。");
		return false;
	}

	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(
		const_cast<UConfiguratorExperienceSaveGame*>(Snapshot), Bytes))
	{
		OutError = TEXT("SaveGame 序列化失败。");
		return false;
	}

	const FString FinalPath = GetSlotFilename(SlotName);
	const FString TempPath = FinalPath + TEXT(".tmp");
	IFileManager& Files = IFileManager::Get();
	if (!Files.MakeDirectory(*FPaths::GetPath(FinalPath), true)
		|| !FFileHelper::SaveArrayToFile(Bytes, *TempPath))
	{
		OutError = TEXT("无法写入临时存档。");
		Files.Delete(*TempPath, false, true);
		return false;
	}

	if (!ReplaceFileReliably(FinalPath, TempPath, OutError))
	{
		Files.Delete(*TempPath, false, true);
		return false;
	}
	return true;
}

bool UConfiguratorPersistenceSubsystem::ReplaceFileReliably(
	const FString& FinalPath,
	const FString& TempPath,
	FString& OutError)
{
	IFileManager& Files = IFileManager::Get();
	const FString BackupPath = FinalPath + TEXT(".bak");
	const bool bHadFinal = Files.FileExists(*FinalPath);

	Files.Delete(*BackupPath, false, true);
	if (bHadFinal
		&& !Files.Move(*BackupPath, *FinalPath, true, true, false, true))
	{
		OutError = TEXT("无法备份现有存档。");
		return false;
	}

	if (Files.Move(*FinalPath, *TempPath, false, true, false, true))
	{
		Files.Delete(*BackupPath, false, true);
		return true;
	}

	const bool bRolledBack = !bHadFinal
		|| Files.Move(*FinalPath, *BackupPath, false, true, false, true);
	OutError = bRolledBack
		? TEXT("无法替换正式存档，已恢复旧存档。")
		: TEXT("无法替换正式存档，且旧存档回滚失败。");
	return false;
}

UConfiguratorExperienceSaveGame* UConfiguratorPersistenceSubsystem::Load(
	const FString& SlotName,
	FString& OutError) const
{
	OutError.Reset();
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *GetSlotFilename(SlotName)))
	{
		OutError = TEXT("存档不存在或不可读。");
		return nullptr;
	}

	UConfiguratorExperienceSaveGame* Snapshot = Cast<UConfiguratorExperienceSaveGame>(
		UGameplayStatics::LoadGameFromMemory(Bytes));
	if (Snapshot == nullptr || Snapshot->SchemaVersion != 1)
	{
		OutError = TEXT("存档类型或版本无效。");
		return nullptr;
	}
	return Snapshot;
}
