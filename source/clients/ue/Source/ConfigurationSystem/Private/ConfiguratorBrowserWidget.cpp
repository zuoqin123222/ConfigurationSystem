#include "ConfiguratorBrowserWidget.h"

#include "ConfiguratorWebBridge.h"
#include "SWebBrowser.h"

void UConfiguratorBrowserWidget::SetBridge(UConfiguratorWebBridge* InBridge)
{
	Bridge = InBridge;
	if (WebBrowserWidget.IsValid() && Bridge != nullptr)
	{
		WebBrowserWidget->BindUObject(TEXT("ueBridge"), Bridge, true);
	}
}

TSharedRef<SWidget> UConfiguratorBrowserWidget::RebuildWidget()
{
	TSharedRef<SWidget> Widget = Super::RebuildWidget();
	if (WebBrowserWidget.IsValid() && Bridge != nullptr)
	{
		WebBrowserWidget->BindUObject(TEXT("ueBridge"), Bridge, true);
	}
	return Widget;
}

void UConfiguratorBrowserWidget::ReleaseSlateResources(const bool bReleaseChildren)
{
	if (WebBrowserWidget.IsValid() && Bridge != nullptr)
	{
		WebBrowserWidget->UnbindUObject(TEXT("ueBridge"), Bridge, true);
	}
	Super::ReleaseSlateResources(bReleaseChildren);
}
