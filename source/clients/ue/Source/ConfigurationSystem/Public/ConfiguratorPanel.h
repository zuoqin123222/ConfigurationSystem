#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ConfiguratorPanel.generated.h"

class UCarConfiguratorSubsystem;
class UTextBlock;

/** 无 Blueprint 依赖的最小 UMG 面板：四分区各两个选项、两个模板、键与价格。 */
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

private:
	void BuildWidgetTree();
	void AddHeading(class UVerticalBox* Parent, const FText& Text);
	void AddOptionRow(
		class UVerticalBox* Parent,
		const FText& PartName,
		const FText& FirstName,
		const FText& SecondName,
		FName FirstHandler,
		FName SecondHandler);
	class UButton* MakeButton(const FText& Label, FName Handler);
	void RefreshSummary();
	void Select(const TCHAR* PartId, const TCHAR* OptionId);

	UFUNCTION()
	void HandleConfigurationChanged();
	UFUNCTION() void SelectPaintRed();
	UFUNCTION() void SelectPaintSilver();
	UFUNCTION() void SelectWheelSport();
	UFUNCTION() void SelectWheelForged();
	UFUNCTION() void SelectInteriorDark();
	UFUNCTION() void SelectInteriorIvory();
	UFUNCTION() void SelectFrameBlack();
	UFUNCTION() void SelectFrameRed();
	UFUNCTION() void ApplySportTemplate();
	UFUNCTION() void ApplyLuxuryTemplate();

	UPROPERTY(Transient)
	TObjectPtr<UCarConfiguratorSubsystem> Configurator;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CanonicalKeyText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PriceText;
};
