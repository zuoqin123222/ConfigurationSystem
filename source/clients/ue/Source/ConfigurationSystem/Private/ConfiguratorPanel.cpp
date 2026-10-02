#include "ConfiguratorPanel.h"

#include "Blueprint/WidgetTree.h"
#include "CarConfiguratorSubsystem.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Styling/CoreStyle.h"

TSharedRef<SWidget> UConfiguratorPanel::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		BuildWidgetTree();
	}
	return Super::RebuildWidget();
}

void UConfiguratorPanel::NativeConstruct()
{
	Super::NativeConstruct();
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		Configurator = GameInstance->GetSubsystem<UCarConfiguratorSubsystem>();
	}
	if (Configurator != nullptr)
	{
		Configurator->OnConfigurationChanged.AddDynamic(
			this,
			&UConfiguratorPanel::HandleConfigurationChanged);
	}
	RefreshSummary();
}

void UConfiguratorPanel::NativeDestruct()
{
	if (Configurator != nullptr)
	{
		Configurator->OnConfigurationChanged.RemoveDynamic(
			this,
			&UConfiguratorPanel::HandleConfigurationChanged);
	}
	Configurator = nullptr;
	Super::NativeDestruct();
}

void UConfiguratorPanel::BuildWidgetTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	USizeBox* PanelWidth = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("PanelWidth"));
	PanelWidth->SetWidthOverride(440.0f);
	UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(PanelWidth);
	PanelSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
	PanelSlot->SetAlignment(FVector2D(1.0f, 0.0f));
	PanelSlot->SetOffsets(FMargin(-32.0f, 32.0f, 440.0f, 64.0f));

	UBorder* PanelBackground = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("PanelBackground"));
	PanelBackground->SetBrushColor(FLinearColor(0.025f, 0.03f, 0.028f, 0.94f));
	PanelBackground->SetPadding(FMargin(22.0f));
	PanelWidth->AddChild(PanelBackground);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("Content"));
	PanelBackground->SetContent(Content);

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(NSLOCTEXT("Configurator", "PanelTitle", "演示车型选配"));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.87f, 0.92f, 0.84f)));
	Title->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 26));
	Content->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 16));

	UHorizontalBox* Templates = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("Templates"));
	Content->AddChildToVerticalBox(Templates)->SetPadding(FMargin(0, 0, 0, 14));
	UButton* Sport = MakeButton(
		NSLOCTEXT("Configurator", "SportTemplateButton", "模板：运动"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ApplySportTemplate));
	UButton* Luxury = MakeButton(
		NSLOCTEXT("Configurator", "LuxuryTemplateButton", "模板：豪华"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ApplyLuxuryTemplate));
	Templates->AddChildToHorizontalBox(Sport)->SetPadding(FMargin(0, 0, 6, 0));
	Templates->AddChildToHorizontalBox(Luxury)->SetPadding(FMargin(6, 0, 0, 0));

	AddOptionRow(
		Content,
		NSLOCTEXT("Configurator", "PaintPart", "车漆"),
		NSLOCTEXT("Configurator", "PaintRed", "竞速红"),
		NSLOCTEXT("Configurator", "PaintSilver", "星辉银  +¥8,800"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectPaintRed),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectPaintSilver));
	AddOptionRow(
		Content,
		NSLOCTEXT("Configurator", "WheelPart", "轮毂"),
		NSLOCTEXT("Configurator", "WheelSport", "运动轮毂"),
		NSLOCTEXT("Configurator", "WheelForged", "锻造轮毂  +¥12,000"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectWheelSport),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectWheelForged));
	AddOptionRow(
		Content,
		NSLOCTEXT("Configurator", "InteriorPart", "内饰"),
		NSLOCTEXT("Configurator", "InteriorDark", "曜石黑"),
		NSLOCTEXT("Configurator", "InteriorIvory", "象牙白  +¥6,800"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectInteriorDark),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectInteriorIvory));
	AddOptionRow(
		Content,
		NSLOCTEXT("Configurator", "FramePart", "内部车架"),
		NSLOCTEXT("Configurator", "FrameBlack", "哑光黑"),
		NSLOCTEXT("Configurator", "FrameRed", "性能红  +¥3,600"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectFrameBlack),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SelectFrameRed));

	UTextBlock* PriceLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("PriceLabel"));
	PriceLabel->SetText(NSLOCTEXT("Configurator", "TotalPrice", "车辆总价"));
	PriceLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.62f, 0.56f)));
	Content->AddChildToVerticalBox(PriceLabel)->SetPadding(FMargin(0, 12, 0, 2));

	PriceText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("Price"));
	PriceText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.9f, 0.7f)));
	PriceText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 24));
	Content->AddChildToVerticalBox(PriceText)->SetPadding(FMargin(0, 0, 0, 10));

	UTextBlock* KeyLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("KeyLabel"));
	KeyLabel->SetText(NSLOCTEXT("Configurator", "CanonicalKey", "Canonical key"));
	KeyLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.62f, 0.56f)));
	Content->AddChildToVerticalBox(KeyLabel);

	CanonicalKeyText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("CanonicalKeyValue"));
	CanonicalKeyText->SetAutoWrapText(true);
	CanonicalKeyText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Content->AddChildToVerticalBox(CanonicalKeyText);

	UTextBlock* TemporaryNotice = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("TemporaryNotice"));
	TemporaryNotice->SetText(NSLOCTEXT(
		"Configurator",
		"TemporaryAssets",
		"当前车辆为 /Engine/BasicShapes 临时占位资源"));
	TemporaryNotice->SetAutoWrapText(true);
	TemporaryNotice->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.55f, 0.3f)));
	Content->AddChildToVerticalBox(TemporaryNotice)->SetPadding(FMargin(0, 16, 0, 0));
}

void UConfiguratorPanel::AddHeading(UVerticalBox* Parent, const FText& Text)
{
	UTextBlock* Heading = WidgetTree->ConstructWidget<UTextBlock>();
	Heading->SetText(Text);
	Heading->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.72f)));
	Heading->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 15));
	Parent->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0, 7, 0, 4));
}

void UConfiguratorPanel::AddOptionRow(
	UVerticalBox* Parent,
	const FText& PartName,
	const FText& FirstName,
	const FText& SecondName,
	const FName FirstHandler,
	const FName SecondHandler)
{
	AddHeading(Parent, PartName);
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Parent->AddChildToVerticalBox(Row);
	UHorizontalBoxSlot* FirstSlot =
		Row->AddChildToHorizontalBox(MakeButton(FirstName, FirstHandler));
	FirstSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	FirstSlot->SetPadding(FMargin(0, 0, 5, 0));
	UHorizontalBoxSlot* SecondSlot =
		Row->AddChildToHorizontalBox(MakeButton(SecondName, SecondHandler));
	SecondSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SecondSlot->SetPadding(FMargin(5, 0, 0, 0));
}

UButton* UConfiguratorPanel::MakeButton(const FText& Label, const FName Handler)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(FLinearColor(0.16f, 0.2f, 0.17f, 1.0f));
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Text->SetAutoWrapText(true);
	Button->AddChild(Text);

	FScriptDelegate ClickDelegate;
	ClickDelegate.BindUFunction(this, Handler);
	Button->OnClicked.Add(ClickDelegate);
	return Button;
}

void UConfiguratorPanel::RefreshSummary()
{
	if (Configurator == nullptr)
	{
		return;
	}
	if (CanonicalKeyText != nullptr)
	{
		CanonicalKeyText->SetText(FText::FromString(Configurator->GetCanonicalKey()));
	}
	if (PriceText != nullptr)
	{
		const double Yuan = static_cast<double>(Configurator->GetTotalPriceMinor()) / 100.0;
		PriceText->SetText(FText::FromString(
			FString::Printf(TEXT("¥ %s"), *FString::Printf(TEXT("%.0f"), Yuan))));
	}
}

void UConfiguratorPanel::Select(const TCHAR* PartId, const TCHAR* OptionId)
{
	if (Configurator != nullptr)
	{
		Configurator->SelectOption(PartId, OptionId);
		RefreshSummary();
	}
}

void UConfiguratorPanel::HandleConfigurationChanged() { RefreshSummary(); }
void UConfiguratorPanel::SelectPaintRed() { Select(TEXT("paint"), TEXT("paint-red")); }
void UConfiguratorPanel::SelectPaintSilver() { Select(TEXT("paint"), TEXT("paint-silver")); }
void UConfiguratorPanel::SelectWheelSport() { Select(TEXT("wheel"), TEXT("wheel-sport")); }
void UConfiguratorPanel::SelectWheelForged() { Select(TEXT("wheel"), TEXT("wheel-forged")); }
void UConfiguratorPanel::SelectInteriorDark() { Select(TEXT("interior"), TEXT("interior-dark")); }
void UConfiguratorPanel::SelectInteriorIvory() { Select(TEXT("interior"), TEXT("interior-ivory")); }
void UConfiguratorPanel::SelectFrameBlack() { Select(TEXT("frame"), TEXT("frame-black")); }
void UConfiguratorPanel::SelectFrameRed() { Select(TEXT("frame"), TEXT("frame-red")); }

void UConfiguratorPanel::ApplySportTemplate()
{
	if (Configurator != nullptr)
	{
		Configurator->ApplyTemplate(TEXT("sport"));
		RefreshSummary();
	}
}

void UConfiguratorPanel::ApplyLuxuryTemplate()
{
	if (Configurator != nullptr)
	{
		Configurator->ApplyTemplate(TEXT("luxury"));
		RefreshSummary();
	}
}
