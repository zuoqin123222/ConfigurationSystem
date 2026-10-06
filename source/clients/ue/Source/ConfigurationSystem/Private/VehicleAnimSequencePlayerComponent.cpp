#include "VehicleAnimSequencePlayerComponent.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

UVehicleAnimSequencePlayerComponent::UVehicleAnimSequencePlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UVehicleAnimSequencePlayerComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceAnimation(DeltaTime);
}

void UVehicleAnimSequencePlayerComponent::BindMesh(USkeletalMeshComponent* InMesh)
{
	Mesh = InMesh;
	if (IsValid(Mesh))
	{
		Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Mesh->bPauseAnims = true;
	}
}

bool UVehicleAnimSequencePlayerComponent::SetSequenceAndClips(
	UAnimSequence* InSequence,
	const TArray<FVehicleAnimationClip>& InClips)
{
	Sequence = nullptr;
	Clips.Reset();
	ActiveAnimationId = NAME_None;
	PendingAnimationId = NAME_None;
	bPlaying = false;
	bClosing = false;
	bHasPendingFocus = false;
	FrameAccumulator = 0.0;

	const USkeletalMesh* SkeletalMesh = IsValid(Mesh)
		? Mesh->GetSkeletalMeshAsset()
		: nullptr;
	if (!IsValid(InSequence) || SkeletalMesh == nullptr
		|| InSequence->GetSkeleton() != SkeletalMesh->GetSkeleton())
	{
		return false;
	}

	const int32 MaxFrame = InSequence->GetNumberOfSampledKeys() - 1;
	TArray<FVehicleAnimationClip> NormalizedClips = InClips;
	for (FVehicleAnimationClip& Clip : NormalizedClips)
	{
		if (!IsStableAnimationId(Clip.AnimationId.ToString()) || Clip.FrameRate <= 0.0f
			|| !NormalizeFrameRange(Clip.StartFrame, Clip.EndFrame, MaxFrame))
		{
			return false;
		}
	}
	Sequence = InSequence;
	Clips = MoveTemp(NormalizedClips);
	return !Clips.IsEmpty();
}

const FVehicleAnimationClip*
UVehicleAnimSequencePlayerComponent::FindClip(const FName AnimationId) const
{
	return Clips.FindByPredicate([AnimationId](const FVehicleAnimationClip& Clip)
	{
		return Clip.AnimationId == AnimationId;
	});
}

bool UVehicleAnimSequencePlayerComponent::HasAnimationById(
	const FName AnimationId) const
{
	const FVehicleAnimationClip* Clip = FindClip(AnimationId);
	return Clip != nullptr
		&& IsValid(Sequence)
		&& IsValid(Mesh)
		&& Clip->FrameRate > 0.0f
		&& Clip->EndFrame > Clip->StartFrame;
}

bool UVehicleAnimSequencePlayerComponent::PlayAnimationById(
	const FName AnimationId)
{
	const FVehicleAnimationClip* Clip = FindClip(AnimationId);
	if (Clip == nullptr || !IsValid(Sequence)
		|| !IsValid(Mesh) || Clip->FrameRate <= 0.0f
		|| Clip->EndFrame <= Clip->StartFrame)
	{
		return false;
	}

	ActiveAnimationId = AnimationId;
	CurrentFrame = Clip->StartFrame;
	Direction = 1;
	FrameAccumulator = 0.0;
	bClosing = false;
	bPlaying = true;
	Mesh->SetAnimation(Sequence);
	ApplyCurrentFrame();
	return true;
}

bool UVehicleAnimSequencePlayerComponent::FocusAnimationById(
	const FName NextAnimationId)
{
	if (!NextAnimationId.IsNone() && FindClip(NextAnimationId) == nullptr)
	{
		return false;
	}
	if (NextAnimationId == ActiveAnimationId)
	{
		PendingAnimationId = NAME_None;
		bHasPendingFocus = false;
		if (bClosing)
		{
			bClosing = false;
			Direction = 1;
			bPlaying = true;
		}
		return true;
	}
	if (ActiveAnimationId.IsNone())
	{
		return NextAnimationId.IsNone() || PlayAnimationById(NextAnimationId);
	}

	PendingAnimationId = NextAnimationId;
	bHasPendingFocus = true;
	if (bClosing)
	{
		return true;
	}
	if (!CloseAnimationById(ActiveAnimationId))
	{
		PendingAnimationId = NAME_None;
		bHasPendingFocus = false;
		return false;
	}
	if (ActiveAnimationId.IsNone())
	{
		StartPendingAnimation();
	}
	return true;
}

bool UVehicleAnimSequencePlayerComponent::CloseAnimationById(
	const FName AnimationId)
{
	const FVehicleAnimationClip* Clip = FindClip(AnimationId);
	if (Clip == nullptr || ActiveAnimationId != AnimationId)
	{
		return false;
	}

	switch (Clip->CloseMode)
	{
	case EVehicleAnimationCloseMode::Reverse:
		CurrentFrame = ResolveCloseFrame(
			CurrentFrame, Clip->StartFrame, Clip->CloseMode, bClosing);
		if (!bClosing)
		{
			StopAtFrame(Clip->StartFrame, true);
		}
		else
		{
			Direction = -1;
			bPlaying = true;
		}
		break;
	case EVehicleAnimationCloseMode::ResetToStart:
	case EVehicleAnimationCloseMode::Stop:
	default:
		CurrentFrame = ResolveCloseFrame(
			CurrentFrame, Clip->StartFrame, Clip->CloseMode, bClosing);
		StopAtFrame(CurrentFrame, true);
		break;
	}
	return true;
}

void UVehicleAnimSequencePlayerComponent::FreezeAnimation()
{
	bPlaying = false;
	bClosing = false;
	bHasPendingFocus = false;
	PendingAnimationId = NAME_None;
	ActiveAnimationId = NAME_None;
	FrameAccumulator = 0.0;
}

int32 UVehicleAnimSequencePlayerComponent::ResolveCloseFrame(
	const int32 Current,
	const int32 Start,
	const EVehicleAnimationCloseMode CloseMode,
	bool& bOutReverse)
{
	bOutReverse = CloseMode == EVehicleAnimationCloseMode::Reverse
		&& Current > Start;
	return CloseMode == EVehicleAnimationCloseMode::ResetToStart
		? Start
		: Current;
}

int32 UVehicleAnimSequencePlayerComponent::AdvanceFrame(
	const int32 Current,
	const int32 Start,
	const int32 End,
	const EVehicleAnimationLoopMode LoopMode,
	int32& InOutDirection,
	bool& bOutStopped)
{
	bOutStopped = false;
	const int32 SafeDirection = InOutDirection < 0 ? -1 : 1;
	const int32 Next = Current + SafeDirection;
	if (LoopMode == EVehicleAnimationLoopMode::Forward)
	{
		InOutDirection = 1;
		return Next > End ? Start : Next;
	}
	if (LoopMode == EVehicleAnimationLoopMode::PingPong)
	{
		if (Next >= End)
		{
			InOutDirection = -1;
			return End;
		}
		if (Next <= Start)
		{
			InOutDirection = 1;
			return Start;
		}
		return Next;
	}
	if (Next >= End)
	{
		InOutDirection = 1;
		bOutStopped = true;
		return End;
	}
	return Next;
}

bool UVehicleAnimSequencePlayerComponent::NormalizeFrameRange(
	int32& InOutStart,
	int32& InOutEnd,
	const int32 MaxFrame)
{
	if (MaxFrame < 1)
	{
		return false;
	}
	InOutStart = FMath::Clamp(InOutStart, 0, MaxFrame);
	InOutEnd = FMath::Clamp(InOutEnd, 0, MaxFrame);
	return InOutEnd > InOutStart;
}

bool UVehicleAnimSequencePlayerComponent::IsStableAnimationId(
	const FString& AnimationId)
{
	if (AnimationId.IsEmpty() || AnimationId.Len() > 64
		|| AnimationId.StartsWith(TEXT("-"))
		|| AnimationId.EndsWith(TEXT("-")))
	{
		return false;
	}
	bool bPreviousWasDash = false;
	for (const TCHAR Character : AnimationId)
	{
		const bool bIsDash = Character == TEXT('-');
		if ((!FChar::IsLower(Character)
				&& !FChar::IsDigit(Character)
				&& !bIsDash)
			|| (bIsDash && bPreviousWasDash))
		{
			return false;
		}
		bPreviousWasDash = bIsDash;
	}
	return true;
}

void UVehicleAnimSequencePlayerComponent::AdvanceAnimation(
	const float DeltaSeconds)
{
	const FVehicleAnimationClip* Clip = FindClip(ActiveAnimationId);
	if (!bPlaying || Clip == nullptr || DeltaSeconds <= 0.0f)
	{
		return;
	}

	FrameAccumulator += static_cast<double>(DeltaSeconds) * Clip->FrameRate;
	while (FrameAccumulator >= 1.0 && bPlaying)
	{
		FrameAccumulator -= 1.0;
		if (bClosing)
		{
			CurrentFrame = FMath::Max(CurrentFrame - 1, Clip->StartFrame);
			if (CurrentFrame == Clip->StartFrame)
			{
				StopAtFrame(CurrentFrame, true);
				StartPendingAnimation();
			}
		}
		else
		{
			bool bStopped = false;
			CurrentFrame = AdvanceFrame(
				CurrentFrame,
				Clip->StartFrame,
				Clip->EndFrame,
				Clip->LoopMode,
				Direction,
				bStopped);
			if (bStopped)
			{
				StopAtFrame(CurrentFrame);
			}
		}
		ApplyCurrentFrame();
	}
}

void UVehicleAnimSequencePlayerComponent::StartPendingAnimation()
{
	if (!bHasPendingFocus)
	{
		return;
	}
	const FName NextAnimationId = PendingAnimationId;
	PendingAnimationId = NAME_None;
	bHasPendingFocus = false;
	if (!NextAnimationId.IsNone())
	{
		PlayAnimationById(NextAnimationId);
	}
}

void UVehicleAnimSequencePlayerComponent::ApplyCurrentFrame()
{
	const FVehicleAnimationClip* Clip = FindClip(ActiveAnimationId);
	if (!IsValid(Mesh) || Clip == nullptr || !IsValid(Sequence))
	{
		return;
	}
	const float Position = FMath::Clamp(
		static_cast<float>(CurrentFrame) / Clip->FrameRate,
		0.0f,
		Sequence->GetPlayLength());
	Mesh->SetPosition(Position, false);
	Mesh->TickAnimation(0.0f, false);
	Mesh->RefreshBoneTransforms();
}

void UVehicleAnimSequencePlayerComponent::StopAtFrame(
	const int32 Frame,
	const bool bClearActive)
{
	CurrentFrame = Frame;
	bPlaying = false;
	bClosing = false;
	FrameAccumulator = 0.0;
	ApplyCurrentFrame();
	if (bClearActive)
	{
		ActiveAnimationId = NAME_None;
	}
}
