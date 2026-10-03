#pragma once

#include "CoreMinimal.h"
#include "WebBrowser.h"
#include "ConfiguratorBrowserWidget.generated.h"

class UConfiguratorWebBridge;

/** UWebBrowser 的窄扩展，仅用于绑定专用最小权限 bridge。 */
UCLASS(NotBlueprintable, Transient)
class CONFIGURATIONSYSTEM_API UConfiguratorBrowserWidget final : public UWebBrowser
{
	GENERATED_BODY()

public:
	void SetBridge(UConfiguratorWebBridge* InBridge);
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorWebBridge> Bridge;
};
