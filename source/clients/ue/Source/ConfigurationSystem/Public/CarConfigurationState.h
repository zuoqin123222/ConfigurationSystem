#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CarConfigurationState.generated.h"

/** 四个固定配置分区的当前选择。 */
USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FCarConfigurationSelection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FString Paint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FString Wheel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FString Interior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FString Frame;

	bool operator==(const FCarConfigurationSelection& Other) const
	{
		return Paint == Other.Paint
			&& Wheel == Other.Wheel
			&& Interior == Other.Interior
			&& Frame == Other.Frame;
	}

	bool operator!=(const FCarConfigurationSelection& Other) const
	{
		return !(*this == Other);
	}
};

/** 一个可选配置项；价格以货币最小单位表示。 */
USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FCarConfigurationOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FString PartId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FString OptionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	int64 PriceDeltaMinor = 0;
};

/** 一组可一次应用的四分区配置模板。 */
USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FCarConfigurationTemplate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FString TemplateId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "车辆配置")
	FCarConfigurationSelection Selections;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCarConfigurationChanged);

/**
 * Blueprint 可直接使用的车辆配置状态。
 *
 * 分区集合固定为 paint、wheel、interior、frame；所有修改先完整校验，
 * 非法分区、跨分区选项和未知模板均不会改变当前状态，也不会触发事件。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API UCarConfigurationState : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "车辆配置")
	FCarConfigurationChanged OnChanged;

	/** 用目录数据初始化状态；数据无效时返回 false，且保留原状态。 */
	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	bool Initialize(
		int64 InBasePriceMinor,
		const TArray<FCarConfigurationOption>& InOptions,
		const TArray<FCarConfigurationTemplate>& InTemplates,
		const FCarConfigurationSelection& InDefaults);

	/** 在指定分区选择选项；成功但选择未变化时不重复触发事件。 */
	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	bool SelectOption(const FString& PartId, const FString& OptionId);

	/** 原子应用模板；模板不存在或模板内容无效时状态保持不变。 */
	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	bool ApplyTemplate(const FString& TemplateId);

	/** 完整校验后一次性应用四分区选择；用于持久化恢复且只广播一次。 */
	UFUNCTION(BlueprintCallable, Category = "车辆配置")
	bool ApplySelection(const FCarConfigurationSelection& InSelection);

	/** 只校验四分区选择，不修改状态。 */
	UFUNCTION(BlueprintPure, Category = "车辆配置")
	bool CanApplySelection(const FCarConfigurationSelection& InSelection) const;

	/** 按 paint__wheel__interior__frame 的固定顺序生成规范键。 */
	UFUNCTION(BlueprintPure, Category = "车辆配置")
	FString GetCanonicalKey() const;

	/** 返回基础价加当前四个选项价差，单位为货币最小单位。 */
	UFUNCTION(BlueprintPure, Category = "车辆配置")
	int64 GetTotalPrice() const;

	UFUNCTION(BlueprintPure, Category = "车辆配置")
	FCarConfigurationSelection GetSelection() const { return Selection; }

private:
	static bool IsValidPartId(const FString& PartId);
	static const FString* GetSelectionForPart(
		const FCarConfigurationSelection& Value,
		const FString& PartId);
	static FString* GetSelectionForPart(
		FCarConfigurationSelection& Value,
		const FString& PartId);

	bool IsSelectionValid(
		const FCarConfigurationSelection& Value,
		const TMap<FString, FCarConfigurationOption>& CandidateOptions) const;

	UPROPERTY(Transient)
	int64 BasePriceMinor = 0;

	UPROPERTY(Transient)
	TMap<FString, FCarConfigurationOption> OptionsById;

	UPROPERTY(Transient)
	TMap<FString, FCarConfigurationTemplate> TemplatesById;

	UPROPERTY(Transient)
	FCarConfigurationSelection Selection;

	UPROPERTY(Transient)
	bool bInitialized = false;
};
