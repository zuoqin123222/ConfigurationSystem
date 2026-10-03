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

/** 右侧承载 480px Web 选配面板；体验控制与画质设置由全屏原生 UI 提供。 */
UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorPanel final : public UUserWidget
{
	GENERATED_BODY()

public:
	static FString GetConfiguredWebUrl();
	static FString BuildHealthUrl(const FString& WebUrl);
	static float GetHealthRetryDelaySeconds(int32 CompletedAttemptCount);
	static bool IsSupportedQuality(const FString& Quality);
	static bool ParseWebConfigurationJson(
		const FString& ConfigurationJson,
		TMap<FString, FString>& OutSelections,
		TMap<FString, FSc01V2Customization>& OutCustomizations,
		FString& OutError);
	static constexpr int32 MaxHealthProbeAttempts = 5;

	void ApplyWebConfigurationJson(const FString& ConfigurationJson);

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
	void BuildExperienceControls(UCanvasPanel* Root);
	UButton* CreateToolbarButton(
		UHorizontalBox* Toolbar,
		FName Name,
		const FText& Label,
		TObjectPtr<UTextBlock>& OutLabel);
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
	void RefreshExperienceControls();
	bool ApplyQualityLevel(const FString& Quality);
	void SetExperienceError(const FString& Error);

	UFUNCTION()
	void ToggleWebConfigurator();

	UFUNCTION()
	void HandleCameraClicked();
	UFUNCTION()
	void HandleAnimationClicked();
	UFUNCTION()
	void HandleLightClicked();
	UFUNCTION()
	void HandleRenderClicked();
	UFUNCTION()
	void HandlePathTracingWarmupStateChanged();
	UFUNCTION()
	void HandleResetClicked();
	UFUNCTION()
	void HandleFullscreenClicked();
	UFUNCTION()
	void HandleQualityMenuClicked();
	UFUNCTION()
	void HandleQualityLowClicked();
	UFUNCTION()
	void HandleQualityMediumClicked();
	UFUNCTION()
	void HandleQualityHighClicked();
	UFUNCTION()
	void HandleQualityEpicClicked();

	UPROPERTY(Transient)
	TObjectPtr<USizeBox> PanelSize;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanelSlot> PanelCanvasSlot;

	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorBrowserWidget> WebBrowser;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ToggleButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToggleLabel;

	UPROPERTY(Transient)
	TObjectPtr<UConfiguratorWebBridge> WebBridge;

	UPROPERTY(Transient)
	TObjectPtr<UButton> AnimationButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> LightButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> RenderButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FullscreenButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CameraLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AnimationLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LightLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RenderLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FullscreenLabel;
	UPROPERTY(Transient)
	TObjectPtr<UBorder> QualityPopup;

	FHttpRequestPtr ActiveHealthRequest;
	FTimerHandle HealthRetryTimer;
	EHealthProbeState HealthProbeState = EHealthProbeState::Idle;
	int32 HealthProbeAttemptCount = 0;
	float PendingRetryDelaySeconds = 0.0f;
	FString ExperienceError;
	bool bExpanded = true;
	bool bQualityMenuOpen = false;
};
