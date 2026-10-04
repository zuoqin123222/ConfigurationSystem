#pragma once

#include "CoreMinimal.h"
#include "AutomotiveCatalogDomain.h"
#include "UObject/Object.h"
#include "AutomotiveConfigurationState.generated.h"

class UAutomotiveCatalogData;

UENUM(BlueprintType)
enum class EAutomotiveCustomizationKind : uint8
{
	MaterialVariant,
	Paint
};

USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FAutomotivePaintCustomization
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	FString ColorHex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	double Metallic = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	double Roughness = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	double ClearCoat = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	double OrangePeel = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	double FlakeIntensity = 0.0;

	bool operator==(const FAutomotivePaintCustomization& Other) const;
};

USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FAutomotiveCustomization
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	EAutomotiveCustomizationKind Kind = EAutomotiveCustomizationKind::MaterialVariant;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	FString MaterialVariantId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Automotive Catalog")
	FAutomotivePaintCustomization Paint;

	bool operator==(const FAutomotiveCustomization& Other) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAutomotiveConfigurationChanged);
DECLARE_MULTICAST_DELEGATE(FAutomotiveConfigurationChangedNative);

/**
 * 车型目录 v2 的独立动态状态。
 *
 * 每次修改都先在候选 selections/customizations 上完成全量校验和身份派生，
 * 成功后才一次提交并广播；失败与无变化均不广播。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API UAutomotiveConfigurationState final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Automotive Catalog")
	FAutomotiveConfigurationChanged OnChanged;

	/** C++ 观察者入口，与 Blueprint 事件在同一事务提交点广播。 */
	FAutomotiveConfigurationChangedNative OnChangedNative;

	/** 使用 catalog.defaultSelections 建立唯一默认状态；可选 surface 默认不选装。 */
	UFUNCTION(BlueprintCallable, Category = "Automotive Catalog")
	bool Initialize(UAutomotiveCatalogData* InCatalogAsset);

	/** 原子替换完整的动态选择与定制，只广播一次。 */
	UFUNCTION(BlueprintCallable, Category = "Automotive Catalog")
	bool ApplyTransaction(
		const TMap<FString, FString>& InSelections,
		const TMap<FString, FAutomotiveCustomization>& InCustomizations);

	/** 仅校验并派生候选，不修改状态，供持久化恢复做原子预检。 */
	bool CanApplyTransaction(
		const TMap<FString, FString>& InSelections,
		const TMap<FString, FAutomotiveCustomization>& InCustomizations) const;

	UFUNCTION(BlueprintCallable, Category = "Automotive Catalog")
	bool SelectOption(const FString& SurfaceId, const FString& OptionId);

	UFUNCTION(BlueprintCallable, Category = "Automotive Catalog")
	bool ClearOptionalSelection(const FString& SurfaceId);

	UFUNCTION(BlueprintCallable, Category = "Automotive Catalog")
	bool SetMaterialVariant(const FString& SurfaceId, const FString& MaterialVariantId);

	UFUNCTION(BlueprintCallable, Category = "Automotive Catalog")
	bool SetPaintCustomization(
		const FString& SurfaceId,
		const FAutomotivePaintCustomization& Paint);

	UFUNCTION(BlueprintCallable, Category = "Automotive Catalog")
	bool ClearCustomization(const FString& SurfaceId);

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	bool IsInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	TMap<FString, FString> GetSelections() const { return Selections; }

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	TMap<FString, FAutomotiveCustomization> GetCustomizations() const { return Customizations; }

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	FString GetConfigurationId() const { return ConfigurationId; }

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	FString GetRenderKey() const { return RenderKey; }

	UFUNCTION(BlueprintPure, Category = "Automotive Catalog")
	FString GetLastErrorCode() const { return LastErrorCode; }

	const AutomotiveCatalog::FCatalogIndex& GetCatalogIndex() const { return CatalogIndex; }

private:
	bool TryCommit(
		const TMap<FString, FString>& CandidateSelections,
		const TMap<FString, FAutomotiveCustomization>& CandidateCustomizations,
		bool bBroadcast);
	static AutomotiveCatalog::FCustomizations ToDomainCustomizations(
		const TMap<FString, FAutomotiveCustomization>& Values);

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveCatalogData> CatalogAsset;

	UPROPERTY(Transient)
	TMap<FString, FString> Selections;

	UPROPERTY(Transient)
	TMap<FString, FAutomotiveCustomization> Customizations;

	UPROPERTY(Transient)
	FString ConfigurationId;

	UPROPERTY(Transient)
	FString RenderKey;

	UPROPERTY(Transient)
	FString LastErrorCode;

	AutomotiveCatalog::FCatalogIndex CatalogIndex;
	bool bInitialized = false;
};
