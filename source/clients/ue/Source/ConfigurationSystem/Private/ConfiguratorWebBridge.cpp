#include "ConfiguratorWebBridge.h"

#include "ConfiguratorPanel.h"

void UConfiguratorWebBridge::Initialize(UConfiguratorPanel* InOwner)
{
	Owner = InOwner;
}

bool UConfiguratorWebBridge::IsSupportedCameraIndex(const int32 CameraIndex)
{
	return CameraIndex >= 0 && CameraIndex <= 5;
}

bool UConfiguratorWebBridge::IsSupportedCameraId(const FString& CameraId)
{
	if (CameraId.IsEmpty() || CameraId.Len() > 64)
	{
		return false;
	}
	if (CameraId.Equals(TEXT("interior"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	for (const TCHAR Character : CameraId)
	{
		if (!FChar::IsLower(Character)
			&& !FChar::IsDigit(Character)
			&& Character != TEXT('-'))
		{
			return false;
		}
	}
	return true;
}

bool UConfiguratorWebBridge::IsSupportedAnimationId(const FString& AnimationId)
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

bool UConfiguratorWebBridge::PlayAnimation(const FString& AnimationId)
{
	return Owner != nullptr
		&& IsSupportedAnimationId(AnimationId)
		&& Owner->PlayExperienceAnimation(AnimationId);
}

bool UConfiguratorWebBridge::CloseAnimation(const FString& AnimationId)
{
	return Owner != nullptr
		&& IsSupportedAnimationId(AnimationId)
		&& Owner->CloseExperienceAnimation(AnimationId);
}

bool UConfiguratorWebBridge::FocusAnimation(const FString& NextAnimationId)
{
	return Owner != nullptr
		&& (NextAnimationId.IsEmpty() || IsSupportedAnimationId(NextAnimationId))
		&& Owner->FocusExperienceAnimation(NextAnimationId);
}

bool UConfiguratorWebBridge::CanPlayAnimation(
	const FString& AnimationId) const
{
	return Owner != nullptr
		&& IsSupportedAnimationId(AnimationId)
		&& Owner->CanPlayExperienceAnimation(AnimationId);
}

FString UConfiguratorWebBridge::GetAnimationExecutorStateJson(
	const FString& AnimationId) const
{
	return Owner != nullptr
		&& IsSupportedAnimationId(AnimationId)
		? Owner->GetAnimationExecutorStateJson(AnimationId)
		: FString();
}

bool UConfiguratorWebBridge::CompleteCefBridgeProbe(
	const FString& ResultJson)
{
	return Owner != nullptr && Owner->CompleteCefBridgeProbe(ResultJson);
}

bool UConfiguratorWebBridge::ReportUiReady(const FString& ViewId)
{
	return Owner != nullptr && Owner->ReportUiReady(ViewId);
}

bool UConfiguratorWebBridge::IsSupportedLightPreset(const FString& Preset)
{
	return Preset == TEXT("studio") || Preset == TEXT("outdoor");
}

bool UConfiguratorWebBridge::IsSupportedRenderMode(const FString& Mode)
{
	return Mode == TEXT("realtime") || Mode == TEXT("path-tracing");
}

bool UConfiguratorWebBridge::IsSupportedQualityLevel(const FString& Quality)
{
	return Quality == TEXT("low")
		|| Quality == TEXT("medium")
		|| Quality == TEXT("high")
		|| Quality == TEXT("epic");
}

bool UConfiguratorWebBridge::IsSupportedConfiguratorCategory(
	const FString& CategoryId)
{
	return CategoryId == TEXT("preset")
		|| CategoryId == TEXT("exterior")
		|| CategoryId == TEXT("interior")
		|| CategoryId == TEXT("performance")
		|| CategoryId == TEXT("personalization")
		|| CategoryId == TEXT("summary");
}

bool UConfiguratorWebBridge::IsSupportedConfiguratorHeaderAction(
	const FString& Action)
{
	return Action == TEXT("save")
		|| Action == TEXT("share")
		|| Action == TEXT("reset");
}

void UConfiguratorWebBridge::ApplyConfigurationJson(
	const FString& ConfigurationJson)
{
	if (Owner != nullptr)
	{
		Owner->ApplyWebConfigurationJson(ConfigurationJson);
	}
}

FString UConfiguratorWebBridge::ApplyConfigurationTransactionJson(
	const FString& ConfigurationJson)
{
	return Owner != nullptr
		? Owner->ApplyWebConfigurationTransactionJson(ConfigurationJson)
		: TEXT("{\"ok\":false,\"code\":\"BRIDGE_OWNER_UNAVAILABLE\","
			"\"message\":\"UE bridge owner unavailable\","
			"\"configurationId\":\"\",\"appliedSurfaceIds\":[],"
			"\"unsupportedSurfaceIds\":[],\"appliedSlotIds\":[]}");
}

FString UConfiguratorWebBridge::GetPresentationStateJson() const
{
	return Owner != nullptr
		? Owner->GetExperienceStateJson()
		: FString();
}

bool UConfiguratorWebBridge::SetCamera(const int32 CameraIndex)
{
	return Owner != nullptr
		&& IsSupportedCameraIndex(CameraIndex)
		&& Owner->SetExperienceCamera(CameraIndex);
}

bool UConfiguratorWebBridge::SetCameraId(const FString& CameraId)
{
	return Owner != nullptr
		&& IsSupportedCameraId(CameraId)
		&& Owner->SetExperienceCameraId(CameraId);
}

bool UConfiguratorWebBridge::SetAnimationEnabled(const bool bEnabled)
{
	return Owner != nullptr && Owner->SetExperienceAnimationEnabled(bEnabled);
}

bool UConfiguratorWebBridge::SetLightPreset(const FString& Preset)
{
	return Owner != nullptr
		&& IsSupportedLightPreset(Preset)
		&& Owner->SetExperienceLightPreset(Preset);
}

bool UConfiguratorWebBridge::SetRenderMode(const FString& Mode)
{
	return Owner != nullptr
		&& IsSupportedRenderMode(Mode)
		&& Owner->SetExperienceRenderMode(Mode);
}

FString UConfiguratorWebBridge::GetRenderModeError() const
{
	return Owner != nullptr ? Owner->GetLastRenderModeError() : FString();
}

bool UConfiguratorWebBridge::SetQualityLevel(const FString& Quality)
{
	return Owner != nullptr
		&& IsSupportedQualityLevel(Quality)
		&& Owner->SetExperienceQualityLevel(Quality);
}

bool UConfiguratorWebBridge::ResetPresentation()
{
	return Owner != nullptr && Owner->ResetExperiencePresentation();
}

bool UConfiguratorWebBridge::SetFullscreen(const bool bEnabled)
{
	return Owner != nullptr && Owner->SetExperienceFullscreen(bEnabled);
}

bool UConfiguratorWebBridge::SetConfiguratorCategory(const FString& CategoryId)
{
	return Owner != nullptr
		&& IsSupportedConfiguratorCategory(CategoryId)
		&& Owner->SetConfiguratorCategory(CategoryId);
}

bool UConfiguratorWebBridge::SetConfiguratorHeaderStateJson(
	const FString& StateJson)
{
	return Owner != nullptr
		&& Owner->SetConfiguratorHeaderStateJson(StateJson);
}

bool UConfiguratorWebBridge::TriggerConfiguratorHeaderAction(
	const FString& Action)
{
	return Owner != nullptr
		&& IsSupportedConfiguratorHeaderAction(Action)
		&& Owner->TriggerConfiguratorHeaderAction(Action);
}

FString UConfiguratorWebBridge::GetConfiguratorHeaderStateJson() const
{
	return Owner != nullptr
		? Owner->GetConfiguratorHeaderStateJson()
		: FString();
}
