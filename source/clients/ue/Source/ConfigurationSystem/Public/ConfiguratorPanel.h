#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Sc01V2ConfigurationState.h"
#include "ConfiguratorPanel.generated.h"

class UCarConfiguratorSubsystem;
class UPathTracingExperienceSubsystem;
class UEditableTextBox;
class USlider;
class UVerticalBox;
class UWrapBox;
class UProgressBar;
class UTextBlock;

DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FConfiguratorDataButtonClicked,
	const FString&,
	const FString&,
	const FString&);

/** 所有动态筛选和选项按钮共用的 payload 绑定，不为 catalog 条目生成专用函数。 */
UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorDataButton final : public UButton
{
	GENERATED_BODY()

public:
	void InitializeBinding(
		const FString& InAction,
		const FString& InPrimaryId,
		const FString& InSecondaryId);

	FConfiguratorDataButtonClicked OnDataClicked;

private:
	UFUNCTION()
	void ForwardClick();

	FString Action;
	FString PrimaryId;
	FString SecondaryId;
};

/** 无 Blueprint 依赖的 catalog 驱动 SC01 v2 UMG，并保留原有体验控制。 */
UCLASS()
class CONFIGURATIONSYSTEM_API UConfiguratorPanel final : public UUserWidget
{
	GENERATED_BODY()

public:
	static constexpr int32 PartCount = 4;
	static constexpr int32 OptionButtonCount = 8;
	static constexpr int32 TemplateButtonCount = 2;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildWidgetTree();
	void BuildSc01V2Controls(UVerticalBox* Parent);
	void AddHeading(UVerticalBox* Parent, const FText& Text);
	class UButton* MakeButton(const FText& Label, FName Handler);
	UConfiguratorDataButton* MakeDataButton(
		const FString& Label,
		const FString& Action,
		const FString& PrimaryId,
		const FString& SecondaryId = FString());
	void RefreshV2Navigation();
	void RefreshCategoryButtons();
	void RefreshSurfaceButtons();
	void RefreshOptionButtons();
	void RefreshVariantButtons();
	void RefreshPaintEditor();
	void QueueV2NavigationRefresh();
	void HandleDeferredV2NavigationRefresh();
	void QueuePaintCommit();
	void CommitPendingPaint();
	void RefreshSummary();

	UFUNCTION()
	void HandleConfigurationChanged();
	UFUNCTION()
	void HandleSc01V2ConfigurationChanged();
	UFUNCTION()
	void HandlePathTracingProgress(int32 CurrentSample, int32 TargetSamples);
	void HandleDataButtonClicked(
		const FString& Action,
		const FString& PrimaryId,
		const FString& SecondaryId);
	UFUNCTION() void HandlePaintHexCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	UFUNCTION() void HandleMetallicChanged(float Value);
	UFUNCTION() void HandleRoughnessChanged(float Value);
	UFUNCTION() void HandleClearCoatChanged(float Value);
	UFUNCTION() void HandleOrangePeelChanged(float Value);
	UFUNCTION() void HandleFlakeIntensityChanged(float Value);
	UFUNCTION() void HandlePaintSliderCaptureEnded();
	UFUNCTION() void ApplySportTemplate();
	UFUNCTION() void ApplyLuxuryTemplate();
	UFUNCTION() void ToggleLeftDoor();
	UFUNCTION() void ToggleRightDoor();
	UFUNCTION() void ToggleHood();
	UFUNCTION() void ToggleTrunk();
	UFUNCTION() void ToggleWheels();
	UFUNCTION() void CameraFront();
	UFUNCTION() void CameraRear();
	UFUNCTION() void CameraLeft();
	UFUNCTION() void CameraRight();
	UFUNCTION() void CameraInterior();
	UFUNCTION() void CameraInteriorPassenger();
	UFUNCTION() void ToggleEnvironment();
	UFUNCTION() void TogglePathTracing();
	UFUNCTION() void SaveExperience();
	UFUNCTION() void LoadExperience();

	UPROPERTY(Transient)
	TObjectPtr<UCarConfiguratorSubsystem> Configurator;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CanonicalKeyText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PriceText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ExperienceStatusText;

	UPROPERTY(Transient)
	TObjectPtr<UProgressBar> PathTracingProgress;

	UPROPERTY(Transient)
	TObjectPtr<UPathTracingExperienceSubsystem> PathTracing;

	UPROPERTY(Transient)
	TObjectPtr<USc01V2ConfigurationState> Sc01V2State;

	UPROPERTY(Transient)
	TObjectPtr<UWrapBox> RegionButtons;
	UPROPERTY(Transient)
	TObjectPtr<UWrapBox> CategoryButtons;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> SurfaceButtons;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> OptionButtons;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> VariantButtons;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PaintEditor;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> PaintHex;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> V2StatusText;

	UPROPERTY(Transient) TObjectPtr<USlider> MetallicSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> RoughnessSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> ClearCoatSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> OrangePeelSlider;
	UPROPERTY(Transient) TObjectPtr<USlider> FlakeIntensitySlider;

	FString SelectedRegionId;
	FString SelectedCategoryId;
	FString SelectedSurfaceId;
	FSc01V2PaintCustomization PendingPaint;
	FTimerHandle V2NavigationRefreshTimer;
	double NextPaintCommitSeconds = 0.0;
	bool bV2NavigationRefreshPending = false;
	bool bPaintCommitPending = false;
	bool bRefreshingPaintEditor = false;
};
