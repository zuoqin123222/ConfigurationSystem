#include "ConfiguratorPanel.h"

#include "Blueprint/WidgetTree.h"
#include "CarConfiguratorSubsystem.h"
#include "ConfigShowroomPlayerController.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "PathTracingExperienceSubsystem.h"
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
	PathTracing = GetGameInstance()->GetSubsystem<UPathTracingExperienceSubsystem>();
	if (PathTracing != nullptr)
	{
		PathTracing->OnProgressChanged.AddDynamic(
			this, &UConfiguratorPanel::HandlePathTracingProgress);
		HandlePathTracingProgress(
			PathTracing->GetCurrentSample(), PathTracing->GetTargetSamples());
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
	if (PathTracing != nullptr)
	{
		PathTracing->OnProgressChanged.RemoveDynamic(
			this, &UConfiguratorPanel::HandlePathTracingProgress);
	}
	Configurator = nullptr;
	PathTracing = nullptr;
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
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(
		UScrollBox::StaticClass(), TEXT("Scroll"));
	PanelBackground->SetContent(Scroll);
	Scroll->AddChild(Content);

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

	AddHeading(Content, NSLOCTEXT("Configurator", "ExperienceControls", "体验控制（纯 C++）"));
	UHorizontalBox* Parts = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(Parts)->SetPadding(FMargin(0, 0, 0, 5));
	Parts->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "LeftDoor", "左门"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ToggleLeftDoor)));
	Parts->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "RightDoor", "右门"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ToggleRightDoor)));
	Parts->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "Hood", "机盖"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ToggleHood)));
	Parts->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "Trunk", "后备箱"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ToggleTrunk)));
	Parts->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "Wheels", "车轮"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ToggleWheels)));

	UHorizontalBox* Cameras = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(Cameras)->SetPadding(FMargin(0, 0, 0, 5));
	const FText CameraLabels[] = {
		NSLOCTEXT("Configurator", "CameraFront", "前"),
		NSLOCTEXT("Configurator", "CameraRear", "后"),
		NSLOCTEXT("Configurator", "CameraLeft", "左"),
		NSLOCTEXT("Configurator", "CameraRight", "右"),
		NSLOCTEXT("Configurator", "CameraInterior", "内")
	};
	const FName CameraHandlers[] = {
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraFront),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraRear),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraLeft),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraRight),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraInterior)
	};
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Cameras->AddChildToHorizontalBox(MakeButton(CameraLabels[Index], CameraHandlers[Index]));
	}

	UHorizontalBox* Runtime = WidgetTree->ConstructWidget<UHorizontalBox>();
	Content->AddChildToVerticalBox(Runtime)->SetPadding(FMargin(0, 0, 0, 5));
	Runtime->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "Environment", "切换环境"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, ToggleEnvironment)));
	Runtime->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "PathTracing", "Path Tracing"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, TogglePathTracing)));
	Runtime->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "Save", "保存"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, SaveExperience)));
	Runtime->AddChildToHorizontalBox(MakeButton(
		NSLOCTEXT("Configurator", "Load", "载入"),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, LoadExperience)));

	PathTracingProgress = WidgetTree->ConstructWidget<UProgressBar>(
		UProgressBar::StaticClass(), TEXT("PathTracingProgress"));
	PathTracingProgress->SetPercent(0.0f);
	PathTracingProgress->SetFillColorAndOpacity(FLinearColor(0.72f, 0.86f, 0.62f));
	Content->AddChildToVerticalBox(PathTracingProgress)->SetPadding(FMargin(0, 2, 0, 2));
	ExperienceStatusText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ExperienceStatus"));
	ExperienceStatusText->SetText(NSLOCTEXT(
		"Configurator", "ExperienceReady", "实时模式 · 环境 A · 机位 1"));
	ExperienceStatusText->SetAutoWrapText(true);
	Content->AddChildToVerticalBox(ExperienceStatusText)->SetPadding(FMargin(0, 0, 0, 8));

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
void UConfiguratorPanel::HandlePathTracingProgress(
	const int32 CurrentSample,
	const int32 TargetSamples)
{
	if (PathTracingProgress != nullptr)
	{
		PathTracingProgress->SetPercent(
			TargetSamples > 0
				? FMath::Clamp(static_cast<float>(CurrentSample) / TargetSamples, 0.0f, 1.0f)
				: 0.0f);
	}
	if (ExperienceStatusText != nullptr && PathTracing != nullptr)
	{
		ExperienceStatusText->SetText(FText::FromString(
			PathTracing->IsPathTracingEnabled()
				? FString::Printf(TEXT("Path Tracing · %d / %d 样本"), CurrentSample, TargetSamples)
				: TEXT("实时模式")));
	}
}
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

void UConfiguratorPanel::ToggleLeftDoor()
{
	if (AConfigShowroomPlayerController* PC =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleVehiclePart(TEXT("door-left"));
	}
}
void UConfiguratorPanel::ToggleRightDoor()
{
	if (AConfigShowroomPlayerController* PC =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleVehiclePart(TEXT("door-right"));
	}
}
void UConfiguratorPanel::ToggleHood()
{
	if (AConfigShowroomPlayerController* PC =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleVehiclePart(TEXT("hood"));
	}
}
void UConfiguratorPanel::ToggleTrunk()
{
	if (AConfigShowroomPlayerController* PC =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleVehiclePart(TEXT("trunk"));
	}
}
void UConfiguratorPanel::ToggleWheels()
{
	if (AConfigShowroomPlayerController* PC =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleWheelSpin();
	}
}
void UConfiguratorPanel::CameraFront()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer())) { PC->SwitchCamera(0); }
}
void UConfiguratorPanel::CameraRear()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer())) { PC->SwitchCamera(1); }
}
void UConfiguratorPanel::CameraLeft()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer())) { PC->SwitchCamera(2); }
}
void UConfiguratorPanel::CameraRight()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer())) { PC->SwitchCamera(3); }
}
void UConfiguratorPanel::CameraInterior()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer())) { PC->SwitchCamera(4); }
}
void UConfiguratorPanel::ToggleEnvironment()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleEnvironment();
	}
}
void UConfiguratorPanel::TogglePathTracing()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		FString Error;
		if (!PC->TogglePathTracing(Error) && ExperienceStatusText != nullptr)
		{
			ExperienceStatusText->SetText(FText::FromString(Error));
		}
	}
}
void UConfiguratorPanel::SaveExperience()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		FString Error;
		const bool bSaved = PC->SaveExperience(Error);
		if (ExperienceStatusText != nullptr)
		{
			ExperienceStatusText->SetText(FText::FromString(
				bSaved ? TEXT("配置、环境和 UI 已原子保存") : Error));
		}
	}
}
void UConfiguratorPanel::LoadExperience()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		FString Error;
		const bool bLoaded = PC->LoadExperience(Error);
		if (ExperienceStatusText != nullptr)
		{
			ExperienceStatusText->SetText(FText::FromString(
				bLoaded ? TEXT("配置、环境和 UI 已恢复") : Error));
		}
	}
}
