#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CarConfigurationState.h"
#include "CarConfiguratorSubsystem.generated.h"

class AConfiguratorVehicleActor;
class UAutomotiveMaterialBinder;
class UAutomotiveMaterialLibrary;
class UAutomotiveCatalogData;
class UAutomotiveConfigurationState;

USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FConfiguratorDisplayOption
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "车辆配置")
	FString PartId;

	UPROPERTY(BlueprintReadOnly, Category = "车辆配置")
	FString OptionId;

	UPROPERTY(BlueprintReadOnly, Category = "车辆配置")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "车辆配置")
	int64 PriceDeltaMinor = 0;
};

USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FConfiguratorDisplayTemplate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "车辆配置")
	FString TemplateId;

	UPROPERTY(BlueprintReadOnly, Category = "车辆配置")
	FText DisplayName;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FConfiguratorStateChanged);

/**
 * GameInstance 生命周期的配置门面。唯一持有现有 UCarConfigurationState，
 * UI 与车辆 Actor 都只通过本 Subsystem 读写，避免复制价格和 canonical key 规则。
 */
UCLASS()
class CONFIGURATIONSYSTEM_API UCarConfiguratorSubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "车辆配置")
	FConfiguratorStateChanged OnConfigurationChanged;

	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	bool SelectOption(const FString& PartId, const FString& OptionId);

	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	bool ApplyTemplate(const FString& TemplateId);

	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	bool ApplySelection(const FCarConfigurationSelection& Selection);

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	bool CanApplySelection(const FCarConfigurationSelection& Selection) const;

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	FString GetCanonicalKey() const;

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	int64 GetTotalPriceMinor() const;

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	FCarConfigurationSelection GetSelection() const;

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	TArray<FConfiguratorDisplayOption> GetDisplayOptions() const
	{
		return DisplayOptions;
	}

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	TArray<FConfiguratorDisplayTemplate> GetDisplayTemplates() const
	{
		return DisplayTemplates;
	}

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	UCarConfigurationState* GetState() const { return State; }

/** 车型目录 v2 的内嵌 JSON Primary Asset；v1 接口保持不变。 */
	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	UAutomotiveCatalogData* GetAutomotiveCatalog() const { return AutomotiveCatalogData; }

	/** 独立于四分区 v1 状态的 38 surface 动态状态。 */
	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	UAutomotiveConfigurationState* GetAutomotiveConfigurationState() const
	{
		return AutomotiveConfigurationState;
	}

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	UAutomotiveMaterialLibrary* GetAutomotiveMaterialLibrary() const
	{
		return AutomotiveMaterialLibrary;
	}

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	UAutomotiveMaterialBinder* GetAutomotiveMaterialBinder() const
	{
		return AutomotiveMaterialBinder;
	}

	void RegisterVehicle(AConfiguratorVehicleActor* Vehicle);
	void UnregisterVehicle(const AConfiguratorVehicleActor* Vehicle);

	/** 单一 MVP fixture 构造入口，供运行时和自动化探针共同使用。 */
	static void BuildMvpCatalog(
		int64& OutBasePriceMinor,
		TArray<FCarConfigurationOption>& OutOptions,
		TArray<FCarConfigurationTemplate>& OutTemplates,
		FCarConfigurationSelection& OutDefaults);

private:
	UFUNCTION()
	void HandleStateChanged();

	UPROPERTY(Transient)
	TObjectPtr<UCarConfigurationState> State;

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveCatalogData> AutomotiveCatalogData;

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveConfigurationState> AutomotiveConfigurationState;

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveMaterialLibrary> AutomotiveMaterialLibrary;

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveMaterialBinder> AutomotiveMaterialBinder;

	UPROPERTY(Transient)
	TObjectPtr<AConfiguratorVehicleActor> RegisteredVehicle;

	UPROPERTY(Transient)
	TArray<FConfiguratorDisplayOption> DisplayOptions;

	UPROPERTY(Transient)
	TArray<FConfiguratorDisplayTemplate> DisplayTemplates;
};
