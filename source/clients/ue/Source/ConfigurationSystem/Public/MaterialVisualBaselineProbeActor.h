#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MaterialVisualBaselineProbeActor.generated.h"

class UAutomotiveMaterialLibrary;
class UMaterialInterface;
class UStaticMeshComponent;
class FViewport;

USTRUCT()
struct FMaterialVisualBaselineVariant
{
	GENERATED_BODY()

	FString VariantId;
	TSoftObjectPtr<UMaterialInterface> Material;
};

/**
 * 固定材质视觉基准场景中的测试平面与 352 variant 截图探针。
 * Runtime 通过命令行自动启动；Editor 由 EditorSubsystem 驱动同一状态机。
 */
UCLASS()
class CONFIGURATIONSYSTEM_API AMaterialVisualBaselineProbeActor final : public AActor
{
	GENERATED_BODY()

public:
	AMaterialVisualBaselineProbeActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static constexpr int32 ExpectedVariantCount = 352;
	static constexpr float DefaultStableWaitSeconds = 0.5f;
	static constexpr int32 RequiredStableFrames = 3;
	static constexpr TCHAR LibraryObjectPath[] =
		TEXT("/Game/SC01/Materials/DA_SC01MaterialLibrary."
			"DA_SC01MaterialLibrary");

	static bool BuildVariantPlan(
		const UAutomotiveMaterialLibrary* Library,
		TArray<FMaterialVisualBaselineVariant>& OutVariants,
		FString& OutError);

	bool StartProbe(
		const FString& InMode,
		const FString& InOutputDirectory,
		float InStableWaitSeconds,
		bool bInExitOnComplete);
	bool AdvanceProbe(float DeltaSeconds, FViewport* Viewport);
	bool IsProbeRunning() const;
	bool DidProbeSucceed() const;
	const FString& GetManifestPath() const;

	UStaticMeshComponent* GetPlaneComponent() const
	{
		return Plane;
	}

private:
	bool PrepareVariant(FString& OutError);
	bool CaptureVariant(FViewport& Viewport, FString& OutError);
	bool WriteManifest(const FString& FatalError);
	void Finish(const FString& FatalError);
	void AppendRemainingFailures(const FString& Error);
	static FString MakeSafeFilename(const FString& VariantId);

	UPROPERTY(VisibleAnywhere, Category = "Material Visual Baseline")
	TObjectPtr<UStaticMeshComponent> Plane;

	UPROPERTY(Transient)
	TObjectPtr<UAutomotiveMaterialLibrary> Library;

	TArray<FMaterialVisualBaselineVariant> Variants;
	TArray<TSharedPtr<class FJsonValue>> Results;
	FString Mode;
	FString OutputDirectory;
	FString ManifestPath;
	FString CurrentVariantId;
	FString CurrentMaterialPath;
	double TotalElapsedSeconds = 0.0;
	double VariantElapsedSeconds = 0.0;
	float StableWaitSeconds = DefaultStableWaitSeconds;
	int32 StableFrameCount = 0;
	int32 CurrentVariantIndex = 0;
	bool bExitOnComplete = false;
	bool bRunning = false;
	bool bSucceeded = false;
	bool bVariantPrepared = false;
};
