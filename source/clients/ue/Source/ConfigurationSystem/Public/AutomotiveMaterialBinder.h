#pragma once

#include "CoreMinimal.h"
#include "AutomotiveConfigurationState.h"
#include "UObject/Object.h"
#include "AutomotiveMaterialBinder.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UTexture2D;
class UAutomotiveMaterialLibrary;
class UAutomotiveConfigurationState;

struct CONFIGURATIONSYSTEM_API FAutomotiveMaterialTransactionResult
{
	bool bSuccess = false;
	FString Code;
	FString Message;
	TArray<FString> AppliedSurfaceIds;
	TArray<FString> UnsupportedSurfaceIds;
	TArray<FName> AppliedSlotIds;
	FString ConfigurationId;

	FString ToJson() const;
};

USTRUCT()
struct FAutomotiveBoundMaterialSlot
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> Component;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OriginalMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DynamicParent;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DynamicColorTexture;

	FString SurfaceId;
	FName SlotId;
	int32 MaterialIndex = INDEX_NONE;
};

/**
 * 把车型目录 surface binding 原子绑定到车辆的一个或多个命名材质槽。
 * 显式 capability 缺口只提交配置状态并进入事务回执；可映射 surface 在同一
 * transaction 中完成材质预检、状态提交和实时槽更新。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API UAutomotiveMaterialBinder final : public UObject
{
	GENERATED_BODY()

public:
	static const FName PaintProxySlotTag;
	static const FName InteriorProxySlotTag;
	static const FString PaintSurfaceId;
	static const FString InteriorProxySurfaceId;
	static FName MakeProxyTargetTag(FName SlotId);

	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	bool Bind(
		UAutomotiveConfigurationState* InState,
		UAutomotiveMaterialLibrary* InLibrary,
		AActor* InVehicle);

	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	void Unbind();

	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	bool ApplyCurrentConfiguration();

	FAutomotiveMaterialTransactionResult ApplyTransaction(
		const TMap<FString, FString>& InSelections,
		const TMap<FString, FAutomotiveCustomization>& InCustomizations);

	UFUNCTION(BlueprintPure, Category = "Automotive|Materials")
	FString GetLastTransactionResultJson() const;

	UFUNCTION(BlueprintPure, Category = "Automotive|Materials")
	FString GetAppliedInteriorFamilyId() const { return AppliedInteriorFamilyId; }

	UFUNCTION(BlueprintPure, Category = "Automotive|Materials")
	FString GetLastError() const { return LastError; }

	UMeshComponent* GetPaintComponent() const { return PaintComponent; }
	UMeshComponent* GetInteriorComponent() const { return InteriorComponent; }
	UMaterialInstanceDynamic* GetPaintMaterialInstance() const { return PaintMaterialInstance; }
	UMaterialInstanceDynamic* GetInteriorMaterialInstance() const { return InteriorMaterialInstance; }
	int32 GetBoundSlotCount(const FString& SurfaceId) const;
	UMaterialInterface* GetAppliedMaterialForSurface(const FString& SurfaceId) const;

protected:
	virtual void BeginDestroy() override;

private:
	void HandleStateChanged();
	static UMeshComponent* FindUniqueTaggedMesh(
		AActor* Vehicle,
		FName SlotTag,
		FString& OutError);
	bool BuildBoundSlots(AActor* Vehicle);
	bool ApplySurface(
		const FString& SurfaceId,
		const TMap<FString, FString>& Selections,
		const TMap<FString, FAutomotiveCustomization>& Customizations,
		TArray<FName>* OutAppliedSlots = nullptr);
	bool ResolveSurfaceMaterial(
		const FString& SurfaceId,
		const TMap<FString, FString>& Selections,
		const TMap<FString, FAutomotiveCustomization>& Customizations,
		UMaterialInterface*& OutMaterial,
		bool& bOutUseDynamic,
		FLinearColor& OutColor,
		FAutomotivePaintCustomization& OutPaint,
		bool& bOutHasPaintParameters,
		FString& OutFamilyId,
		FString& OutErrorCode,
		FString& OutErrorMessage) const;
	void SetFailure(const FString& Code, const FString& Message);

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveConfigurationState> State;

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveMaterialLibrary> Library;

	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> PaintComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> InteriorComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PaintMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> InteriorMaterialInstance;

	UPROPERTY(Transient)
	TArray<FAutomotiveBoundMaterialSlot> BoundSlots;

	UPROPERTY(Transient)
	FString AppliedInteriorFamilyId;

	UPROPERTY(Transient)
	FString LastError;

	FAutomotiveMaterialTransactionResult LastTransactionResult;
	bool bApplyingTransaction = false;
};
