#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HttpFwd.h"
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
	static FString BuildHealthUrl(const FString& WebUrl);
	static float GetHealthRetryDelaySeconds(int32 CompletedAttemptCount);
	static bool ParseWebConfigurationJson(
		const FString& ConfigurationJson,
		TMap<FString, FString>& OutSelections,
		TMap<FString, FAutomotiveCustomization>& OutCustomizations,
		FString& OutError);
	static bool IsValidConfiguratorHeaderStateJson(
		const FString& StateJson,
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
	bool SetConfiguratorCategory(const FString& CategoryId);
	bool SetConfiguratorHeaderStateJson(const FString& StateJson);
	bool TriggerConfiguratorHeaderAction(const FString& Action);
	FString GetConfiguratorHeaderStateJson() const;
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
	void StartHealthProbe();
	void IssueHealthProbe();
	void HandleHealthProbeCompleted(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bConnectedSuccessfully);
	void HandleHealthProbeFailure();
	void CancelHealthProbe();
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

	FHttpRequestPtr ActiveHealthRequest;
	FTimerHandle HealthRetryTimer;
	EHealthProbeState HealthProbeState = EHealthProbeState::Idle;
	int32 HealthProbeAttemptCount = 0;
	float PendingRetryDelaySeconds = 0.0f;
	bool bWebFullscreen = false;
	FString LatestConfiguratorHeaderStateJson;
};
