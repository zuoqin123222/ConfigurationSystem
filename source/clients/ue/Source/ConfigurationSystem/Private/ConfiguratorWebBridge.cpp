#include "ConfiguratorWebBridge.h"

#include "ConfiguratorPanel.h"

void UConfiguratorWebBridge::Initialize(UConfiguratorPanel* InOwner)
{
	Owner = InOwner;
}

void UConfiguratorWebBridge::ApplyConfigurationJson(
	const FString& ConfigurationJson)
{
	if (Owner != nullptr)
	{
		Owner->ApplyWebConfigurationJson(ConfigurationJson);
	}
}
