#include "ConfiguratorPanel.h"

#include "ConfigShowroomPlayerController.h"
#include "CarConfiguratorSubsystem.h"
#include "ConfiguratorBrowserWidget.h"
#include "ConfiguratorWebBridge.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
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
#include "Serialization/JsonWriter.h"
#include "Styling/SlateBrush.h"
#include "TimerManager.h"

namespace
{
	const TCHAR* WebConfiguratorSection = TEXT("ConfigurationSystem.WebConfigurator");
	const TCHAR* DefaultWebConfiguratorEndpoint = TEXT("127.0.0.1:8080");
	constexpr float ExpandedPanelWidth = 480.0f;
	constexpr float HeaderHeight = 76.0f;
	constexpr float StageMargin = 18.0f;
	constexpr float ControlsLayerWidth = 620.0f;
	constexpr float ControlsLayerHeight = 190.0f;
	constexpr float ControlsBottomInset = 32.0f;
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

FString UConfiguratorPanel::GetControlsWebUrl()
{
	FString Url = GetConfiguredWebUrl();
	Url.ReplaceInline(TEXT("view=embedded"), TEXT("view=controls"));
	return Url;
}

FString UConfiguratorPanel::GetHeaderWebUrl()
{
	FString Url = GetConfiguredWebUrl();
	Url.ReplaceInline(TEXT("view=embedded"), TEXT("view=header"));
	return Url;
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
	StartHealthProbe();
}

void UConfiguratorPanel::NativeDestruct()
{
	CancelHealthProbe();
	WebBridge = nullptr;
	Super::NativeDestruct();
}

void UConfiguratorPanel::BuildWidgetTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;
	PageMasks.Reset();

	auto AddWhiteMask = [this, Root](
		const FName Name,
		const FAnchors& Anchors,
		const FMargin& Offsets,
		const int32 ZOrder)
	{
		UBorder* Mask = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), Name);
		Mask->SetBrushColor(FLinearColor::White);
		Mask->SetVisibility(ESlateVisibility::HitTestInvisible);
		PageMasks.Add(Mask);
		UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Mask);
		Slot->SetAnchors(Anchors);
		Slot->SetOffsets(Offsets);
		Slot->SetZOrder(ZOrder);
		return Mask;
	};

	// 以实心白色页面遮住舞台外部，而不是给渲染画面套一圈描边。
	AddWhiteMask(
		TEXT("PageTopMask"),
		FAnchors(0.0f, 0.0f, 1.0f, 0.0f),
		FMargin(0.0f, 0.0f, ExpandedPanelWidth, HeaderHeight),
		1);
	AddWhiteMask(
		TEXT("PageLeftMask"),
		FAnchors(0.0f, 0.0f, 0.0f, 1.0f),
		FMargin(0.0f, HeaderHeight, StageMargin, StageMargin),
		1);
	AddWhiteMask(
		TEXT("PageBottomMask"),
		FAnchors(0.0f, 1.0f, 1.0f, 1.0f),
		FMargin(0.0f, -StageMargin, ExpandedPanelWidth, StageMargin),
		1);
	AddWhiteMask(
		TEXT("PageRightPanelBackground"),
		FAnchors(1.0f, 0.0f, 1.0f, 1.0f),
		FMargin(-ExpandedPanelWidth, 0.0f, ExpandedPanelWidth, 0.0f),
		1);

	// 仅在窗口转角覆盖圆角像素；直边由上面的实心遮罩承担。
	UBorder* VehicleStageCornerMask = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("VehicleStageCornerCutout"));
	FSlateBrush CornerMaskBrush;
	CornerMaskBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
	CornerMaskBrush.TintColor = FLinearColor::Transparent;
	CornerMaskBrush.OutlineSettings.CornerRadii = FVector4(24.0f);
	CornerMaskBrush.OutlineSettings.Color = FSlateColor(FLinearColor::White);
	CornerMaskBrush.OutlineSettings.Width = StageMargin;
	CornerMaskBrush.OutlineSettings.RoundingType =
		ESlateBrushRoundingType::FixedRadius;
	VehicleStageCornerMask->SetBrush(CornerMaskBrush);
	VehicleStageCornerMask->SetVisibility(ESlateVisibility::HitTestInvisible);
	PageMasks.Add(VehicleStageCornerMask);
	UCanvasPanelSlot* VehicleStageSlot = Root->AddChildToCanvas(VehicleStageCornerMask);
	VehicleStageSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
	VehicleStageSlot->SetOffsets(FMargin(
		StageMargin,
		HeaderHeight,
		ExpandedPanelWidth + StageMargin,
		StageMargin));
	VehicleStageSlot->SetZOrder(2);

	WebBrowser = WidgetTree->ConstructWidget<UConfiguratorBrowserWidget>(
		UConfiguratorBrowserWidget::StaticClass(), TEXT("EmbeddedWebConfigurator"));
	WebBridge = NewObject<UConfiguratorWebBridge>(this);
	WebBridge->Initialize(this);
	WebBrowser->SetBridge(WebBridge);
	UCanvasPanelSlot* BrowserSlot = Root->AddChildToCanvas(WebBrowser);
	BrowserSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
	BrowserSlot->SetAlignment(FVector2D(1.0f, 0.0f));
	BrowserSlot->SetOffsets(FMargin(0.0f, 0.0f, ExpandedPanelWidth, 0.0f));
	BrowserSlot->SetZOrder(4);

	HeaderBrowser = WidgetTree->ConstructWidget<UConfiguratorBrowserWidget>(
		UConfiguratorBrowserWidget::StaticClass(), TEXT("WebConfiguratorHeader"));
	HeaderBrowser->SetBridge(WebBridge);
	UCanvasPanelSlot* HeaderSlot = Root->AddChildToCanvas(HeaderBrowser);
	HeaderSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
	HeaderSlot->SetOffsets(FMargin(0.0f, 0.0f, ExpandedPanelWidth, HeaderHeight));
	HeaderSlot->SetZOrder(4);

	ControlsBrowser = WidgetTree->ConstructWidget<UConfiguratorBrowserWidget>(
		UConfiguratorBrowserWidget::StaticClass(), TEXT("WebExperienceControls"));
	ControlsBrowser->SetBridge(WebBridge);
	ControlsCanvasSlot = Root->AddChildToCanvas(ControlsBrowser);
	ControlsCanvasSlot->SetAnchors(FAnchors(0.5f, 1.0f));
	ControlsCanvasSlot->SetAlignment(FVector2D(0.5f, 1.0f));
	ControlsCanvasSlot->SetPosition(FVector2D(
		-ExpandedPanelWidth * 0.5f,
		-ControlsBottomInset));
	ControlsCanvasSlot->SetSize(FVector2D(ControlsLayerWidth, ControlsLayerHeight));
	ControlsCanvasSlot->SetZOrder(10);
}

void UConfiguratorPanel::StartHealthProbe()
{
	CancelHealthProbe();
	HealthProbeAttemptCount = 0;
	PendingRetryDelaySeconds = 0.0f;
	HealthProbeState = EHealthProbeState::Waiting;
	IssueHealthProbe();
}

void UConfiguratorPanel::IssueHealthProbe()
{
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
	if (bHealthy)
	{
		HealthProbeState = EHealthProbeState::Ready;
		PendingRetryDelaySeconds = 0.0f;
		if (WebBrowser != nullptr)
		{
			WebBrowser->LoadURL(GetConfiguredWebUrl());
		}
		if (ControlsBrowser != nullptr)
		{
			ControlsBrowser->LoadURL(GetControlsWebUrl());
		}
		if (HeaderBrowser != nullptr)
		{
			HeaderBrowser->LoadURL(GetHeaderWebUrl());
		}
		return;
	}
	HandleHealthProbeFailure();
}

void UConfiguratorPanel::HandleHealthProbeFailure()
{
	PendingRetryDelaySeconds =
		GetHealthRetryDelaySeconds(HealthProbeAttemptCount);
	if (PendingRetryDelaySeconds <= 0.0f)
	{
		HealthProbeState = EHealthProbeState::Failed;
		return;
	}

	HealthProbeState = EHealthProbeState::Waiting;
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

bool UConfiguratorPanel::SetExperienceCamera(const int32 CameraIndex)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	return Controller != nullptr && Controller->SetCamera(CameraIndex);
}

bool UConfiguratorPanel::SetExperienceAnimationEnabled(const bool bEnabled)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	return Controller != nullptr && Controller->SetAnimationEnabled(bEnabled);
}

bool UConfiguratorPanel::SetExperienceLightPreset(const FString& Preset)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	return Controller != nullptr && Controller->SetLightPreset(Preset);
}

bool UConfiguratorPanel::SetExperienceRenderMode(const FString& Mode)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	FString Error;
	return Controller != nullptr && Controller->SetRenderMode(Mode, Error);
}

bool UConfiguratorPanel::SetExperienceQualityLevel(const FString& Quality)
{
	UGameUserSettings* Settings =
		GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (!UConfiguratorWebBridge::IsSupportedQualityLevel(Quality)
		|| Settings == nullptr)
	{
		return false;
	}
	const int32 Level = Quality == TEXT("low") ? 0
		: Quality == TEXT("medium") ? 1
		: Quality == TEXT("high") ? 2
		: 3;
	Settings->SetOverallScalabilityLevel(Level);
	Settings->ApplySettings(false);
	return true;
}

bool UConfiguratorPanel::ResetExperiencePresentation()
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	if (Controller == nullptr)
	{
		return false;
	}
	Controller->ResetPresentation();
	return true;
}

bool UConfiguratorPanel::SetExperienceFullscreen(const bool bEnabled)
{
	bWebFullscreen = bEnabled;
	const ESlateVisibility WebVisibility =
		bWebFullscreen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible;
	if (WebBrowser != nullptr)
	{
		WebBrowser->SetVisibility(WebVisibility);
	}
	if (HeaderBrowser != nullptr)
	{
		HeaderBrowser->SetVisibility(WebVisibility);
	}
	for (UBorder* Mask : PageMasks)
	{
		if (Mask != nullptr)
		{
			Mask->SetVisibility(
				bWebFullscreen
					? ESlateVisibility::Collapsed
					: ESlateVisibility::HitTestInvisible);
		}
	}
	if (ControlsCanvasSlot != nullptr)
	{
		ControlsCanvasSlot->SetPosition(FVector2D(
			bWebFullscreen ? 0.0f : -ExpandedPanelWidth * 0.5f,
			-ControlsBottomInset));
	}
	return true;
}

bool UConfiguratorPanel::SetConfiguratorCategory(const FString& CategoryId)
{
	if (!UConfiguratorWebBridge::IsSupportedConfiguratorCategory(CategoryId))
	{
		return false;
	}
	const FString Script = FString::Printf(
		TEXT("window.dispatchEvent(new CustomEvent('ue-configurator-category',"
			"{detail:'%s'}));"),
		*CategoryId);
	if (WebBrowser != nullptr)
	{
		WebBrowser->ExecuteJavascript(Script);
	}
	if (HeaderBrowser != nullptr)
	{
		HeaderBrowser->ExecuteJavascript(Script);
	}
	return WebBrowser != nullptr || HeaderBrowser != nullptr;
}

FString UConfiguratorPanel::GetExperienceStateJson()
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	UGameUserSettings* Settings =
		GEngine != nullptr ? GEngine->GetGameUserSettings() : nullptr;
	if (Controller == nullptr || Settings == nullptr)
	{
		return FString();
	}

	const int32 ScalabilityLevel = Settings->GetOverallScalabilityLevel();
	const FString Quality = ScalabilityLevel <= 0 ? TEXT("low")
		: ScalabilityLevel == 1 ? TEXT("medium")
		: ScalabilityLevel == 2 ? TEXT("high")
		: TEXT("epic");
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetNumberField(
		TEXT("cameraIndex"),
		FMath::Clamp(Controller->GetCurrentCameraIndex(), 0, 5));
	State->SetBoolField(
		TEXT("animationEnabled"),
		Controller->IsAnimationEnabled());
	State->SetStringField(TEXT("lightPreset"), Controller->GetLightPreset());
	State->SetStringField(TEXT("renderMode"), Controller->GetRenderMode());
	State->SetStringField(TEXT("quality"), Quality);
	State->SetBoolField(TEXT("fullscreen"), bWebFullscreen);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(State, Writer) ? Json : FString();
}
