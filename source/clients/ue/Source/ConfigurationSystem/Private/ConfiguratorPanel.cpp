#include "ConfiguratorPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "WebBrowser.h"

namespace
{
	const TCHAR* WebConfiguratorSection = TEXT("ConfigurationSystem.WebConfigurator");
	const TCHAR* DefaultWebConfiguratorEndpoint = TEXT("127.0.0.1:8080");
	constexpr float ExpandedPanelWidth = 480.0f;
	constexpr float ToggleWidth = 34.0f;
	constexpr float HealthRequestTimeoutSeconds = 3.0f;
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
}

void UConfiguratorPanel::NativeDestruct()
{
	CancelHealthProbe();
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

	WebBrowser = WidgetTree->ConstructWidget<UWebBrowser>(
		UWebBrowser::StaticClass(), TEXT("EmbeddedWebConfigurator"));
	UHorizontalBoxSlot* BrowserSlot = Row->AddChildToHorizontalBox(WebBrowser);
	BrowserSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RefreshToggleLabel();
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
