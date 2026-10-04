#pragma once

#include "CarConfigurationState.h"
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "AutomotiveConfigurationState.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ConfiguratorExperienceSaveGame.generated.h"

UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorExperienceSaveGame final : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 SchemaVersion = 2;

	UPROPERTY(SaveGame)
	FCarConfigurationSelection Configuration;

	UPROPERTY(SaveGame)
	int32 EnvironmentIndex = 0;

	UPROPERTY(SaveGame)
	bool bPanelVisible = true;

	/** v2 状态可选存在；旧版 schema=1 存档仍按原路径读取。 */
	UPROPERTY(SaveGame)
	bool bHasAutomotiveState = false;

	UPROPERTY(SaveGame)
	TMap<FString, FString> AutomotiveSelections;

	UPROPERTY(SaveGame)
	TMap<FString, FAutomotiveCustomization> AutomotiveCustomizations;
};

/** 仅将配置、环境与 UI 快照序列化，并通过 final/backup/temp 可靠替换。 */
UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorPersistenceSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Configurator|Persistence")
	bool SaveAtomic(const FString& SlotName, const UConfiguratorExperienceSaveGame* Snapshot,
		FString& OutError) const;

	UFUNCTION(BlueprintCallable, Category="Configurator|Persistence")
	UConfiguratorExperienceSaveGame* Load(const FString& SlotName, FString& OutError) const;

	UFUNCTION(BlueprintPure, Category="Configurator|Persistence")
	static FString GetSlotFilename(const FString& SlotName);

	/** 已写好的 temp 依次执行 final->backup、temp->final，失败时恢复 backup。 */
	static bool ReplaceFileReliably(
		const FString& FinalPath,
		const FString& TempPath,
		FString& OutError);
};
