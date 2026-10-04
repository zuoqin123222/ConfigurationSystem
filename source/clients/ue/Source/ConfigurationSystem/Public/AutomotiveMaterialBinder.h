#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AutomotiveMaterialBinder.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UAutomotiveMaterialLibrary;
class UAutomotiveConfigurationState;

/**
 * 把车型目录状态绑定到车辆代理槽。
 *
 * 车身槽消费 exterior-body-cover 自定义车漆；唯一内饰代理槽消费
 * door-middle，并可在五种 MVP 内饰材料族之间切换。
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

	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	bool Bind(
		UAutomotiveConfigurationState* InState,
		UAutomotiveMaterialLibrary* InLibrary,
		AActor* InVehicle);

	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	void Unbind();

	UFUNCTION(BlueprintCallable, Category = "Automotive|Materials")
	bool ApplyCurrentConfiguration();

	UFUNCTION(BlueprintPure, Category = "Automotive|Materials")
	FString GetAppliedInteriorFamilyId() const { return AppliedInteriorFamilyId; }

	UFUNCTION(BlueprintPure, Category = "Automotive|Materials")
	FString GetLastError() const { return LastError; }

	UMeshComponent* GetPaintComponent() const { return PaintComponent; }
	UMeshComponent* GetInteriorComponent() const { return InteriorComponent; }
	UMaterialInstanceDynamic* GetPaintMaterialInstance() const { return PaintMaterialInstance; }

protected:
	virtual void BeginDestroy() override;

private:
	void HandleStateChanged();
	static UMeshComponent* FindUniqueTaggedMesh(
		AActor* Vehicle,
		FName SlotTag,
		FString& OutError);
	bool ApplyPaint();
	bool ApplyInterior();

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
	FString AppliedInteriorFamilyId;

	UPROPERTY(Transient)
	FString LastError;
};
