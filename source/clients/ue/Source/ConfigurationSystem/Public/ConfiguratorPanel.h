#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AutomotiveConfigurationState.h"
#include "ConfiguratorPanel.generated.h"

class UBorder;
class UCanvasPanel;
class UConfiguratorBrowserWidget;
class USizeBox;
class UCanvasPanelSlot;
class UConfiguratorWebBridge;
class UWidget;

/** 承载顶部 Web 导航、右侧 480px 选配面板和透明底部体验控制层。 */
UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorPanel final : public UUserWidget
{
	GENERATED_BODY()

public:
	static FString GetConfiguredWebUrl();
	static FString GetControlsWebUrl();
	static FString GetHeaderWebUrl();
	static bool ParseWebConfigurationJson(
		const FString& ConfigurationJson,
		TMap<FString, FString>& OutSelections,
		TMap<FString, FAutomotiveCustomization>& OutCustomizations,
		FString& OutError);
	static bool IsValidConfiguratorHeaderStateJson(
		const FString& StateJson,
		FString& OutError);
	/** 将舞台左右遮挡宽度换算为 UE 非对称投影偏移。 */
	static float CalculateStageProjectionOffsetX(
		float ViewportWidth,
		float LeftInset,
		float RightInset);

	void ApplyWebConfigurationJson(const FString& ConfigurationJson);
	bool SetExperienceCamera(int32 CameraIndex);
	bool SetExperienceCameraId(const FString& CameraId);
	bool SetExperienceAnimationEnabled(bool bEnabled);
	bool PlayExperienceAnimation(const FString& AnimationId);
	bool CloseExperienceAnimation(const FString& AnimationId);
	bool FocusExperienceAnimation(const FString& NextAnimationId);
	bool CanPlayExperienceAnimation(const FString& AnimationId) const;
	bool SetExperienceLightPreset(const FString& Preset);
	bool SetExperienceRenderMode(const FString& Mode);
	bool SetExperienceQualityLevel(const FString& Quality);
	bool ResetExperiencePresentation();
	bool SetExperienceFullscreen(bool bEnabled);
	bool SetConfiguratorCategory(const FString& CategoryId);
	bool SetConfiguratorHeaderStateJson(const FString& StateJson);
	bool TriggerConfiguratorHeaderAction(const FString& Action);
	FString GetConfiguratorHeaderStateJson() const;
	FString GetLastRenderModeError() const { return LastRenderModeError; }
	FString GetExperienceStateJson();

	/**
	 * 返回左侧可见舞台中心相对全窗口中心的投影补偿。
	 * 普通模式考虑右侧 480px 面板；全屏模式返回 0。
	 */
	float GetStageProjectionOffsetX() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BuildWidgetTree();
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorBrowserWidget> WebBrowser;
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorBrowserWidget> ControlsBrowser;
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorBrowserWidget> HeaderBrowser;
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanelSlot> ControlsCanvasSlot;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> PageMasks;

	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorWebBridge> WebBridge;

	bool bWebFullscreen = false;
	FString LatestConfiguratorHeaderStateJson;
	FString LastRenderModeError;
};
