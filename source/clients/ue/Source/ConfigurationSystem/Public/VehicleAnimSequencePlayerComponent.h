#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VehicleAnimSequencePlayerComponent.generated.h"

class UAnimSequence;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EVehicleAnimationLoopMode : uint8
{
	None,
	Forward,
	PingPong
};

UENUM(BlueprintType)
enum class EVehicleAnimationCloseMode : uint8
{
	Reverse,
	ResetToStart,
	Stop
};

USTRUCT(BlueprintType)
struct CONFIGURATIONSYSTEM_API FVehicleAnimationClip
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName AnimationId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.001"))
	float FrameRate = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
	int32 StartFrame = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 EndFrame = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EVehicleAnimationLoopMode LoopMode = EVehicleAnimationLoopMode::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EVehicleAnimationCloseMode CloseMode = EVehicleAnimationCloseMode::Reverse;
};

/**
 * 以离散帧驱动一条完整 AnimSequence 的多个帧段。组件不依赖 AnimBP，
 * 每次采样都明确写入当前位置，避免 CEF 开关与引擎播放状态漂移。
 */
UCLASS(ClassGroup = (Configurator), meta = (BlueprintSpawnableComponent))
class CONFIGURATIONSYSTEM_API UVehicleAnimSequencePlayerComponent final
	: public UActorComponent
{
	GENERATED_BODY()

public:
	UVehicleAnimSequencePlayerComponent();

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	void BindMesh(USkeletalMeshComponent* InMesh);
	bool SetSequenceAndClips(
		UAnimSequence* InSequence,
		const TArray<FVehicleAnimationClip>& InClips);

	UFUNCTION(BlueprintCallable, Category = "Vehicle Animation")
	bool PlayAnimationById(FName AnimationId);

	UFUNCTION(BlueprintCallable, Category = "Vehicle Animation")
	bool CloseAnimationById(FName AnimationId);

	/** 原子切换动画焦点；NAME_None 表示关闭。旧动画反播完成后才播放最新请求。 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle Animation")
	bool FocusAnimationById(FName NextAnimationId);

	UFUNCTION(BlueprintPure, Category = "Vehicle Animation")
	bool HasAnimationById(FName AnimationId) const;

	/** 立即停止采样并丢弃待播请求，保留当前骨骼姿态。 */
	UFUNCTION(BlueprintCallable, Category = "Vehicle Animation")
	void FreezeAnimation();

	void AdvanceAnimation(float DeltaSeconds);

	static int32 AdvanceFrame(
		int32 Current,
		int32 Start,
		int32 End,
		EVehicleAnimationLoopMode LoopMode,
		int32& InOutDirection,
		bool& bOutStopped);

	static int32 ResolveCloseFrame(
		int32 Current,
		int32 Start,
		EVehicleAnimationCloseMode CloseMode,
		bool& bOutReverse);

	static bool NormalizeFrameRange(
		int32& InOutStart,
		int32& InOutEnd,
		int32 MaxFrame);

	static bool IsStableAnimationId(const FString& AnimationId);

	UFUNCTION(BlueprintPure, Category = "Vehicle Animation")
	FName GetActiveAnimationId() const { return ActiveAnimationId; }

	/** UI 状态使用：切焦反播期间立即返回最新目标；NAME_None 也可表示待关闭。 */
	UFUNCTION(BlueprintPure, Category = "Vehicle Animation")
	FName GetFocusedAnimationId() const
	{
		return bHasPendingFocus ? PendingAnimationId : ActiveAnimationId;
	}

	UFUNCTION(BlueprintPure, Category = "Vehicle Animation")
	bool IsPlaying() const { return bPlaying; }

	UFUNCTION(BlueprintPure, Category = "Vehicle Animation")
	int32 GetCurrentFrame() const { return CurrentFrame; }

	UFUNCTION(BlueprintPure, Category = "Vehicle Animation")
	int32 GetDirection() const { return Direction; }

private:
	const FVehicleAnimationClip* FindClip(FName AnimationId) const;
	void ApplyCurrentFrame();
	void StopAtFrame(int32 Frame, bool bClearActive = false);
	void StartPendingAnimation();

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Sequence;

	UPROPERTY(Transient)
	TArray<FVehicleAnimationClip> Clips;

	FName ActiveAnimationId;
	FName PendingAnimationId;
	double FrameAccumulator = 0.0;
	int32 CurrentFrame = 0;
	int32 Direction = 1;
	bool bPlaying = false;
	bool bClosing = false;
	bool bHasPendingFocus = false;
};
