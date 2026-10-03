#include "ShowroomEnvironmentActor.h"

#include "Engine/LevelStreaming.h"
#include "Engine/World.h"

const FName AShowroomEnvironmentActor::EnvironmentActorTag(
	TEXT("Configurator.EnvironmentController"));
const FName AShowroomEnvironmentActor::TemporaryEnvironmentTag(
	TEXT("Configurator.Environment.Temporary"));
const FName AShowroomEnvironmentActor::StudioLevelName(
	TEXT("/Game/Maps/L_Lighting_Studio"));
const FName AShowroomEnvironmentActor::OutdoorLevelName(
	TEXT("/Game/Maps/L_Lighting_Outdoor"));

AShowroomEnvironmentActor::AShowroomEnvironmentActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Tags.Add(EnvironmentActorTag);
	Tags.Add(TemporaryEnvironmentTag);
}

void AShowroomEnvironmentActor::BeginPlay()
{
	Super::BeginPlay();
	SetEnvironmentIndex(EnvironmentIndex);
}

void AShowroomEnvironmentActor::SetEnvironmentIndex(const int32 InIndex)
{
	EnvironmentIndex = FMath::Clamp(InIndex, 0, 1);
	if (GetWorld() == nullptr)
	{
		return;
	}

	UWorld* World = GetWorld();
	const FName TargetLevel =
		EnvironmentIndex == 0 ? StudioLevelName : OutdoorLevelName;
	const FName PreviousLevel =
		EnvironmentIndex == 0 ? OutdoorLevelName : StudioLevelName;
	ULevelStreaming* TargetStreaming = nullptr;
	ULevelStreaming* PreviousStreaming = nullptr;
	for (ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!IsValid(StreamingLevel))
		{
			continue;
		}
		const FName PackageName = StreamingLevel->GetWorldAssetPackageFName();
		if (PackageName == TargetLevel)
		{
			TargetStreaming = StreamingLevel;
		}
		else if (PackageName == PreviousLevel)
		{
			PreviousStreaming = StreamingLevel;
		}
	}
	if (TargetStreaming == nullptr || PreviousStreaming == nullptr)
	{
		return;
	}

	// 先阻塞载入目标场景，再卸载旧场景，避免切换期间出现无地面、无灯光帧。
	TargetStreaming->SetShouldBeLoaded(true);
	TargetStreaming->SetShouldBeVisible(true);
	World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
	PreviousStreaming->SetShouldBeVisible(false);
	PreviousStreaming->SetShouldBeLoaded(false);
	World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
}

void AShowroomEnvironmentActor::ToggleEnvironment()
{
	SetEnvironmentIndex(EnvironmentIndex == 0 ? 1 : 0);
}
