#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HttpFwd.h"
#include "Sc01V2ConfigurationState.h"
#include "ConfiguratorPanel.generated.h"

class UBorder;
class UButton;
class UCanvasPanel;
class UConfiguratorBrowserWidget;
class UHorizontalBox;
class USizeBox;
class UTextBlock;
class UCanvasPanelSlot;
class UConfiguratorWebBridge;

/** 右侧承载 480px Web 选配面板，并以透明 WebBrowser 承载底部体验控制层。 */
UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorPanel final : public UUserWidget
{
	GENERATED_BODY()

public:
	static FString GetConfiguredWebUrl();
	static FString GetControlsWebUrl();
	static FString BuildHealthUrl(const FString& WebUrl);
	static float GetHealthRetryDelaySeconds(int32 CompletedAttemptCount);
	static bool ParseWebConfigurationJson(
		const FString& ConfigurationJson,
		TMap<FString, FString>& OutSelections,
		TMap<FString, FSc01V2Customization>& OutCustomizations,
		FString& OutError);
	static constexpr int32 MaxHealthProbeAttempts = 5;

	void ApplyWebConfigurationJson(const FString& ConfigurationJson);
	bool SetExperienceCamera(int32 CameraIndex);
	bool SetExperienceAnimationEnabled(bool bEnabled);
	bool SetExperienceLightPreset(const FString& Preset);
	bool SetExperienceRenderMode(const FString& Mode);
	bool SetExperienceQualityLevel(const FString& Quality);
	bool ResetExperiencePresentation();
	bool SetExperienceFullscreen(bool bEnabled);
	FString GetExperienceStateJson();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	enum class EHealthProbeState : uint8
	{
		Idle,
		Waiting,
		Ready,
		Failed
	};

	void BuildWidgetTree();
	void ApplyExpandedState(bool bShouldExpand, bool bReloadPage);
	void StartHealthProbe();
	void IssueHealthProbe();
	void HandleHealthProbeCompleted(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully);
	void HandleHealthProbeFailure();
	void CancelHealthProbe();
	void RefreshToggleLabel();

	UFUNCTION()
	void ToggleWebConfigurator();

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PanelSize;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanelSlot> PanelCanvasSlot;

	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorBrowserWidget> WebBrowser;
	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorBrowserWidget> ControlsBrowser;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ToggleButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToggleLabel;

	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorWebBridge> WebBridge;

	FHttpRequestPtr ActiveHealthRequest;
	FTimerHandle HealthRetryTimer;
	EHealthProbeState HealthProbeState = EHealthProbeState::Idle;
	int32 HealthProbeAttemptCount = 0;
	float PendingRetryDelaySeconds = 0.0f;
	bool bExpanded = true;
};
