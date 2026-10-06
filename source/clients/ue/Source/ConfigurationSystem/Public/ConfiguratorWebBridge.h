#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConfiguratorWebBridge.generated.h"

class UConfiguratorPanel;

/**
 * 内嵌网页的最小权限桥。仅接受经过白名单解析的车型目录 v2 配置 JSON；
 * 不把 PlayerController、World 或通用执行入口暴露给 JavaScript。
 */
UCLASS(NotBlueprintable, Transient)
class CONFIGURATIONSYSTEM_API UConfiguratorWebBridge final : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UConfiguratorPanel* InOwner);
	static bool IsSupportedCameraIndex(int32 CameraIndex);
	static bool IsSupportedCameraId(const FString& CameraId);
	static bool IsSupportedAnimationId(const FString& AnimationId);
	static bool IsSupportedLightPreset(const FString& Preset);
	static bool IsSupportedRenderMode(const FString& Mode);
	static bool IsSupportedQualityLevel(const FString& Quality);
	static bool IsSupportedConfiguratorCategory(const FString& CategoryId);
	static bool IsSupportedConfiguratorHeaderAction(const FString& Action);

	UFUNCTION()
	void ApplyConfigurationJson(const FString& ConfigurationJson);
	UFUNCTION()
	FString GetPresentationStateJson() const;
	UFUNCTION()
	bool SetCamera(int32 CameraIndex);
	UFUNCTION()
	bool SetCameraId(const FString& CameraId);
	UFUNCTION()
	bool SetAnimationEnabled(bool bEnabled);
	UFUNCTION()
	bool PlayAnimation(const FString& AnimationId);
	UFUNCTION()
	bool CloseAnimation(const FString& AnimationId);
	UFUNCTION()
	bool FocusAnimation(const FString& NextAnimationId);
	UFUNCTION()
	bool CanPlayAnimation(const FString& AnimationId) const;
	UFUNCTION()
	bool SetLightPreset(const FString& Preset);
	UFUNCTION()
	bool SetRenderMode(const FString& Mode);
	UFUNCTION()
	FString GetRenderModeError() const;
	UFUNCTION()
	bool SetQualityLevel(const FString& Quality);
	UFUNCTION()
	bool ResetPresentation();
	UFUNCTION()
	bool SetFullscreen(bool bEnabled);
	UFUNCTION()
	bool SetConfiguratorCategory(const FString& CategoryId);
	UFUNCTION()
	bool SetConfiguratorHeaderStateJson(const FString& StateJson);
	UFUNCTION()
	bool TriggerConfiguratorHeaderAction(const FString& Action);
	UFUNCTION()
	FString GetConfiguratorHeaderStateJson() const;

private:
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorPanel> Owner;
};
