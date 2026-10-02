#pragma once

#include "CoreMinimal.h"
#include "Sc01V2Domain.h"
#include "UObject/Object.h"
#include "Sc01V2ConfigurationState.generated.h"

class USc01V2CatalogData;

UENUM(BlueprintType)
enum class ESc01V2CustomizationKind : uint8
{
	MaterialVariant,
	Paint
};

USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FSc01V2PaintCustomization
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	FString ColorHex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	double Metallic = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	double Roughness = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	double ClearCoat = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	double OrangePeel = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	double FlakeIntensity = 0.0;

	bool operator==(const FSc01V2PaintCustomization& Other) const;
};

USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FSc01V2Customization
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	ESc01V2CustomizationKind Kind = ESc01V2CustomizationKind::MaterialVariant;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	FString MaterialVariantId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SC01 v2")
	FSc01V2PaintCustomization Paint;

	bool operator==(const FSc01V2Customization& Other) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSc01V2ConfigurationChanged);
DECLARE_MULTICAST_DELEGATE(FSc01V2ConfigurationChangedNative);

/**
 * SC01 v2 的独立动态状态。
 *
 * 每次修改都先在候选 selections/customizations 上完成全量校验和身份派生，
 * 成功后才一次提交并广播；失败与无变化均不广播。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API USc01V2ConfigurationState final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "SC01 v2")
	FSc01V2ConfigurationChanged OnChanged;

	/** C++ 观察者入口，与 Blueprint 事件在同一事务提交点广播。 */
	FSc01V2ConfigurationChangedNative OnChangedNative;

	/** 使用每个 surface 的第一个 option 建立 38 项确定性默认状态。 */
	UFUNCTION(BlueprintCallable, Category = "SC01 v2")
	bool Initialize(USc01V2CatalogData* InCatalogAsset);

	/** 原子替换完整的动态选择与定制，只广播一次。 */
	UFUNCTION(BlueprintCallable, Category = "SC01 v2")
	bool ApplyTransaction(
		const TMap<FString, FString>& InSelections,
		const TMap<FString, FSc01V2Customization>& InCustomizations);

	/** 仅校验并派生候选，不修改状态，供持久化恢复做原子预检。 */
	bool CanApplyTransaction(
		const TMap<FString, FString>& InSelections,
		const TMap<FString, FSc01V2Customization>& InCustomizations) const;

	UFUNCTION(BlueprintCallable, Category = "SC01 v2")
	bool SelectOption(const FString& SurfaceId, const FString& OptionId);

	UFUNCTION(BlueprintCallable, Category = "SC01 v2")
	bool SetMaterialVariant(const FString& SurfaceId, const FString& MaterialVariantId);

	UFUNCTION(BlueprintCallable, Category = "SC01 v2")
	bool SetPaintCustomization(
		const FString& SurfaceId,
		const FSc01V2PaintCustomization& Paint);

	UFUNCTION(BlueprintCallable, Category = "SC01 v2")
	bool ClearCustomization(const FString& SurfaceId);

	UFUNCTION(BlueprintPure, Category = "SC01 v2")
	bool IsInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category = "SC01 v2")
	TMap<FString, FString> GetSelections() const { return Selections; }

	UFUNCTION(BlueprintPure, Category = "SC01 v2")
	TMap<FString, FSc01V2Customization> GetCustomizations() const { return Customizations; }

	UFUNCTION(BlueprintPure, Category = "SC01 v2")
	FString GetConfigurationId() const { return ConfigurationId; }

	UFUNCTION(BlueprintPure, Category = "SC01 v2")
	FString GetRenderKey() const { return RenderKey; }

	UFUNCTION(BlueprintPure, Category = "SC01 v2")
	FString GetLastErrorCode() const { return LastErrorCode; }

	const Sc01V2::FCatalogIndex& GetCatalogIndex() const { return CatalogIndex; }

private:
	bool TryCommit(
		const TMap<FString, FString>& CandidateSelections,
		const TMap<FString, FSc01V2Customization>& CandidateCustomizations,
		bool bBroadcast);
	static Sc01V2::FCustomizations ToDomainCustomizations(
		const TMap<FString, FSc01V2Customization>& Values);

	UPROPERTY(Transient)
	TObjectPtr<USc01V2CatalogData> CatalogAsset;

	UPROPERTY(Transient)
	TMap<FString, FString> Selections;

	UPROPERTY(Transient)
	TMap<FString, FSc01V2Customization> Customizations;

	UPROPERTY(Transient)
	FString ConfigurationId;

	UPROPERTY(Transient)
	FString RenderKey;

	UPROPERTY(Transient)
	FString LastErrorCode;

	Sc01V2::FCatalogIndex CatalogIndex;
	bool bInitialized = false;
};
