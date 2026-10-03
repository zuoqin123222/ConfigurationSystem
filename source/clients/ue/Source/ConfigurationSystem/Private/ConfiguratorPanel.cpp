#include "ConfiguratorPanel.h"

#include "Async/Async.h"
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
#include "Components/Image.h"
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
#include "Engine/Texture2D.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "PathTracingExperienceSubsystem.h"
#include "Sc01V2Domain.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

namespace
{
	bool DecodeVariantThumbnailPreview(const FString& ThumbnailUrl, FImage& OutPreview)
	{
		constexpr int32 PreviewSize = 64;
		const FString Prefix(TEXT("/sc01/thumbnails/"));
		if (!ThumbnailUrl.StartsWith(Prefix) || ThumbnailUrl.Contains(TEXT("..")))
		{
			return false;
		}
		const FString RelativePath = ThumbnailUrl.RightChop(1);
		const FString Candidates[] = {
			FPaths::Combine(FPlatformProcess::BaseDir(), RelativePath),
			FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectDir(), TEXT("../web/public"), RelativePath))
		};
		for (const FString& Filename : Candidates)
		{
			FImage Image;
			if (FImageUtils::LoadImage(*Filename, Image))
			{
				Image.ResizeTo(
					OutPreview,
					PreviewSize,
					PreviewSize,
					ERawImageFormat::BGRA8,
					EGammaSpace::sRGB);
				return OutPreview.SizeX == PreviewSize
					&& OutPreview.SizeY == PreviewSize;
			}
		}
		return false;
	}
}

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
	++ThumbnailLifetimeSerial;
	bAcceptThumbnailResults = true;
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
	bAcceptThumbnailResults = false;
	++ThumbnailLifetimeSerial;
	ResetThumbnailViewState();
	ActiveVariantThumbnailUrls.Reset();
	PendingVariantThumbnailUrlSet.Reset();
	ActiveVariantThumbnailLoadCount = 0;
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
	VariantThumbnailCache.Reset();
	VariantThumbnailCacheOrder.Reset();
	FailedVariantThumbnailUrls.Reset();
	FailedVariantThumbnailOrder.Reset();
	VariantThumbnailLoaderOverride = nullptr;
	Super::NativeDestruct();
}

void UConfiguratorPanel::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateVisibleThumbnailRequests();
	PumpVariantThumbnailLoads();
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

	AddHeading(Content, NSLOCTEXT("Configurator", "PriceSummary", "价格摘要"));
	BasePriceText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("BasePrice"));
	OptionsPriceText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("OptionsPrice"));
	ReferenceTotalText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ReferenceTotal"));
	BasePriceText->SetColorAndOpacity(FSlateColor(FLinearColor(0.66f, 0.7f, 0.66f)));
	OptionsPriceText->SetColorAndOpacity(FSlateColor(FLinearColor(0.66f, 0.7f, 0.66f)));
	ReferenceTotalText->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.9f, 0.7f)));
	ReferenceTotalText->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 24));
	Content->AddChildToVerticalBox(BasePriceText);
	Content->AddChildToVerticalBox(OptionsPriceText);
	Content->AddChildToVerticalBox(ReferenceTotalText)->SetPadding(FMargin(0, 2, 0, 10));

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
	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Stage", "阶段 · 外饰 / 内饰 / 性能 / 个性化"));
	StageButtons = WidgetTree->ConstructWidget<UWrapBox>();
	StageButtons->SetInnerSlotPadding(FVector2D(6.0f, 6.0f));
	Parent->AddChildToVerticalBox(StageButtons);

	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Component", "部件"));
	ComponentButtons = WidgetTree->ConstructWidget<UWrapBox>();
	ComponentButtons->SetInnerSlotPadding(FVector2D(6.0f, 6.0f));
	Parent->AddChildToVerticalBox(ComponentButtons);

	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Surface", "项目"));
	SurfaceButtons = WidgetTree->ConstructWidget<UVerticalBox>();
	Parent->AddChildToVerticalBox(SurfaceButtons);

	AddHeading(Parent, NSLOCTEXT("Configurator", "V2Option", "选项与材质"));
	OptionButtons = WidgetTree->ConstructWidget<UVerticalBox>();
	Parent->AddChildToVerticalBox(OptionButtons);

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

UConfiguratorDataButton* UConfiguratorPanel::MakeVariantButton(
	const Sc01V2::FMaterialVariant& Variant,
	const FString& OptionId,
	const bool bSelected,
	UImage*& OutSwatch)
{
	UConfiguratorDataButton* Button =
		WidgetTree->ConstructWidget<UConfiguratorDataButton>();
	Button->SetBackgroundColor(
		bSelected
			? FLinearColor(0.45f, 0.48f, 0.44f, 1.0f)
			: FLinearColor(0.11f, 0.13f, 0.12f, 1.0f));

	UHorizontalBox* Content = WidgetTree->ConstructWidget<UHorizontalBox>();
	USizeBox* SwatchSize = WidgetTree->ConstructWidget<USizeBox>();
	SwatchSize->SetWidthOverride(24.0f);
	SwatchSize->SetHeightOverride(24.0f);
	UImage* Swatch = WidgetTree->ConstructWidget<UImage>();
	Swatch->SetColorAndOpacity(FLinearColor::Transparent);
	Swatch->SetToolTipText(NSLOCTEXT(
		"Configurator",
		"VariantThumbnailLoading",
		"色卡缩略图加载中"));
	OutSwatch = Swatch;
	SwatchSize->AddChild(Swatch);
	Content->AddChildToHorizontalBox(SwatchSize)->SetPadding(FMargin(2));

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Variant.DisplayName));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	Content->AddChildToHorizontalBox(Text)->SetPadding(FMargin(4, 3, 6, 3));
	Button->AddChild(Content);
	Button->InitializeBinding(TEXT("variant"), OptionId, Variant.VariantId);
	Button->OnDataClicked.AddUObject(this, &UConfiguratorPanel::HandleDataButtonClicked);
	return Button;
}

void UConfiguratorPanel::RegisterThumbnailStrip(
	UScrollBox* ScrollBox,
	TArray<TPair<FString, TWeakObjectPtr<UImage>>>&& Items)
{
	FThumbnailStrip& Strip = VariantThumbnailStrips.Emplace_GetRef();
	Strip.ScrollBox = ScrollBox;
	Strip.Items = MoveTemp(Items);
}

void UConfiguratorPanel::ResetThumbnailViewState()
{
	++ThumbnailViewGeneration;
	VariantThumbnailStrips.Reset();
	PendingVariantThumbnailUrls.Reset();
	PendingVariantThumbnailUrlSet = ActiveVariantThumbnailUrls;
	VariantThumbnailWaiters.Reset();
	VisibleVariantThumbnailImages.Reset();
}

FInt32Range UConfiguratorPanel::CalculateThumbnailRequestRange(
	const float ScrollOffset,
	const float ViewportWidth,
	const int32 ItemCount)
{
	if (ItemCount <= 0)
	{
		return FInt32Range(0, 0);
	}
	const int32 FirstVisible = FMath::FloorToInt(
		FMath::Max(0.0f, ScrollOffset) / ThumbnailItemExtent);
	const int32 VisibleCount = FMath::Max(
		1,
		FMath::CeilToInt(FMath::Max(1.0f, ViewportWidth) / ThumbnailItemExtent));
	const int32 FirstRequested = FMath::Clamp(
		FirstVisible - ThumbnailPrefetchItems,
		0,
		ItemCount);
	const int32 LastRequestedExclusive = FMath::Clamp(
		FirstVisible + VisibleCount + ThumbnailPrefetchItems,
		FirstRequested,
		ItemCount);
	return FInt32Range(FirstRequested, LastRequestedExclusive);
}

void UConfiguratorPanel::UpdateVisibleThumbnailRequests()
{
	VisibleVariantThumbnailImages.Reset();
	for (const FThumbnailStrip& Strip : VariantThumbnailStrips)
	{
		UScrollBox* ScrollBox = Strip.ScrollBox.Get();
		if (ScrollBox == nullptr)
		{
			continue;
		}
		const float CachedWidth = ScrollBox->GetCachedGeometry().GetLocalSize().X;
		const float ViewportWidth = CachedWidth > 1.0f ? CachedWidth : 440.0f;
		const FInt32Range Range = CalculateThumbnailRequestRange(
			ScrollBox->GetScrollOffset(),
			ViewportWidth,
			Strip.Items.Num());
		for (int32 Index = 0; Index < Strip.Items.Num(); ++Index)
		{
			UImage* Image = Strip.Items[Index].Value.Get();
			if (Index >= Range.GetLowerBoundValue()
				&& Index < Range.GetUpperBoundValue())
			{
				RequestVariantThumbnail(Strip.Items[Index].Key, Image);
			}
			else if (Image != nullptr
				&& Image->GetBrush().GetResourceObject() != nullptr)
			{
				Image->SetBrushFromTexture(nullptr);
				Image->SetColorAndOpacity(FLinearColor::Transparent);
			}
		}
	}
}

void UConfiguratorPanel::RequestVariantThumbnail(
	const FString& ThumbnailUrl,
	UImage* TargetImage)
{
	if (TargetImage == nullptr || ThumbnailUrl.IsEmpty())
	{
		return;
	}
	VisibleVariantThumbnailImages.Add(TargetImage);
	if (const TObjectPtr<UTexture2D>* CachedTexture =
		VariantThumbnailCache.Find(ThumbnailUrl))
	{
		TouchVariantThumbnail(ThumbnailUrl);
		TargetImage->SetBrushFromTexture(CachedTexture->Get());
		TargetImage->SetColorAndOpacity(FLinearColor::White);
		TargetImage->SetToolTipText(FText::GetEmpty());
		return;
	}
	if (FailedVariantThumbnailUrls.Contains(ThumbnailUrl))
	{
		TargetImage->SetToolTipText(NSLOCTEXT(
			"Configurator",
			"VariantThumbnailUnavailable",
			"色卡缩略图不可用"));
		return;
	}

	TArray<FThumbnailTarget>& Waiters =
		VariantThumbnailWaiters.FindOrAdd(ThumbnailUrl);
	const bool bAlreadyWaiting = Waiters.ContainsByPredicate(
		[TargetImage, this](const FThumbnailTarget& Existing)
		{
			return Existing.Generation == ThumbnailViewGeneration
				&& Existing.Image.Get() == TargetImage;
		});
	if (!bAlreadyWaiting)
	{
		Waiters.Add({TargetImage, ThumbnailViewGeneration});
	}
	if (!PendingVariantThumbnailUrlSet.Contains(ThumbnailUrl))
	{
		PendingVariantThumbnailUrlSet.Add(ThumbnailUrl);
		PendingVariantThumbnailUrls.Add(ThumbnailUrl);
	}
}

void UConfiguratorPanel::PumpVariantThumbnailLoads()
{
	while (bAcceptThumbnailResults
		&& ActiveVariantThumbnailLoadCount < MaxConcurrentThumbnailLoads
		&& !PendingVariantThumbnailUrls.IsEmpty())
	{
		const FString ThumbnailUrl = PendingVariantThumbnailUrls[0];
		PendingVariantThumbnailUrls.RemoveAt(0, 1, EAllowShrinking::No);
		ActiveVariantThumbnailUrls.Add(ThumbnailUrl);
		++ActiveVariantThumbnailLoadCount;

		if (VariantThumbnailLoaderOverride)
		{
			UTexture2D* Texture = VariantThumbnailLoaderOverride(ThumbnailUrl);
			--ActiveVariantThumbnailLoadCount;
			ActiveVariantThumbnailUrls.Remove(ThumbnailUrl);
			PendingVariantThumbnailUrlSet.Remove(ThumbnailUrl);
			if (Texture != nullptr)
			{
				StoreVariantThumbnail(ThumbnailUrl, Texture);
			}
			else
			{
				RememberFailedVariantThumbnail(ThumbnailUrl);
			}
			TArray<FThumbnailTarget> Waiters;
			VariantThumbnailWaiters.RemoveAndCopyValue(ThumbnailUrl, Waiters);
			for (const FThumbnailTarget& Waiter : Waiters)
			{
				if (Waiter.Generation == ThumbnailViewGeneration
					&& Waiter.Image.IsValid()
					&& VisibleVariantThumbnailImages.Contains(Waiter.Image)
					&& Texture != nullptr)
				{
					Waiter.Image->SetBrushFromTexture(Texture);
					Waiter.Image->SetColorAndOpacity(FLinearColor::White);
				}
			}
			continue;
		}

		const TWeakObjectPtr<UConfiguratorPanel> WeakThis(this);
		const uint32 LifetimeSerial = ThumbnailLifetimeSerial;
		Async(EAsyncExecution::ThreadPool, [WeakThis, ThumbnailUrl, LifetimeSerial]()
		{
			FImage Preview;
			DecodeVariantThumbnailPreview(ThumbnailUrl, Preview);
			AsyncTask(ENamedThreads::GameThread, [
				WeakThis,
				ThumbnailUrl,
				LifetimeSerial,
				Preview = MoveTemp(Preview)]() mutable
			{
				if (UConfiguratorPanel* Panel = WeakThis.Get();
					Panel != nullptr
					&& Panel->bAcceptThumbnailResults
					&& Panel->ThumbnailLifetimeSerial == LifetimeSerial)
				{
					Panel->HandleVariantThumbnailDecoded(
						ThumbnailUrl,
						MoveTemp(Preview));
					Panel->PumpVariantThumbnailLoads();
				}
			});
		});
	}
}

void UConfiguratorPanel::HandleVariantThumbnailDecoded(
	const FString& ThumbnailUrl,
	FImage&& Preview)
{
	ActiveVariantThumbnailLoadCount = FMath::Max(
		0,
		ActiveVariantThumbnailLoadCount - 1);
	ActiveVariantThumbnailUrls.Remove(ThumbnailUrl);
	PendingVariantThumbnailUrlSet.Remove(ThumbnailUrl);

	UTexture2D* Texture = Preview.SizeX == ThumbnailPreviewSize
		&& Preview.SizeY == ThumbnailPreviewSize
		? FImageUtils::CreateTexture2DFromImage(Preview)
		: nullptr;
	if (Texture != nullptr)
	{
		StoreVariantThumbnail(ThumbnailUrl, Texture);
	}
	else
	{
		RememberFailedVariantThumbnail(ThumbnailUrl);
	}

	TArray<FThumbnailTarget> Waiters;
	VariantThumbnailWaiters.RemoveAndCopyValue(ThumbnailUrl, Waiters);
	for (const FThumbnailTarget& Waiter : Waiters)
	{
		if (Waiter.Generation != ThumbnailViewGeneration || !Waiter.Image.IsValid())
		{
			continue;
		}
		if (!VisibleVariantThumbnailImages.Contains(Waiter.Image))
		{
			continue;
		}
		if (Texture != nullptr)
		{
			Waiter.Image->SetBrushFromTexture(Texture);
			Waiter.Image->SetColorAndOpacity(FLinearColor::White);
			Waiter.Image->SetToolTipText(FText::GetEmpty());
		}
		else
		{
			Waiter.Image->SetToolTipText(NSLOCTEXT(
				"Configurator",
				"VariantThumbnailUnavailable",
				"色卡缩略图不可用"));
		}
	}
}

void UConfiguratorPanel::StoreVariantThumbnail(
	const FString& ThumbnailUrl,
	UTexture2D* Texture)
{
	if (Texture == nullptr)
	{
		return;
	}
	if (!VariantThumbnailCache.Contains(ThumbnailUrl)
		&& VariantThumbnailCacheOrder.Num() >= MaxVariantThumbnailCacheEntries)
	{
		VariantThumbnailCache.Remove(VariantThumbnailCacheOrder[0]);
		VariantThumbnailCacheOrder.RemoveAt(0, 1, EAllowShrinking::No);
	}
	VariantThumbnailCache.Add(ThumbnailUrl, Texture);
	TouchVariantThumbnail(ThumbnailUrl);
}

void UConfiguratorPanel::TouchVariantThumbnail(const FString& ThumbnailUrl)
{
	VariantThumbnailCacheOrder.RemoveSingle(ThumbnailUrl);
	VariantThumbnailCacheOrder.Add(ThumbnailUrl);
}

void UConfiguratorPanel::RememberFailedVariantThumbnail(const FString& ThumbnailUrl)
{
	if (FailedVariantThumbnailUrls.Contains(ThumbnailUrl))
	{
		return;
	}
	if (FailedVariantThumbnailOrder.Num() >= MaxFailedThumbnailCacheEntries)
	{
		FailedVariantThumbnailUrls.Remove(FailedVariantThumbnailOrder[0]);
		FailedVariantThumbnailOrder.RemoveAt(0);
	}
	FailedVariantThumbnailUrls.Add(ThumbnailUrl);
	FailedVariantThumbnailOrder.Add(ThumbnailUrl);
}

void UConfiguratorPanel::RefreshV2Navigation()
{
	if (StageButtons == nullptr)
	{
		return;
	}
	StageButtons->ClearChildren();
	if (Sc01V2State == nullptr || !Sc01V2State->IsInitialized())
	{
		V2StatusText->SetText(NSLOCTEXT(
			"Configurator", "V2Unavailable", "SC01 v2 catalog asset 不可用"));
		return;
	}
	const Sc01V2::FCatalog& Catalog = Sc01V2State->GetCatalogIndex().GetCatalog();
	if (SelectedCategoryId.IsEmpty() && !Catalog.Categories.IsEmpty())
	{
		SelectedCategoryId = Catalog.Categories[0].CategoryId;
	}
	for (const Sc01V2::FCategory& Category : Catalog.Categories)
	{
		StageButtons->AddChildToWrapBox(MakeDataButton(
			Category.DisplayName,
			TEXT("category"),
			Category.CategoryId));
	}
	RefreshComponentButtons();
}

void UConfiguratorPanel::RefreshComponentButtons()
{
	ComponentButtons->ClearChildren();
	const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
	const TArray<FString>* ComponentIds =
		Index.FindComponentIdsForCategory(SelectedCategoryId);
	if (ComponentIds == nullptr || ComponentIds->IsEmpty())
	{
		SelectedComponentId.Reset();
		RefreshSurfaceButtons();
		return;
	}
	if (!ComponentIds->Contains(SelectedComponentId))
	{
		SelectedComponentId = (*ComponentIds)[0];
	}
	for (const FString& ComponentId : *ComponentIds)
	{
		const Sc01V2::FComponent* Component = Index.FindComponent(ComponentId);
		ComponentButtons->AddChildToWrapBox(MakeDataButton(
			Component->DisplayName,
			TEXT("component"),
			ComponentId));
	}
	RefreshSurfaceButtons();
}

void UConfiguratorPanel::RefreshSurfaceButtons()
{
	SurfaceButtons->ClearChildren();
	const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
	const TArray<FString>* SurfaceIds =
		Index.FindSurfaceIdsForComponent(SelectedComponentId);
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
	ResetThumbnailViewState();
	if (Sc01V2State == nullptr)
	{
		return;
	}
	const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
	const TArray<FString>* OptionIds = Index.FindOptionIdsForSurface(SelectedSurfaceId);
	const FString SelectedOption = Sc01V2State->GetSelections().FindRef(SelectedSurfaceId);
	const Sc01V2::FSurface* Surface = Index.FindSurface(SelectedSurfaceId);
	if (Surface != nullptr && !Surface->bRequired)
	{
		OptionButtons->AddChildToVerticalBox(MakeDataButton(
			SelectedOption.IsEmpty() ? TEXT("● 不选装 · 默认 · ¥0") : TEXT("○ 不选装 · 默认 · ¥0"),
			TEXT("none"),
			SelectedSurfaceId))->SetPadding(FMargin(0, 1, 0, 5));
	}

	auto AddMaterialGroup = [this, &Index, OptionIds, &SelectedOption](
		const Sc01V2::FMaterialFamily* Family)
	{
		TArray<const Sc01V2::FOption*> GroupOptions;
		if (OptionIds != nullptr)
		{
			for (const FString& OptionId : *OptionIds)
			{
				const Sc01V2::FOption* Option = Index.FindOption(OptionId);
				const bool bMatches = Option != nullptr
					&& (Family == nullptr
						? !Option->MaterialFamilyId.IsSet()
						: Option->MaterialFamilyId.IsSet()
							&& Option->MaterialFamilyId.GetValue() == Family->MaterialFamilyId);
				if (bMatches)
				{
					GroupOptions.Add(Option);
				}
			}
		}
		if (GroupOptions.IsEmpty())
		{
			return;
		}

		const Sc01V2::FOption* Selected = Index.FindOption(SelectedOption);
		const bool bActive = Selected != nullptr
			&& (Family == nullptr
				? !Selected->MaterialFamilyId.IsSet()
				: Selected->MaterialFamilyId.IsSet()
					&& Selected->MaterialFamilyId.GetValue() == Family->MaterialFamilyId);
		UBorder* GroupBorder = WidgetTree->ConstructWidget<UBorder>();
		GroupBorder->SetPadding(FMargin(8));
		GroupBorder->SetBrushColor(
			bActive
				? FLinearColor(0.34f, 0.35f, 0.33f, 0.95f)
				: FLinearColor(0.08f, 0.09f, 0.085f, 0.9f));
		UVerticalBox* GroupContent = WidgetTree->ConstructWidget<UVerticalBox>();
		GroupBorder->SetContent(GroupContent);

		UTextBlock* GroupTitle = WidgetTree->ConstructWidget<UTextBlock>();
		GroupTitle->SetText(FText::FromString(
			(Family != nullptr ? Family->DisplayName : TEXT("其他"))
			+ (bActive ? TEXT(" · 当前材质") : TEXT(" · 可选材质"))));
		GroupTitle->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.84f, 0.8f)));
		GroupContent->AddChildToVerticalBox(GroupTitle)->SetPadding(FMargin(0, 0, 0, 4));

		UWrapBox* Choices = WidgetTree->ConstructWidget<UWrapBox>();
		Choices->SetInnerSlotPadding(FVector2D(4.0f, 4.0f));
		GroupContent->AddChildToVerticalBox(Choices);
		for (const Sc01V2::FOption* Option : GroupOptions)
		{
			const FString Marker = Option->OptionId == SelectedOption ? TEXT("● ") : TEXT("○ ");
			const FString Price = Option->Pricing.bIsStandard
				? TEXT("默认 · 免费")
				: Option->Pricing.UnitPriceMinor.IsSet()
					? FString::Printf(
						TEXT("+¥%.0f"),
						static_cast<double>(Option->Pricing.UnitPriceMinor.GetValue()) / 100.0)
					: TEXT("价格待确认");
			Choices->AddChildToWrapBox(MakeDataButton(
				Marker + Option->DisplayName + TEXT("\n") + Price,
				TEXT("option"),
				SelectedSurfaceId,
				Option->OptionId));
		}

		if (Family != nullptr)
		{
			const TArray<FString>* VariantIds =
				Index.FindVariantIdsForMaterialFamily(Family->MaterialFamilyId);
			const TMap<FString, FSc01V2Customization> Customizations =
				Sc01V2State->GetCustomizations();
			const FSc01V2Customization* Current = Customizations.Find(SelectedSurfaceId);
			for (const Sc01V2::FOption* ColorCardOption : GroupOptions)
			{
				if (!ColorCardOption->SupportsMaterialVariants()
					|| VariantIds == nullptr
					|| VariantIds->IsEmpty())
				{
					continue;
				}
				UTextBlock* ColorCardTitle = WidgetTree->ConstructWidget<UTextBlock>();
				ColorCardTitle->SetText(FText::FromString(
					ColorCardOption->DisplayName + TEXT(" · PDF 色卡")));
				ColorCardTitle->SetColorAndOpacity(
					FSlateColor(FLinearColor(0.72f, 0.78f, 0.72f)));
				GroupContent->AddChildToVerticalBox(ColorCardTitle)->SetPadding(
					FMargin(0, 5, 0, 1));
				UScrollBox* Strip = WidgetTree->ConstructWidget<UScrollBox>();
				Strip->SetOrientation(Orient_Horizontal);
				Strip->SetAnimateWheelScrolling(true);
				UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
				Strip->AddChild(Row);
				TArray<TPair<FString, TWeakObjectPtr<UImage>>> ThumbnailItems;
				ThumbnailItems.Reserve(VariantIds->Num());
				for (const FString& VariantId : *VariantIds)
				{
					const Sc01V2::FMaterialVariant* Variant =
						Index.FindMaterialVariant(VariantId);
					if (Variant == nullptr)
					{
						continue;
					}
					const bool bVariantSelected = Current != nullptr
						&& SelectedOption == ColorCardOption->OptionId
						&& Current->Kind == ESc01V2CustomizationKind::MaterialVariant
						&& Current->MaterialVariantId == VariantId;
					USizeBox* Compact = WidgetTree->ConstructWidget<USizeBox>();
					Compact->SetWidthOverride(82.0f);
					Compact->SetHeightOverride(34.0f);
					UImage* Swatch = nullptr;
					Compact->AddChild(MakeVariantButton(
						*Variant,
						ColorCardOption->OptionId,
						bVariantSelected,
						Swatch));
					ThumbnailItems.Emplace(
						Variant->ThumbnailUrl,
						TWeakObjectPtr<UImage>(Swatch));
					Row->AddChildToHorizontalBox(Compact)->SetPadding(FMargin(0, 4, 4, 0));
				}
				RegisterThumbnailStrip(Strip, MoveTemp(ThumbnailItems));
				GroupContent->AddChildToVerticalBox(Strip);
			}
		}
		OptionButtons->AddChildToVerticalBox(GroupBorder)->SetPadding(FMargin(0, 2));
	};

	const Sc01V2::FCatalog& Catalog = Index.GetCatalog();
	for (const Sc01V2::FMaterialFamily& Family : Catalog.MaterialFamilies)
	{
		AddMaterialGroup(&Family);
	}
	AddMaterialGroup(nullptr);
	RefreshPaintEditor();
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
	if (Action == TEXT("category"))
	{
		SelectedCategoryId = PrimaryId;
		SelectedComponentId.Reset();
		SelectedSurfaceId.Reset();
		QueueV2NavigationRefresh();
	}
	else if (Action == TEXT("component"))
	{
		SelectedComponentId = PrimaryId;
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
	else if (Action == TEXT("none"))
	{
		Sc01V2State->ClearOptionalSelection(PrimaryId);
	}
	else if (Action == TEXT("variant"))
	{
		const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
		const Sc01V2::FMaterialVariant* Variant = Index.FindMaterialVariant(SecondaryId);
		const Sc01V2::FOption* Option = Index.FindOption(PrimaryId);
		if (Variant != nullptr
			&& Option != nullptr
			&& Option->SurfaceId == SelectedSurfaceId
			&& Option->SupportsMaterialVariants()
			&& Option->MaterialFamilyId.IsSet()
			&& Option->MaterialFamilyId.GetValue() == Variant->MaterialFamilyId)
		{
			TMap<FString, FString> Selections = Sc01V2State->GetSelections();
			TMap<FString, FSc01V2Customization> Customizations =
				Sc01V2State->GetCustomizations();
			Selections.Add(SelectedSurfaceId, Option->OptionId);
			FSc01V2Customization Customization;
			Customization.Kind = ESc01V2CustomizationKind::MaterialVariant;
			Customization.MaterialVariantId = Variant->VariantId;
			Customizations.Add(SelectedSurfaceId, MoveTemp(Customization));
			Sc01V2State->ApplyTransaction(Selections, Customizations);
		}
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
	if (BasePriceText != nullptr
		&& OptionsPriceText != nullptr
		&& ReferenceTotalText != nullptr)
	{
		if (Sc01V2State != nullptr && Sc01V2State->IsInitialized())
		{
			const Sc01V2::FCatalogIndex& Index = Sc01V2State->GetCatalogIndex();
			const int64 BasePrice = Index.GetCatalog().BasePriceMinor;
			const int64 OptionsPrice = Sc01V2::CalculateOptionsPriceMinor(
				Sc01V2State->GetSelections(),
				Index);
			BasePriceText->SetText(FText::FromString(FString::Printf(
				TEXT("基础价    ¥ %.0f"),
				static_cast<double>(BasePrice) / 100.0)));
			OptionsPriceText->SetText(FText::FromString(FString::Printf(
				TEXT("选装合计  ¥ %.0f"),
				static_cast<double>(OptionsPrice) / 100.0)));
			ReferenceTotalText->SetText(FText::FromString(FString::Printf(
				TEXT("参考总价  ¥ %.0f"),
				static_cast<double>(BasePrice + OptionsPrice) / 100.0)));
		}
		else
		{
			const double Yuan = static_cast<double>(Configurator->GetTotalPriceMinor()) / 100.0;
			BasePriceText->SetText(FText::GetEmpty());
			OptionsPriceText->SetText(FText::GetEmpty());
			ReferenceTotalText->SetText(FText::FromString(
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
				TEXT("%d/38 已配置 · %s"),
				Sc01V2State->GetSelections().Num(),
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
