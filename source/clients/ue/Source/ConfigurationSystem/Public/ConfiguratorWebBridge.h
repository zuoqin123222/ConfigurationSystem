#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConfiguratorWebBridge.generated.h"

class UConfiguratorPanel;

/**
 * 内嵌网页的最小权限桥。仅接受经过白名单解析的 SC01 v2 配置 JSON；
 * 不把 PlayerController、World 或通用执行入口暴露给 JavaScript。
 */
UCLASS(NotBlueprintable, Transient)
class CONFIGURATIONSYSTEM_API UConfiguratorWebBridge final : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UConfiguratorPanel* InOwner);

	UFUNCTION()
	void ApplyConfigurationJson(const FString& ConfigurationJson);

private:
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorPanel> Owner;
};
