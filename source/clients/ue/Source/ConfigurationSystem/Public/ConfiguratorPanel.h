#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HttpFwd.h"
#include "ConfiguratorPanel.generated.h"

class UButton;
class USizeBox;
class UTextBlock;
class UWebBrowser;
class UCanvasPanelSlot;

/** UE 端只承载右侧内嵌 Web 选配面板，不保留重复的原生选配控件。 */
UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorPanel final : public UUserWidget
{
	GENERATED_BODY()

public:
	static FString GetConfiguredWebUrl();
	static FString BuildHealthUrl(const FString& WebUrl);
	static float GetHealthRetryDelaySeconds(int32 CompletedAttemptCount);
	static constexpr int32 MaxHealthProbeAttempts = 5;

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
	TObjectPtr<UWebBrowser> WebBrowser;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ToggleButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToggleLabel;

	FHttpRequestPtr ActiveHealthRequest;
	FTimerHandle HealthRetryTimer;
	EHealthProbeState HealthProbeState = EHealthProbeState::Idle;
	int32 HealthProbeAttemptCount = 0;
	float PendingRetryDelaySeconds = 0.0f;
	bool bExpanded = true;
};
