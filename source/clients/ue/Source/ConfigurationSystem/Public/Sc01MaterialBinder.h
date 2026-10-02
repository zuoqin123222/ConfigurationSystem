#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Sc01MaterialBinder.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UMeshComponent;
class USc01MaterialLibrary;
class USc01V2ConfigurationState;

/**
 * 把 SC01 v2 状态绑定到车辆代理槽。
 *
 * 车身槽消费 exterior-body-cover 自定义车漆；唯一内饰代理槽消费
 * door-middle，并可在五种 MVP 内饰材料族之间切换。
 */
UCLASS(BlueprintType)
class CONFIGURATIONSYSTEM_API USc01MaterialBinder final : public UObject
{
	GENERATED_BODY()

public:
	static const FName PaintProxySlotTag;
	static const FName InteriorProxySlotTag;
	static const FString PaintSurfaceId;
	static const FString InteriorProxySurfaceId;

	UFUNCTION(BlueprintCallable, Category = "SC01|Materials")
	bool Bind(
		USc01V2ConfigurationState* InState,
		USc01MaterialLibrary* InLibrary,
		AActor* InVehicle);

	UFUNCTION(BlueprintCallable, Category = "SC01|Materials")
	void Unbind();

	UFUNCTION(BlueprintCallable, Category = "SC01|Materials")
	bool ApplyCurrentConfiguration();

	UFUNCTION(BlueprintPure, Category = "SC01|Materials")
	FString GetAppliedInteriorFamilyId() const { return AppliedInteriorFamilyId; }

	UFUNCTION(BlueprintPure, Category = "SC01|Materials")
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
	TObjectPtr<USc01V2ConfigurationState> State;

	UPROPERTY(Transient)
	TObjectPtr<USc01MaterialLibrary> Library;

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
