#include "ConfiguratorPanel.h"

#include "ConfigShowroomPlayerController.h"
#include "CarConfiguratorSubsystem.h"
#include "ConfiguratorBrowserWidget.h"
#include "ConfiguratorWebBridge.h"
#include "PathTracingExperienceSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* WebConfiguratorSection = TEXT("ConfigurationSystem.WebConfigurator");
	const TCHAR* DefaultWebConfiguratorEndpoint = TEXT("127.0.0.1:8080");
	constexpr float ExpandedPanelWidth = 480.0f;
	constexpr float ToggleWidth = 34.0f;
	constexpr float HealthRequestTimeoutSeconds = 3.0f;
	constexpr int32 MaxBridgeJsonCharacters = 65536;

	bool HasOnlyFields(
		const TSharedPtr<FJsonObject>& Object,
		const TSet<FString>& AllowedFields)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
		{
			if (!AllowedFields.Contains(Field.Key))
			{
				return false;
			}
		}
		return true;
	}

	bool IsValidPaintColor(const FString& ColorHex)
	{
		if (ColorHex.Len() != 7 || ColorHex[0] != TEXT('#'))
		{
			return false;
		}
		for (int32 Index = 1; Index < ColorHex.Len(); ++Index)
		{
			if (!FChar::IsHexDigit(ColorHex[Index]))
			{
				return false;
			}
		}
		return true;
	}
}

FString UConfiguratorPanel::GetConfiguredWebUrl()
{
	FString Endpoint;
	if (GConfig != nullptr)
	{
		GConfig->GetString(
			WebConfiguratorSection,
			TEXT("Endpoint"),
			Endpoint,
			GGameIni);
	}
	Endpoint.TrimStartAndEndInline();
	const bool bLoopbackHost = Endpoint.Equals(TEXT("localhost"))
		|| Endpoint.StartsWith(TEXT("localhost:"))
		|| Endpoint.Equals(TEXT("127.0.0.1"))
		|| Endpoint.StartsWith(TEXT("127.0.0.1:"))
		|| Endpoint.Equals(TEXT("[::1]"))
		|| Endpoint.StartsWith(TEXT("[::1]:"));
	const bool bSafeLocalEndpoint = bLoopbackHost
		&& !Endpoint.Contains(TEXT("/"))
		&& !Endpoint.Contains(TEXT("?"))
		&& !Endpoint.Contains(TEXT("#"))
		&& !Endpoint.Contains(TEXT("@"))
		&& (Endpoint.StartsWith(TEXT("127.0.0.1"))
			|| Endpoint.StartsWith(TEXT("localhost"))
			|| Endpoint.StartsWith(TEXT("[::1]")));
	if (!bSafeLocalEndpoint)
	{
		Endpoint = DefaultWebConfiguratorEndpoint;
	}
	return FString::Printf(
		TEXT("http://%s/?source=ue&view=embedded"),
		*Endpoint);
}

FString UConfiguratorPanel::BuildHealthUrl(const FString& WebUrl)
{
	FString TrimmedUrl = WebUrl;
	TrimmedUrl.TrimStartAndEndInline();
	const int32 SchemeSeparator = TrimmedUrl.Find(TEXT("://"));
	if (SchemeSeparator <= 0)
	{
		return FString();
	}

	const FString Scheme = TrimmedUrl.Left(SchemeSeparator).ToLower();
	if (Scheme != TEXT("http") && Scheme != TEXT("https"))
	{
		return FString();
	}

	const int32 AuthorityStart = SchemeSeparator + 3;
	int32 AuthorityEnd = TrimmedUrl.Len();
	for (int32 Index = AuthorityStart; Index < TrimmedUrl.Len(); ++Index)
	{
		const TCHAR Character = TrimmedUrl[Index];
		if (Character == TEXT('/') || Character == TEXT('?') || Character == TEXT('#'))
		{
			AuthorityEnd = Index;
			break;
		}
	}
	if (AuthorityEnd <= AuthorityStart)
	{
		return FString();
	}
	return TrimmedUrl.Left(AuthorityEnd) + TEXT("/health");
}

float UConfiguratorPanel::GetHealthRetryDelaySeconds(
	const int32 CompletedAttemptCount)
{
	if (CompletedAttemptCount < 1
		|| CompletedAttemptCount >= MaxHealthProbeAttempts)
	{
		return 0.0f;
	}
	return static_cast<float>(1 << (CompletedAttemptCount - 1));
}

bool UConfiguratorPanel::IsSupportedQuality(const FString& Quality)
{
	return Quality == TEXT("low")
		|| Quality == TEXT("medium")
		|| Quality == TEXT("high")
		|| Quality == TEXT("epic");
}

bool UConfiguratorPanel::ParseWebConfigurationJson(
	const FString& ConfigurationJson,
	TMap<FString, FString>& OutSelections,
	TMap<FString, FSc01V2Customization>& OutCustomizations,
	FString& OutError)
{
	OutSelections.Reset();
	OutCustomizations.Reset();
	OutError.Reset();
	if (ConfigurationJson.IsEmpty()
		|| ConfigurationJson.Len() > MaxBridgeJsonCharacters)
	{
		OutError = TEXT("选配 JSON 为空或超过 64 KiB 限制。");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(ConfigurationJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("选配 JSON 不是有效对象。");
		return false;
	}
	const TSet<FString> RootFields = {
		TEXT("schemaVersion"),
		TEXT("selections"),
		TEXT("customizations")
	};
	if (!HasOnlyFields(Root, RootFields))
	{
		OutError = TEXT("选配 JSON 包含非白名单根字段。");
		return false;
	}

	FString SchemaVersion;
	const TSharedPtr<FJsonObject>* SelectionsObject = nullptr;
	const TSharedPtr<FJsonObject>* CustomizationsObject = nullptr;
	if (!Root->TryGetStringField(TEXT("schemaVersion"), SchemaVersion)
		|| SchemaVersion != TEXT("2.0.0")
		|| !Root->TryGetObjectField(TEXT("selections"), SelectionsObject)
		|| !Root->TryGetObjectField(TEXT("customizations"), CustomizationsObject)
		|| SelectionsObject == nullptr || !SelectionsObject->IsValid()
		|| CustomizationsObject == nullptr || !CustomizationsObject->IsValid())
	{
		OutError = TEXT("选配 JSON 必须包含 v2 schemaVersion、selections 与 customizations。");
		return false;
	}
	if ((*SelectionsObject)->Values.Num() > 64
		|| (*CustomizationsObject)->Values.Num() > 64)
	{
		OutError = TEXT("选配 JSON 的 surface 数量超过限制。");
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair :
		(*SelectionsObject)->Values)
	{
		FString OptionId;
		if (Pair.Key.IsEmpty() || Pair.Key.Len() > 128
			|| !Pair.Value.IsValid()
			|| !Pair.Value->TryGetString(OptionId)
			|| OptionId.IsEmpty() || OptionId.Len() > 128)
		{
			OutError = TEXT("selections 只允许非空 surfaceId 到 optionId 字符串映射。");
			return false;
		}
		OutSelections.Add(Pair.Key, MoveTemp(OptionId));
	}

	const TSet<FString> MaterialFields = {TEXT("materialVariantId")};
	const TSet<FString> PaintFields = {
		TEXT("colorHex"),
		TEXT("metallic"),
		TEXT("roughness"),
		TEXT("clearCoat"),
		TEXT("orangePeel"),
		TEXT("flakeIntensity")
	};
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair :
		(*CustomizationsObject)->Values)
	{
		const TSharedPtr<FJsonObject>* ValueObject = nullptr;
		if (Pair.Key.IsEmpty() || Pair.Key.Len() > 128
			|| !Pair.Value.IsValid()
			|| !Pair.Value->TryGetObject(ValueObject)
			|| ValueObject == nullptr || !ValueObject->IsValid())
		{
			OutError = TEXT("customizations 只允许 surfaceId 到对象的映射。");
			return false;
		}

		FSc01V2Customization Customization;
		if (HasOnlyFields(*ValueObject, MaterialFields)
			&& (*ValueObject)->Values.Num() == 1)
		{
			if (!(*ValueObject)->TryGetStringField(
				TEXT("materialVariantId"),
				Customization.MaterialVariantId)
				|| Customization.MaterialVariantId.IsEmpty()
				|| Customization.MaterialVariantId.Len() > 128)
			{
				OutError = TEXT("materialVariantId 必须是非空字符串。");
				return false;
			}
			Customization.Kind = ESc01V2CustomizationKind::MaterialVariant;
		}
		else if (HasOnlyFields(*ValueObject, PaintFields)
			&& (*ValueObject)->Values.Num() == PaintFields.Num())
		{
			Customization.Kind = ESc01V2CustomizationKind::Paint;
			FSc01V2PaintCustomization& Paint = Customization.Paint;
			if (!(*ValueObject)->TryGetStringField(TEXT("colorHex"), Paint.ColorHex)
				|| !(*ValueObject)->TryGetNumberField(TEXT("metallic"), Paint.Metallic)
				|| !(*ValueObject)->TryGetNumberField(TEXT("roughness"), Paint.Roughness)
				|| !(*ValueObject)->TryGetNumberField(TEXT("clearCoat"), Paint.ClearCoat)
				|| !(*ValueObject)->TryGetNumberField(TEXT("orangePeel"), Paint.OrangePeel)
				|| !(*ValueObject)->TryGetNumberField(
					TEXT("flakeIntensity"), Paint.FlakeIntensity)
				|| !IsValidPaintColor(Paint.ColorHex)
				|| Paint.Metallic < 0.0 || Paint.Metallic > 1.0
				|| Paint.Roughness < 0.0 || Paint.Roughness > 1.0
				|| Paint.ClearCoat < 0.0 || Paint.ClearCoat > 1.0
				|| Paint.OrangePeel < 0.0 || Paint.OrangePeel > 1.0
				|| Paint.FlakeIntensity < 0.0 || Paint.FlakeIntensity > 1.0)
			{
				OutError = TEXT("车漆定制必须包含合法颜色和 0..1 数值参数。");
				return false;
			}
			Paint.ColorHex = Paint.ColorHex.ToUpper();
		}
		else
		{
			OutError = TEXT("定制对象包含非白名单字段或混合了两种定制类型。");
			return false;
		}
		OutCustomizations.Add(Pair.Key, MoveTemp(Customization));
	}
	return true;
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
	if (ToggleButton != nullptr)
	{
		ToggleButton->OnClicked.AddUniqueDynamic(
			this,
			&UConfiguratorPanel::ToggleWebConfigurator);
	}
	ApplyExpandedState(true, true);
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPathTracingExperienceSubsystem* PathTracing =
			GameInstance->GetSubsystem<UPathTracingExperienceSubsystem>())
		{
			PathTracing->OnWarmupStateChanged.AddUniqueDynamic(
				this,
				&UConfiguratorPanel::HandlePathTracingWarmupStateChanged);
		}
	}
	RefreshExperienceControls();
}

void UConfiguratorPanel::NativeDestruct()
{
	CancelHealthProbe();
	WebBridge = nullptr;
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UPathTracingExperienceSubsystem* PathTracing =
			GameInstance->GetSubsystem<UPathTracingExperienceSubsystem>())
		{
			PathTracing->OnWarmupStateChanged.RemoveDynamic(
				this,
				&UConfiguratorPanel::HandlePathTracingWarmupStateChanged);
		}
	}
	if (ToggleButton != nullptr)
	{
		ToggleButton->OnClicked.RemoveDynamic(
			this,
			&UConfiguratorPanel::ToggleWebConfigurator);
	}
	Super::NativeDestruct();
}

void UConfiguratorPanel::BuildWidgetTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	PanelSize = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("WebConfiguratorPanelSize"));
	PanelSize->SetWidthOverride(ExpandedPanelWidth);
	PanelCanvasSlot = Root->AddChildToCanvas(PanelSize);
	PanelCanvasSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
	PanelCanvasSlot->SetAlignment(FVector2D(1.0f, 0.0f));
	PanelCanvasSlot->SetOffsets(FMargin(0.0f, 0.0f, ExpandedPanelWidth, 0.0f));

	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("WebConfiguratorPanelBackground"));
	Background->SetBrushColor(FLinearColor(0.025f, 0.03f, 0.028f, 0.94f));
	Background->SetPadding(FMargin(0.0f));
	PanelSize->AddChild(Background);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("WebConfiguratorPanelRow"));
	Background->SetContent(Row);

	USizeBox* ToggleSize = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("WebConfiguratorToggleSize"));
	ToggleSize->SetWidthOverride(ToggleWidth);
	UHorizontalBoxSlot* ToggleSlot = Row->AddChildToHorizontalBox(ToggleSize);
	ToggleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));

	ToggleButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("WebConfiguratorToggle"));
	ToggleButton->SetBackgroundColor(FLinearColor(0.16f, 0.2f, 0.17f, 1.0f));
	ToggleSize->AddChild(ToggleButton);

	ToggleLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("WebConfiguratorToggleLabel"));
	ToggleLabel->SetJustification(ETextJustify::Center);
	ToggleLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	ToggleLabel->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 20));
	ToggleButton->AddChild(ToggleLabel);

	WebBrowser = WidgetTree->ConstructWidget<UConfiguratorBrowserWidget>(
		UConfiguratorBrowserWidget::StaticClass(), TEXT("EmbeddedWebConfigurator"));
	WebBridge = NewObject<UConfiguratorWebBridge>(this);
	WebBridge->Initialize(this);
	WebBrowser->SetBridge(WebBridge);
	UHorizontalBoxSlot* BrowserSlot = Row->AddChildToHorizontalBox(WebBrowser);
	BrowserSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RefreshToggleLabel();
	BuildExperienceControls(Root);
}

UButton* UConfiguratorPanel::CreateToolbarButton(
	UHorizontalBox* Toolbar,
	const FName Name,
	const FText& Label,
	TObjectPtr<UTextBlock>& OutLabel)
{
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), *FString::Printf(TEXT("%sSize"), *Name.ToString()));
	Size->SetWidthOverride(78.0f);
	Size->SetHeightOverride(54.0f);
	UHorizontalBoxSlot* ToolbarSlot = Toolbar->AddChildToHorizontalBox(Size);
	ToolbarSlot->SetPadding(FMargin(1.0f));

	UButton* Button = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), Name);
	Button->SetBackgroundColor(FLinearColor(0.08f, 0.08f, 0.075f, 1.0f));
	Size->AddChild(Button);
	OutLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), *FString::Printf(TEXT("%sLabel"), *Name.ToString()));
	OutLabel->SetText(Label);
	OutLabel->SetJustification(ETextJustify::Center);
	OutLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.83f)));
	OutLabel->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 10));
	Button->AddChild(OutLabel);
	return Button;
}

void UConfiguratorPanel::BuildExperienceControls(UCanvasPanel* Root)
{
	UBorder* ToolbarBackground = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("ExperienceToolbarBackground"));
	ToolbarBackground->SetBrushColor(FLinearColor(0.035f, 0.035f, 0.033f, 0.96f));
	ToolbarBackground->SetPadding(FMargin(5.0f));
	UCanvasPanelSlot* ToolbarCanvasSlot = Root->AddChildToCanvas(ToolbarBackground);
	ToolbarCanvasSlot->SetAnchors(FAnchors(0.5f, 1.0f));
	ToolbarCanvasSlot->SetAlignment(FVector2D(0.5f, 1.0f));
	// The browser owns the right-side configurator column. Keep the native
	// presentation controls centered inside the remaining live viewport.
	ToolbarCanvasSlot->SetPosition(FVector2D(-ExpandedPanelWidth * 0.5f, -24.0f));
	ToolbarCanvasSlot->SetAutoSize(true);

	UHorizontalBox* Toolbar = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ExperienceToolbar"));
	ToolbarBackground->SetContent(Toolbar);
	UButton* CameraButton = CreateToolbarButton(
		Toolbar,
		TEXT("ExperienceCamera"),
		NSLOCTEXT("Configurator", "ExperienceCamera", "◉\n镜头"),
		CameraLabel);
	AnimationButton = CreateToolbarButton(
		Toolbar,
		TEXT("ExperienceAnimation"),
		NSLOCTEXT("Configurator", "ExperienceAnimation", "▷\n动画"),
		AnimationLabel);
	LightButton = CreateToolbarButton(
		Toolbar,
		TEXT("ExperienceLight"),
		NSLOCTEXT("Configurator", "ExperienceLight", "☼\n灯光"),
		LightLabel);
	RenderButton = CreateToolbarButton(
		Toolbar,
		TEXT("ExperienceRender"),
		NSLOCTEXT("Configurator", "ExperienceRender", "◇\n渲染"),
		RenderLabel);
	TObjectPtr<UTextBlock> ResetLabel = nullptr;
	UButton* ResetButton = CreateToolbarButton(
		Toolbar,
		TEXT("ExperienceReset"),
		NSLOCTEXT("Configurator", "ExperienceReset", "↺\n复位"),
		ResetLabel);
	FullscreenButton = CreateToolbarButton(
		Toolbar,
		TEXT("ExperienceFullscreen"),
		NSLOCTEXT("Configurator", "ExperienceFullscreen", "□\n全屏"),
		FullscreenLabel);

	CameraButton->OnClicked.AddUniqueDynamic(
		this, &UConfiguratorPanel::HandleCameraClicked);
	AnimationButton->OnClicked.AddUniqueDynamic(
		this, &UConfiguratorPanel::HandleAnimationClicked);
	LightButton->OnClicked.AddUniqueDynamic(
		this, &UConfiguratorPanel::HandleLightClicked);
	RenderButton->OnClicked.AddUniqueDynamic(
		this, &UConfiguratorPanel::HandleRenderClicked);
	ResetButton->OnClicked.AddUniqueDynamic(
		this, &UConfiguratorPanel::HandleResetClicked);
	FullscreenButton->OnClicked.AddUniqueDynamic(
		this, &UConfiguratorPanel::HandleFullscreenClicked);

	UVerticalBox* QualityColumn = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("QualityColumn"));
	UCanvasPanelSlot* QualityCanvasSlot = Root->AddChildToCanvas(QualityColumn);
	QualityCanvasSlot->SetAnchors(FAnchors(1.0f, 0.0f));
	QualityCanvasSlot->SetAlignment(FVector2D(1.0f, 0.0f));
	// Keep the UE-only settings affordance on the live render surface instead
	// of layering it over the CEF panel, which would also steal pointer input.
	QualityCanvasSlot->SetPosition(
		FVector2D(-(ExpandedPanelWidth + 20.0f), 20.0f));
	QualityCanvasSlot->SetAutoSize(true);

	USizeBox* GearSize = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("QualityGearSize"));
	GearSize->SetWidthOverride(42.0f);
	GearSize->SetHeightOverride(42.0f);
	UVerticalBoxSlot* GearSlot = QualityColumn->AddChildToVerticalBox(GearSize);
	GearSlot->SetHorizontalAlignment(HAlign_Right);
	UButton* GearButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("QualityGear"));
	GearButton->SetBackgroundColor(FLinearColor(0.97f, 0.97f, 0.95f, 0.96f));
	GearButton->SetToolTipText(
		NSLOCTEXT("Configurator", "QualityGearHint", "画质设置"));
	GearSize->AddChild(GearButton);
	UTextBlock* GearLabel = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("QualityGearLabel"));
	GearLabel->SetText(FText::FromString(TEXT("⚙")));
	GearLabel->SetJustification(ETextJustify::Center);
	GearLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.08f, 0.08f, 0.07f)));
	GearLabel->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 17));
	GearButton->AddChild(GearLabel);
	GearButton->OnClicked.AddUniqueDynamic(
		this, &UConfiguratorPanel::HandleQualityMenuClicked);

	QualityPopup = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("QualityPopup"));
	QualityPopup->SetBrushColor(FLinearColor(0.97f, 0.97f, 0.95f, 0.98f));
	QualityPopup->SetPadding(FMargin(6.0f));
	UVerticalBoxSlot* PopupSlot = QualityColumn->AddChildToVerticalBox(QualityPopup);
	PopupSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	PopupSlot->SetHorizontalAlignment(HAlign_Right);
	UVerticalBox* QualityOptions = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("QualityOptions"));
	QualityPopup->SetContent(QualityOptions);

	auto AddQualityButton = [this, QualityOptions](
		const FName Name,
		const FText& Label) -> UButton*
	{
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), *FString::Printf(TEXT("%sSize"), *Name.ToString()));
		Size->SetWidthOverride(148.0f);
		Size->SetHeightOverride(34.0f);
		QualityOptions->AddChildToVerticalBox(Size);
		UButton* Button = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), Name);
		Button->SetBackgroundColor(FLinearColor::Transparent);
		Size->AddChild(Button);
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("%sLabel"), *Name.ToString()));
		Text->SetText(Label);
		Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.12f, 0.12f, 0.11f)));
		Text->SetFont(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 11));
		Button->AddChild(Text);
		return Button;
	};
	UButton* Low = AddQualityButton(
		TEXT("QualityLow"), NSLOCTEXT("Configurator", "QualityLow", "流畅"));
	UButton* Medium = AddQualityButton(
		TEXT("QualityMedium"), NSLOCTEXT("Configurator", "QualityMedium", "均衡"));
	UButton* High = AddQualityButton(
		TEXT("QualityHigh"), NSLOCTEXT("Configurator", "QualityHigh", "高"));
	UButton* Epic = AddQualityButton(
		TEXT("QualityEpic"), NSLOCTEXT("Configurator", "QualityEpic", "极致"));
	Low->OnClicked.AddUniqueDynamic(this, &UConfiguratorPanel::HandleQualityLowClicked);
	Medium->OnClicked.AddUniqueDynamic(this, &UConfiguratorPanel::HandleQualityMediumClicked);
	High->OnClicked.AddUniqueDynamic(this, &UConfiguratorPanel::HandleQualityHighClicked);
	Epic->OnClicked.AddUniqueDynamic(this, &UConfiguratorPanel::HandleQualityEpicClicked);
	QualityPopup->SetVisibility(ESlateVisibility::Collapsed);
}

void UConfiguratorPanel::ToggleWebConfigurator()
{
	if (bExpanded && HealthProbeState == EHealthProbeState::Failed)
	{
		StartHealthProbe();
		return;
	}
	ApplyExpandedState(!bExpanded, !bExpanded);
}

void UConfiguratorPanel::ApplyExpandedState(
	const bool bShouldExpand,
	const bool bReloadPage)
{
	bExpanded = bShouldExpand;
	if (!bExpanded)
	{
		CancelHealthProbe();
	}
	if (WebBrowser != nullptr)
	{
		WebBrowser->SetVisibility(
			bExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bExpanded && bReloadPage)
		{
			StartHealthProbe();
		}
	}
	if (PanelSize != nullptr)
	{
		PanelSize->SetWidthOverride(
			bExpanded ? ExpandedPanelWidth : ToggleWidth);
	}
	if (PanelCanvasSlot != nullptr)
	{
		PanelCanvasSlot->SetSize(FVector2D(
			bExpanded ? ExpandedPanelWidth : ToggleWidth,
			PanelCanvasSlot->GetSize().Y));
	}
	RefreshToggleLabel();
}

void UConfiguratorPanel::StartHealthProbe()
{
	CancelHealthProbe();
	if (!bExpanded)
	{
		return;
	}
	HealthProbeAttemptCount = 0;
	PendingRetryDelaySeconds = 0.0f;
	HealthProbeState = EHealthProbeState::Waiting;
	RefreshToggleLabel();
	IssueHealthProbe();
}

void UConfiguratorPanel::IssueHealthProbe()
{
	if (!bExpanded)
	{
		return;
	}

	const FString HealthUrl = BuildHealthUrl(GetConfiguredWebUrl());
	if (HealthUrl.IsEmpty())
	{
		HealthProbeAttemptCount = MaxHealthProbeAttempts;
		HandleHealthProbeFailure();
		return;
	}

	++HealthProbeAttemptCount;
	PendingRetryDelaySeconds = 0.0f;
	HealthProbeState = EHealthProbeState::Waiting;
	RefreshToggleLabel();

	ActiveHealthRequest = FHttpModule::Get().CreateRequest();
	ActiveHealthRequest->SetURL(HealthUrl);
	ActiveHealthRequest->SetVerb(TEXT("GET"));
	ActiveHealthRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	ActiveHealthRequest->SetTimeout(HealthRequestTimeoutSeconds);
	ActiveHealthRequest->OnProcessRequestComplete().BindUObject(
		this,
		&UConfiguratorPanel::HandleHealthProbeCompleted);
	if (!ActiveHealthRequest->ProcessRequest())
	{
		ActiveHealthRequest->OnProcessRequestComplete().Unbind();
		ActiveHealthRequest.Reset();
		HandleHealthProbeFailure();
	}
}

void UConfiguratorPanel::HandleHealthProbeCompleted(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	const bool bConnectedSuccessfully)
{
	if (Request != ActiveHealthRequest)
	{
		return;
	}
	ActiveHealthRequest->OnProcessRequestComplete().Unbind();
	ActiveHealthRequest.Reset();

	const bool bHealthy = bConnectedSuccessfully
		&& Response.IsValid()
		&& Response->GetResponseCode() >= 200
		&& Response->GetResponseCode() < 300;
	if (bHealthy && bExpanded)
	{
		HealthProbeState = EHealthProbeState::Ready;
		PendingRetryDelaySeconds = 0.0f;
		RefreshToggleLabel();
		if (WebBrowser != nullptr)
		{
			WebBrowser->LoadURL(GetConfiguredWebUrl());
		}
		return;
	}
	HandleHealthProbeFailure();
}

void UConfiguratorPanel::HandleHealthProbeFailure()
{
	if (!bExpanded)
	{
		return;
	}

	PendingRetryDelaySeconds =
		GetHealthRetryDelaySeconds(HealthProbeAttemptCount);
	if (PendingRetryDelaySeconds <= 0.0f)
	{
		HealthProbeState = EHealthProbeState::Failed;
		RefreshToggleLabel();
		return;
	}

	HealthProbeState = EHealthProbeState::Waiting;
	RefreshToggleLabel();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			HealthRetryTimer,
			this,
			&UConfiguratorPanel::IssueHealthProbe,
			PendingRetryDelaySeconds,
			false);
	}
	else
	{
		HealthProbeState = EHealthProbeState::Failed;
		PendingRetryDelaySeconds = 0.0f;
		RefreshToggleLabel();
	}
}

void UConfiguratorPanel::CancelHealthProbe()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HealthRetryTimer);
	}
	HealthRetryTimer.Invalidate();
	if (ActiveHealthRequest.IsValid())
	{
		FHttpRequestPtr RequestToCancel = ActiveHealthRequest;
		ActiveHealthRequest.Reset();
		RequestToCancel->OnProcessRequestComplete().Unbind();
		RequestToCancel->CancelRequest();
	}
	HealthProbeAttemptCount = 0;
	PendingRetryDelaySeconds = 0.0f;
	HealthProbeState = EHealthProbeState::Idle;
	RefreshToggleLabel();
}

void UConfiguratorPanel::RefreshToggleLabel()
{
	if (ToggleLabel != nullptr)
	{
		ToggleLabel->SetText(bExpanded
			? NSLOCTEXT("Configurator", "CollapseWebConfigurator", "›")
			: NSLOCTEXT("Configurator", "ExpandWebConfigurator", "‹"));
		FText ToolTip;
		if (!bExpanded)
		{
			ToolTip = NSLOCTEXT(
				"Configurator",
				"ExpandWebConfiguratorHint",
				"展开网页选配");
		}
		else if (HealthProbeState == EHealthProbeState::Failed)
		{
			ToolTip = NSLOCTEXT(
				"Configurator",
				"RetryWebConfiguratorHint",
				"网页服务连接失败，点击重试");
		}
		else if (HealthProbeState == EHealthProbeState::Waiting)
		{
			ToolTip = PendingRetryDelaySeconds > 0.0f
				? FText::Format(
					NSLOCTEXT(
						"Configurator",
						"WaitingToRetryWebConfiguratorHint",
						"网页服务未就绪，{0} 秒后重试（第 {1}/5 次）；点击折叠可取消"),
					FText::AsNumber(FMath::RoundToInt(PendingRetryDelaySeconds)),
					FText::AsNumber(HealthProbeAttemptCount))
				: FText::Format(
					NSLOCTEXT(
						"Configurator",
						"WaitingForWebConfiguratorHint",
						"正在等待网页服务（第 {0}/5 次）；点击折叠可取消"),
					FText::AsNumber(HealthProbeAttemptCount));
		}
		else
		{
			ToolTip = NSLOCTEXT(
				"Configurator",
				"CollapseWebConfiguratorHint",
				"折叠网页选配");
		}
		ToggleLabel->SetToolTipText(ToolTip);
		if (ToggleButton != nullptr)
		{
			ToggleButton->SetToolTipText(ToolTip);
		}
	}
}

void UConfiguratorPanel::ApplyWebConfigurationJson(
	const FString& ConfigurationJson)
{
	TMap<FString, FString> Selections;
	TMap<FString, FSc01V2Customization> Customizations;
	FString Error;
	if (!ParseWebConfigurationJson(
		ConfigurationJson, Selections, Customizations, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("拒绝 Web 选配 JSON：%s"), *Error);
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UCarConfiguratorSubsystem* Configurator = IsValid(GameInstance)
		? GameInstance->GetSubsystem<UCarConfiguratorSubsystem>()
		: nullptr;
	USc01V2ConfigurationState* State = IsValid(Configurator)
		? Configurator->GetSc01V2State()
		: nullptr;
	if (!IsValid(State) || !State->ApplyTransaction(Selections, Customizations))
	{
		const FString StateError = IsValid(State)
			? State->GetLastErrorCode()
			: TEXT("SC01_V2_STATE_UNAVAILABLE");
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Web 选配 JSON 未能应用到 UE v2 状态：%s"),
			*StateError);
	}
}

void UConfiguratorPanel::SetExperienceError(const FString& Error)
{
	ExperienceError = Error;
	if (!ExperienceError.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("原生体验控制：%s"), *ExperienceError);
	}
}

void UConfiguratorPanel::RefreshExperienceControls()
{
	const AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	const UGameUserSettings* Settings =
		GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (CameraLabel != nullptr)
	{
		CameraLabel->SetText(FText::Format(
			NSLOCTEXT("Configurator", "ExperienceCameraState", "◉\n镜头 {0}"),
			FText::AsNumber(Controller != nullptr
				? Controller->GetCurrentCameraIndex() + 1 : 1)));
	}
	const bool bAnimation = Controller != nullptr && Controller->IsAnimationEnabled();
	const bool bOutdoor = Controller != nullptr
		&& Controller->GetLightPreset() == TEXT("outdoor");
	const bool bPathTracing = Controller != nullptr
		&& Controller->GetRenderMode() == TEXT("path-tracing");
	const UGameInstance* GameInstance = GetGameInstance();
	const UPathTracingExperienceSubsystem* PathTracing =
		IsValid(GameInstance)
		? GameInstance->GetSubsystem<UPathTracingExperienceSubsystem>()
		: nullptr;
	const bool bPathTracingPreparing =
		IsValid(PathTracing) && PathTracing->IsPreparingPathTracing();
	const bool bFullscreen = Settings != nullptr
		&& Settings->GetFullscreenMode() != EWindowMode::Windowed;
	if (AnimationLabel != nullptr)
	{
		AnimationLabel->SetText(bAnimation
			? NSLOCTEXT("Configurator", "ExperienceAnimationOn", "▷\n动画 开")
			: NSLOCTEXT("Configurator", "ExperienceAnimationOff", "▷\n动画"));
	}
	if (LightLabel != nullptr)
	{
		LightLabel->SetText(bOutdoor
			? NSLOCTEXT("Configurator", "ExperienceLightOutdoor", "☼\n灯光 户外")
			: NSLOCTEXT("Configurator", "ExperienceLightStudio", "☼\n灯光"));
	}
	if (RenderLabel != nullptr)
	{
		RenderLabel->SetText(bPathTracingPreparing
			? NSLOCTEXT("Configurator", "ExperienceRenderPreparing", "◇\n准备中")
			: bPathTracing
			? NSLOCTEXT("Configurator", "ExperienceRenderPathTracing", "◇\n渲染 光追")
			: NSLOCTEXT("Configurator", "ExperienceRenderRealtime", "◇\n渲染"));
	}
	if (FullscreenLabel != nullptr)
	{
		FullscreenLabel->SetText(bFullscreen
			? NSLOCTEXT("Configurator", "ExperienceFullscreenOn", "□\n全屏 开")
			: NSLOCTEXT("Configurator", "ExperienceFullscreenOff", "□\n全屏"));
	}
	const FLinearColor ActiveColor(0.42f, 0.01f, 0.045f, 1.0f);
	const FLinearColor IdleColor(0.08f, 0.08f, 0.075f, 1.0f);
	if (AnimationButton != nullptr)
	{
		AnimationButton->SetBackgroundColor(bAnimation ? ActiveColor : IdleColor);
	}
	if (LightButton != nullptr)
	{
		LightButton->SetBackgroundColor(bOutdoor ? ActiveColor : IdleColor);
	}
	if (RenderButton != nullptr)
	{
		RenderButton->SetBackgroundColor(bPathTracing ? ActiveColor : IdleColor);
		RenderButton->SetIsEnabled(!bPathTracingPreparing);
	}
	if (FullscreenButton != nullptr)
	{
		FullscreenButton->SetBackgroundColor(bFullscreen ? ActiveColor : IdleColor);
	}
	if (QualityPopup != nullptr)
	{
		QualityPopup->SetVisibility(
			bQualityMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UConfiguratorPanel::HandleCameraClicked()
{
	SetExperienceError(FString());
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	const int32 NextCamera = Controller != nullptr
		? (Controller->GetCurrentCameraIndex() + 1) % 6 : 0;
	if (Controller == nullptr || !Controller->SetCamera(NextCamera))
	{
		SetExperienceError(TEXT("镜头未能切换。"));
	}
	RefreshExperienceControls();
}

void UConfiguratorPanel::HandleAnimationClicked()
{
	SetExperienceError(FString());
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	const bool bNext = Controller == nullptr || !Controller->IsAnimationEnabled();
	if (Controller == nullptr || !Controller->SetAnimationEnabled(bNext))
	{
		SetExperienceError(TEXT("动画状态未能应用。"));
	}
	RefreshExperienceControls();
}

void UConfiguratorPanel::HandleLightClicked()
{
	SetExperienceError(FString());
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	const FString NextPreset = Controller != nullptr
		&& Controller->GetLightPreset() == TEXT("studio")
		? TEXT("outdoor") : TEXT("studio");
	if (Controller == nullptr || !Controller->SetLightPreset(NextPreset))
	{
		SetExperienceError(TEXT("灯光预设未能应用。"));
	}
	RefreshExperienceControls();
}

void UConfiguratorPanel::HandleRenderClicked()
{
	SetExperienceError(FString());
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	const FString NextMode = Controller != nullptr
		&& Controller->GetRenderMode() == TEXT("realtime")
		? TEXT("path-tracing") : TEXT("realtime");
	FString Error;
	if (Controller == nullptr || !Controller->SetRenderMode(NextMode, Error))
	{
		SetExperienceError(Error.IsEmpty() ? TEXT("渲染模式未能应用。") : Error);
	}
	RefreshExperienceControls();
}

void UConfiguratorPanel::HandlePathTracingWarmupStateChanged()
{
	RefreshExperienceControls();
}

void UConfiguratorPanel::HandleResetClicked()
{
	SetExperienceError(FString());
	if (AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer()))
	{
		Controller->ResetPresentation();
	}
	else
	{
		SetExperienceError(TEXT("体验控制器不可用。"));
	}
	RefreshExperienceControls();
}

void UConfiguratorPanel::HandleFullscreenClicked()
{
	SetExperienceError(FString());
	UGameUserSettings* Settings =
		GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Settings == nullptr)
	{
		SetExperienceError(TEXT("显示设置不可用。"));
	}
	else
	{
		const bool bEnable =
			Settings->GetFullscreenMode() == EWindowMode::Windowed;
		Settings->SetFullscreenMode(
			bEnable ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
		Settings->ApplySettings(false);
	}
	RefreshExperienceControls();
}

void UConfiguratorPanel::HandleQualityMenuClicked()
{
	bQualityMenuOpen = !bQualityMenuOpen;
	RefreshExperienceControls();
}

bool UConfiguratorPanel::ApplyQualityLevel(const FString& Quality)
{
	UGameUserSettings* Settings =
		GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (!IsSupportedQuality(Quality) || Settings == nullptr)
	{
		SetExperienceError(TEXT("画质等级不受支持或显示设置不可用。"));
		return false;
	}
	const int32 Level = Quality == TEXT("low") ? 0
		: Quality == TEXT("medium") ? 1
		: Quality == TEXT("high") ? 2
		: 3;
	Settings->SetOverallScalabilityLevel(Level);
	Settings->ApplySettings(false);
	SetExperienceError(FString());
	bQualityMenuOpen = false;
	RefreshExperienceControls();
	return true;
}

void UConfiguratorPanel::HandleQualityLowClicked()
{
	ApplyQualityLevel(TEXT("low"));
}

void UConfiguratorPanel::HandleQualityMediumClicked()
{
	ApplyQualityLevel(TEXT("medium"));
}

void UConfiguratorPanel::HandleQualityHighClicked()
{
	ApplyQualityLevel(TEXT("high"));
}

void UConfiguratorPanel::HandleQualityEpicClicked()
{
	ApplyQualityLevel(TEXT("epic"));
}
