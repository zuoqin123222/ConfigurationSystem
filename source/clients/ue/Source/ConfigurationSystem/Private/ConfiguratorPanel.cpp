#include "ConfiguratorPanel.h"

#include "Blueprint/WidgetTree.h"
#include "CarConfiguratorSubsystem.h"
#include "ConfigShowroomPlayerController.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/GameInstance.h"
#include "PathTracingExperienceSubsystem.h"
#include "Sc01V2Domain.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

void UConfiguratorDataButton::InitializeBinding(
	const FString& InAction,
	const FString& InPrimaryId,
	const FString& InSecondaryId)
{
	Action = InAction;
	PrimaryId = InPrimaryId;
	SecondaryId = InSecondaryId;
	OnClicked.AddUniqueDynamic(this, &UConfiguratorDataButton::ForwardClick);
}

void UConfiguratorDataButton::ForwardClick()
{
	OnDataClicked.Broadcast(Action, PrimaryId, SecondaryId);
}

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
		Sc01V2State = Configurator->GetSc01V2State();
		if (Sc01V2State != nullptr)
		{
			Sc01V2State->OnChanged.AddDynamic(
				this,
				&UConfiguratorPanel::HandleSc01V2ConfigurationChanged);
		}
	}
	PathTracing = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathTracingExperienceSubsystem>()
		: nullptr;
	if (PathTracing != nullptr)
	{
		PathTracing->OnProgressChanged.AddDynamic(
			this, &UConfiguratorPanel::HandlePathTracingProgress);
		HandlePathTracingProgress(
			PathTracing->GetCurrentSample(), PathTracing->GetTargetSamples());
	}
	RefreshV2Navigation();
	RefreshSummary();
}

void UConfiguratorPanel::NativeDestruct()
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(V2NavigationRefreshTimer);
	}
	bV2NavigationRefreshPending = false;
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
	if (Sc01V2State != nullptr)
	{
		Sc01V2State->OnChanged.RemoveDynamic(
			this,
			&UConfiguratorPanel::HandleSc01V2ConfigurationChanged);
	}
	CommitPendingPaint();
	Configurator = nullptr;
	PathTracing = nullptr;
	Sc01V2State = nullptr;
	Super::NativeDestruct();
}

void UConfiguratorPanel::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bPaintCommitPending
		&& GetWorld() != nullptr
		&& GetWorld()->GetRealTimeSeconds() >= NextPaintCommitSeconds)
	{
		CommitPendingPaint();
	}
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
	Title->SetText(NSLOCTEXT("Configurator", "PanelTitle", "SC01 动态选配"));
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

	UWrapBox* Cameras = WidgetTree->ConstructWidget<UWrapBox>();
	Cameras->SetInnerSlotPadding(FVector2D(4.0f, 4.0f));
	Content->AddChildToVerticalBox(Cameras)->SetPadding(FMargin(0, 0, 0, 5));
	const FText CameraLabels[] = {
		NSLOCTEXT("Configurator", "CameraFront", "前"),
		NSLOCTEXT("Configurator", "CameraRear", "后"),
		NSLOCTEXT("Configurator", "CameraLeft", "左"),
		NSLOCTEXT("Configurator", "CameraRight", "右"),
		NSLOCTEXT("Configurator", "CameraInteriorDriver", "主驾"),
		NSLOCTEXT("Configurator", "CameraInteriorPassenger", "副驾")
	};
	const FName CameraHandlers[] = {
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraFront),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraRear),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraLeft),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraRight),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraInterior),
		GET_FUNCTION_NAME_CHECKED(UConfiguratorPanel, CameraInteriorPassenger)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CameraLabels); ++Index)
	{
		Cameras->AddChildToWrapBox(MakeButton(CameraLabels[Index], CameraHandlers[Index]));
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

	BuildSc01V2Controls(Content);

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
		"SC01 v2 catalog · 当前车辆为临时占位资源"));
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

void UConfiguratorPanel::BuildSc01V2Controls(UVerticalBox* Parent)
{
	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Region", "区域"));
	RegionButtons = WidgetTree->ConstructWidget<UWrapBox>();
	RegionButtons->SetInnerSlotPadding(FVector2D(6.0f, 6.0f));
	Parent->AddChildToVerticalBox(RegionButtons);

	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Category", "类别"));
	CategoryButtons = WidgetTree->ConstructWidget<UWrapBox>();
	CategoryButtons->SetInnerSlotPadding(FVector2D(6.0f, 6.0f));
	Parent->AddChildToVerticalBox(CategoryButtons);

	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Surface", "表面"));
	SurfaceButtons = WidgetTree->ConstructWidget<UVerticalBox>();
	Parent->AddChildToVerticalBox(SurfaceButtons);

	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Option", "选项"));
	OptionButtons = WidgetTree->ConstructWidget<UVerticalBox>();
	Parent->AddChildToVerticalBox(OptionButtons);

	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Variant", "材料色卡"));
	VariantButtons = WidgetTree->ConstructWidget<UVerticalBox>();
	Parent->AddChildToVerticalBox(VariantButtons);

	PaintEditor = WidgetTree->ConstructWidget<UVerticalBox>();
	AddHeading(PaintEditor, NSLOCTEXT("Configurator", "V2CustomPaint", "Custom paint"));
	PaintHex = WidgetTree->ConstructWidget<UEditableTextBox>();
	PaintHex->SetHintText(NSLOCTEXT("Configurator", "PaintHexHint", "#RRGGBB"));
	PaintHex->OnTextCommitted.AddDynamic(
		this, &UConfiguratorPanel::HandlePaintHexCommitted);
	PaintEditor->AddChildToVerticalBox(PaintHex);

	auto AddSlider = [this](const FText& Label)
	{
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
		Text->SetText(Label);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.72f)));
		PaintEditor->AddChildToVerticalBox(Text)->SetPadding(FMargin(0, 5, 0, 1));
		USlider* Slider = WidgetTree->ConstructWidget<USlider>();
		Slider->SetMinValue(0.0f);
		Slider->SetMaxValue(1.0f);
		Slider->SetStepSize(0.01f);
		PaintEditor->AddChildToVerticalBox(Slider);
		return Slider;
	};
	MetallicSlider = AddSlider(NSLOCTEXT("Configurator", "Metallic", "Metallic"));
	RoughnessSlider = AddSlider(NSLOCTEXT("Configurator", "Roughness", "Roughness"));
	ClearCoatSlider = AddSlider(NSLOCTEXT("Configurator", "ClearCoat", "Clear coat"));
	OrangePeelSlider = AddSlider(NSLOCTEXT("Configurator", "OrangePeel", "Orange peel"));
	FlakeIntensitySlider = AddSlider(NSLOCTEXT("Configurator", "FlakeIntensity", "Flake intensity"));
	MetallicSlider->OnValueChanged.AddDynamic(this, &UConfiguratorPanel::HandleMetallicChanged);
	RoughnessSlider->OnValueChanged.AddDynamic(this, &UConfiguratorPanel::HandleRoughnessChanged);
	ClearCoatSlider->OnValueChanged.AddDynamic(this, &UConfiguratorPanel::HandleClearCoatChanged);
	OrangePeelSlider->OnValueChanged.AddDynamic(this, &UConfiguratorPanel::HandleOrangePeelChanged);
	FlakeIntensitySlider->OnValueChanged.AddDynamic(this, &UConfiguratorPanel::HandleFlakeIntensityChanged);
	MetallicSlider->OnMouseCaptureEnd.AddDynamic(this, &UConfiguratorPanel::HandlePaintSliderCaptureEnded);
	RoughnessSlider->OnMouseCaptureEnd.AddDynamic(this, &UConfiguratorPanel::HandlePaintSliderCaptureEnded);
	ClearCoatSlider->OnMouseCaptureEnd.AddDynamic(this, &UConfiguratorPanel::HandlePaintSliderCaptureEnded);
	OrangePeelSlider->OnMouseCaptureEnd.AddDynamic(this, &UConfiguratorPanel::HandlePaintSliderCaptureEnded);
	FlakeIntensitySlider->OnMouseCaptureEnd.AddDynamic(this, &UConfiguratorPanel::HandlePaintSliderCaptureEnded);
	Parent->AddChildToVerticalBox(PaintEditor)->SetPadding(FMargin(0, 4, 0, 8));

	V2StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	V2StatusText->SetAutoWrapText(true);
	V2StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.9f, 0.7f)));
	Parent->AddChildToVerticalBox(V2StatusText)->SetPadding(FMargin(0, 4, 0, 8));
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

UConfiguratorDataButton* UConfiguratorPanel::MakeDataButton(
	const FString& Label,
	const FString& Action,
	const FString& PrimaryId,
	const FString& SecondaryId)
{
	UConfiguratorDataButton* Button =
		WidgetTree->ConstructWidget<UConfiguratorDataButton>();
	Button->SetBackgroundColor(FLinearColor(0.16f, 0.2f, 0.17f, 1.0f));
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Text->SetAutoWrapText(true);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Button->AddChild(Text);
	Button->InitializeBinding(Action, PrimaryId, SecondaryId);
	Button->OnDataClicked.AddUObject(this, &UConfiguratorPanel::HandleDataButtonClicked);
	return Button;
}

void UConfiguratorPanel::RefreshV2Navigation()
{
	if (RegionButtons == nullptr)
	{
		return;
	}
	RegionButtons->ClearChildren();
	if (Sc01V2State == nullptr || !Sc01V2State->IsInitialized())
	{
		V2StatusText->SetText(NSLOCTEXT(
			"Configurator", "V2Unavailable", "SC01 v2 catalog asset 不可用"));
		return;
	}
	const Sc01V2::FCatalog& Catalog = Sc01V2State->GetCatalogIndex().GetCatalog();
	if (SelectedRegionId.IsEmpty() && !Catalog.Regions.IsEmpty())
	{
		SelectedRegionId = Catalog.Regions[0].RegionId;
	}
	for (const Sc01V2::FRegion& Region : Catalog.Regions)
	{
		RegionButtons->AddChildToWrapBox(MakeDataButton(
			Region.DisplayName,
			TEXT("region"),
			Region.RegionId));
	}
	RefreshCategoryButtons();
}

void UConfiguratorPanel::RefreshCategoryButtons()
{
	CategoryButtons->ClearChildren();
	const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
	const TArray<FString>* CategoryIds = Index.FindCategoryIdsForRegion(SelectedRegionId);
	if (CategoryIds == nullptr || CategoryIds->IsEmpty())
	{
		SelectedCategoryId.Reset();
		RefreshSurfaceButtons();
		return;
	}
	if (!CategoryIds->Contains(SelectedCategoryId))
	{
		SelectedCategoryId = (*CategoryIds)[0];
	}
	for (const FString& CategoryId : *CategoryIds)
	{
		const Sc01V2::FCategory* Category = Index.FindCategory(CategoryId);
		CategoryButtons->AddChildToWrapBox(MakeDataButton(
			Category->DisplayName,
			TEXT("category"),
			CategoryId));
	}
	RefreshSurfaceButtons();
}

void UConfiguratorPanel::RefreshSurfaceButtons()
{
	SurfaceButtons->ClearChildren();
	const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
	const TArray<FString>* SurfaceIds = Index.FindSurfaceIdsForCategory(SelectedCategoryId);
	if (SurfaceIds == nullptr || SurfaceIds->IsEmpty())
	{
		SelectedSurfaceId.Reset();
		RefreshOptionButtons();
		return;
	}
	if (!SurfaceIds->Contains(SelectedSurfaceId))
	{
		SelectedSurfaceId = (*SurfaceIds)[0];
	}
	for (const FString& SurfaceId : *SurfaceIds)
	{
		const Sc01V2::FSurface* Surface = Index.FindSurface(SurfaceId);
		SurfaceButtons->AddChildToVerticalBox(MakeDataButton(
			Surface->DisplayName,
			TEXT("surface"),
			SurfaceId))->SetPadding(FMargin(0, 1));
	}
	RefreshOptionButtons();
}

void UConfiguratorPanel::RefreshOptionButtons()
{
	OptionButtons->ClearChildren();
	if (Sc01V2State == nullptr)
	{
		return;
	}
	const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
	const TArray<FString>* OptionIds = Index.FindOptionIdsForSurface(SelectedSurfaceId);
	const FString SelectedOption = Sc01V2State->GetSelections().FindRef(SelectedSurfaceId);
	if (OptionIds != nullptr)
	{
		for (const FString& OptionId : *OptionIds)
		{
			const Sc01V2::FOption* Option = Index.FindOption(OptionId);
			const FString Marker = OptionId == SelectedOption ? TEXT("● ") : TEXT("○ ");
			OptionButtons->AddChildToVerticalBox(MakeDataButton(
				Marker + Option->DisplayName,
				TEXT("option"),
				SelectedSurfaceId,
				OptionId))->SetPadding(FMargin(0, 1));
		}
	}
	RefreshVariantButtons();
	RefreshPaintEditor();
}

void UConfiguratorPanel::RefreshVariantButtons()
{
	VariantButtons->ClearChildren();
	const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
	const FString OptionId = Sc01V2State->GetSelections().FindRef(SelectedSurfaceId);
	const Sc01V2::FOption* Option = Index.FindOption(OptionId);
	if (Option == nullptr || !Option->MaterialFamilyId.IsSet())
	{
		VariantButtons->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	VariantButtons->SetVisibility(ESlateVisibility::Visible);
	const TArray<FString>* VariantIds =
		Index.FindVariantIdsForMaterialFamily(Option->MaterialFamilyId.GetValue());
	const TMap<FString, FSc01V2Customization> Customizations =
		Sc01V2State->GetCustomizations();
	const FSc01V2Customization* Current = Customizations.Find(SelectedSurfaceId);
	if (VariantIds != nullptr)
	{
		for (const FString& VariantId : *VariantIds)
		{
			const Sc01V2::FMaterialVariant* Variant = Index.FindMaterialVariant(VariantId);
			const bool bSelected = Current != nullptr
				&& Current->Kind == ESc01V2CustomizationKind::MaterialVariant
				&& Current->MaterialVariantId == VariantId;
			VariantButtons->AddChildToVerticalBox(MakeDataButton(
				(bSelected ? TEXT("● ") : TEXT("○ ")) + Variant->DisplayName,
				TEXT("variant"),
				SelectedSurfaceId,
				VariantId))->SetPadding(FMargin(0, 1));
		}
	}
}

void UConfiguratorPanel::RefreshPaintEditor()
{
	const FString OptionId = Sc01V2State != nullptr
		? Sc01V2State->GetSelections().FindRef(SelectedSurfaceId)
		: FString();
	const bool bVisible = OptionId == Sc01V2::CustomPaintOptionId;
	PaintEditor->SetVisibility(
		bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bVisible)
	{
		bPaintCommitPending = false;
		return;
	}

	PendingPaint.ColorHex = TEXT("#FFFFFF");
	PendingPaint.Metallic = 0.5;
	PendingPaint.Roughness = 0.35;
	PendingPaint.ClearCoat = 0.8;
	PendingPaint.OrangePeel = 0.1;
	PendingPaint.FlakeIntensity = 0.25;
	const TMap<FString, FSc01V2Customization> Customizations =
		Sc01V2State->GetCustomizations();
	const FSc01V2Customization* Current = Customizations.Find(SelectedSurfaceId);
	if (Current != nullptr && Current->Kind == ESc01V2CustomizationKind::Paint)
	{
		PendingPaint = Current->Paint;
	}
	bRefreshingPaintEditor = true;
	PaintHex->SetText(FText::FromString(PendingPaint.ColorHex));
	MetallicSlider->SetValue(PendingPaint.Metallic);
	RoughnessSlider->SetValue(PendingPaint.Roughness);
	ClearCoatSlider->SetValue(PendingPaint.ClearCoat);
	OrangePeelSlider->SetValue(PendingPaint.OrangePeel);
	FlakeIntensitySlider->SetValue(PendingPaint.FlakeIntensity);
	bRefreshingPaintEditor = false;
}

void UConfiguratorPanel::HandleDataButtonClicked(
	const FString& Action,
	const FString& PrimaryId,
	const FString& SecondaryId)
{
	if (Sc01V2State == nullptr)
	{
		return;
	}
	if (Action == TEXT("region"))
	{
		SelectedRegionId = PrimaryId;
		SelectedCategoryId.Reset();
		SelectedSurfaceId.Reset();
		QueueV2NavigationRefresh();
	}
	else if (Action == TEXT("category"))
	{
		SelectedCategoryId = PrimaryId;
		SelectedSurfaceId.Reset();
		QueueV2NavigationRefresh();
	}
	else if (Action == TEXT("surface"))
	{
		SelectedSurfaceId = PrimaryId;
		QueueV2NavigationRefresh();
	}
	else if (Action == TEXT("option"))
	{
		if (SecondaryId == Sc01V2::CustomPaintOptionId)
		{
			TMap<FString, FString> Selections = Sc01V2State->GetSelections();
			TMap<FString, FSc01V2Customization> Customizations =
				Sc01V2State->GetCustomizations();
			Selections.Add(PrimaryId, SecondaryId);
			FSc01V2Customization Paint;
			Paint.Kind = ESc01V2CustomizationKind::Paint;
			Paint.Paint.ColorHex = TEXT("#FFFFFF");
			Paint.Paint.Metallic = 0.5;
			Paint.Paint.Roughness = 0.35;
			Paint.Paint.ClearCoat = 0.8;
			Paint.Paint.OrangePeel = 0.1;
			Paint.Paint.FlakeIntensity = 0.25;
			Customizations.Add(PrimaryId, Paint);
			Sc01V2State->ApplyTransaction(Selections, Customizations);
		}
		else
		{
			Sc01V2State->SelectOption(PrimaryId, SecondaryId);
		}
	}
	else if (Action == TEXT("variant"))
	{
		Sc01V2State->SetMaterialVariant(PrimaryId, SecondaryId);
	}
}

void UConfiguratorPanel::QueueV2NavigationRefresh()
{
	if (bV2NavigationRefreshPending)
	{
		return;
	}
	if (GetWorld() == nullptr)
	{
		RefreshV2Navigation();
		return;
	}

	bV2NavigationRefreshPending = true;
	V2NavigationRefreshTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateUObject(
			this,
			&UConfiguratorPanel::HandleDeferredV2NavigationRefresh));
}

void UConfiguratorPanel::HandleDeferredV2NavigationRefresh()
{
	bV2NavigationRefreshPending = false;
	RefreshV2Navigation();
}

void UConfiguratorPanel::QueuePaintCommit()
{
	if (bRefreshingPaintEditor || GetWorld() == nullptr)
	{
		return;
	}
	if (!bPaintCommitPending)
	{
		bPaintCommitPending = true;
		NextPaintCommitSeconds = GetWorld()->GetRealTimeSeconds() + 0.05;
	}
}

void UConfiguratorPanel::CommitPendingPaint()
{
	if (!bPaintCommitPending || Sc01V2State == nullptr)
	{
		return;
	}
	bPaintCommitPending = false;
	if (!Sc01V2State->SetPaintCustomization(SelectedSurfaceId, PendingPaint)
		&& V2StatusText != nullptr)
	{
		V2StatusText->SetText(FText::FromString(
			TEXT("参数未应用：") + Sc01V2State->GetLastErrorCode()));
	}
}

void UConfiguratorPanel::RefreshSummary()
{
	if (Configurator == nullptr)
	{
		return;
	}
	if (CanonicalKeyText != nullptr)
	{
		CanonicalKeyText->SetText(FText::FromString(
			Sc01V2State != nullptr
				? Sc01V2State->GetConfigurationId() + TEXT("\n") + Sc01V2State->GetRenderKey()
				: Configurator->GetCanonicalKey()));
	}
	if (PriceText != nullptr)
	{
		if (Sc01V2State != nullptr)
		{
			PriceText->SetText(NSLOCTEXT(
				"Configurator", "V2PriceUnconfirmed", "价格待确认 · 不可报价"));
		}
		else
		{
			const double Yuan = static_cast<double>(Configurator->GetTotalPriceMinor()) / 100.0;
			PriceText->SetText(FText::FromString(
				FString::Printf(TEXT("¥ %s"), *FString::Printf(TEXT("%.0f"), Yuan))));
		}
	}
}

void UConfiguratorPanel::HandleConfigurationChanged() { RefreshSummary(); }
void UConfiguratorPanel::HandleSc01V2ConfigurationChanged()
{
	QueueV2NavigationRefresh();
	RefreshSummary();
	if (V2StatusText != nullptr)
	{
		V2StatusText->SetText(FText::FromString(
			FString::Printf(
				TEXT("38/38 已选择 · %s"),
				*Sc01V2State->GetConfigurationId())));
	}
}
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
void UConfiguratorPanel::HandlePaintHexCommitted(
	const FText& Text,
	const ETextCommit::Type CommitMethod)
{
	(void)CommitMethod;
	PendingPaint.ColorHex = Text.ToString();
	bPaintCommitPending = true;
	CommitPendingPaint();
}

void UConfiguratorPanel::HandleMetallicChanged(const float Value)
{
	PendingPaint.Metallic = Value;
	QueuePaintCommit();
}

void UConfiguratorPanel::HandleRoughnessChanged(const float Value)
{
	PendingPaint.Roughness = Value;
	QueuePaintCommit();
}

void UConfiguratorPanel::HandleClearCoatChanged(const float Value)
{
	PendingPaint.ClearCoat = Value;
	QueuePaintCommit();
}

void UConfiguratorPanel::HandleOrangePeelChanged(const float Value)
{
	PendingPaint.OrangePeel = Value;
	QueuePaintCommit();
}

void UConfiguratorPanel::HandleFlakeIntensityChanged(const float Value)
{
	PendingPaint.FlakeIntensity = Value;
	QueuePaintCommit();
}

void UConfiguratorPanel::HandlePaintSliderCaptureEnded()
{
	CommitPendingPaint();
}

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
void UConfiguratorPanel::CameraInteriorPassenger()
{
	if (AConfigShowroomPlayerController* PC = Cast<AConfigShowroomPlayerController>(GetOwningPlayer())) { PC->SwitchCamera(5); }
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
