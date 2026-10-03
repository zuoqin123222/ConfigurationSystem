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
	return CategoryId == TEXT("exterior")
		|| CategoryId == TEXT("interior")
		|| CategoryId == TEXT("performance")
		|| CategoryId == TEXT("personalization");
}

void UConfiguratorWebBridge::ApplyConfigurationJson(
	const FString& ConfigurationJson)
{
	if (Owner != nullptr)
	{
		Owner->ApplyWebConfigurationJson(ConfigurationJson);
	}
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
