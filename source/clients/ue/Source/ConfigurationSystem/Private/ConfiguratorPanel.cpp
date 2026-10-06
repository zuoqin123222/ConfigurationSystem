#include "ConfiguratorPanel.h"

#include "ConfigShowroomPlayerController.h"
#include "CarConfiguratorSubsystem.h"
#include "ConfiguratorBrowserWidget.h"
#include "ConfiguratorWebBridge.h"
#include "StageCornerMask.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/SlateBrush.h"

namespace
{
	constexpr float ExpandedPanelWidth = 480.0f;
	constexpr float HeaderHeight = 76.0f;
	constexpr float HeaderShadowHeight = 18.0f;
	constexpr float StageMargin = 18.0f;
	constexpr float StageCornerRadius = 24.0f;
	constexpr float ControlsLayerWidth = 620.0f;
	constexpr float ControlsLayerHeight = 190.0f;
	constexpr float ControlsBottomInset = 32.0f;
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
	const FString EditorBundle = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("WebUI/index.html"));
	const FString PackagedBundle = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("WebUI/index.html")));
	FString BundlePath = IFileManager::Get().FileExists(*EditorBundle)
		? EditorBundle
		: PackagedBundle;
	BundlePath.ReplaceInline(TEXT("\\"), TEXT("/"));
	BundlePath.ReplaceInline(TEXT(" "), TEXT("%20"));
	if (!BundlePath.StartsWith(TEXT("/")))
	{
		BundlePath = TEXT("/") + BundlePath;
	}
	return FString::Printf(
		TEXT("file://%s?source=ue&view=embedded&assetRevision=%u"),
		*BundlePath,
		FPlatformProcess::GetCurrentProcessId());
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

bool UConfiguratorPanel::ParseWebConfigurationJson(
	const FString& ConfigurationJson,
	TMap<FString, FString>& OutSelections,
	TMap<FString, FAutomotiveCustomization>& OutCustomizations,
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

		FAutomotiveCustomization Customization;
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
			Customization.Kind = EAutomotiveCustomizationKind::MaterialVariant;
		}
		else if (HasOnlyFields(*ValueObject, PaintFields)
			&& (*ValueObject)->Values.Num() == PaintFields.Num())
		{
			Customization.Kind = EAutomotiveCustomizationKind::Paint;
			FAutomotivePaintCustomization& Paint = Customization.Paint;
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
}

void UConfiguratorPanel::NativeDestruct()
{
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

	// 四条实心白边定义舞台窗口，避免透明 Web 与场景之间出现黑缝。
	AddWhiteMask(
		TEXT("PageStageTopMask"),
		FAnchors(0.0f, 0.0f, 1.0f, 0.0f),
		FMargin(0.0f, HeaderHeight, ExpandedPanelWidth, StageMargin),
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
	AddWhiteMask(
		TEXT("PageStageRightMask"),
		FAnchors(1.0f, 0.0f, 1.0f, 1.0f),
		FMargin(
			-(ExpandedPanelWidth + StageMargin),
			HeaderHeight,
			StageMargin,
			StageMargin),
		2);

	// 四角分别使用同一原生反向圆角遮罩，不再绘制 RoundedBox 白色描边。
	auto AddCornerMask = [this, Root](
		const FName Name,
		const EStageCorner Corner,
		const FAnchors& Anchors,
		const FMargin& Offsets)
	{
		UStageCornerMask* Mask = WidgetTree->ConstructWidget<UStageCornerMask>(
			UStageCornerMask::StaticClass(), Name);
		Mask->SetCorner(Corner);
		Mask->SetRadius(StageCornerRadius);
		Mask->SetVisibility(ESlateVisibility::HitTestInvisible);
		PageMasks.Add(Mask);
		UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Mask);
		Slot->SetAnchors(Anchors);
		Slot->SetOffsets(Offsets);
		Slot->SetZOrder(3);
	};
	AddCornerMask(
		TEXT("StageTopLeftInverseCorner"),
		EStageCorner::TopLeft,
		FAnchors(0.0f, 0.0f),
		FMargin(
			StageMargin,
			HeaderHeight + StageMargin,
			StageCornerRadius,
			StageCornerRadius));
	AddCornerMask(
		TEXT("StageTopRightInverseCorner"),
		EStageCorner::TopRight,
		FAnchors(1.0f, 0.0f),
		FMargin(
			-(ExpandedPanelWidth + StageMargin + StageCornerRadius),
			HeaderHeight + StageMargin,
			StageCornerRadius,
			StageCornerRadius));
	AddCornerMask(
		TEXT("StageBottomLeftInverseCorner"),
		EStageCorner::BottomLeft,
		FAnchors(0.0f, 1.0f),
		FMargin(
			StageMargin,
			-(StageMargin + StageCornerRadius),
			StageCornerRadius,
			StageCornerRadius));
	AddCornerMask(
		TEXT("StageBottomRightInverseCorner"),
		EStageCorner::BottomRight,
		FAnchors(1.0f, 1.0f),
		FMargin(
			-(ExpandedPanelWidth + StageMargin + StageCornerRadius),
			-(StageMargin + StageCornerRadius),
			StageCornerRadius,
			StageCornerRadius));

	WebBrowser = WidgetTree->ConstructWidget<UConfiguratorBrowserWidget>(
		UConfiguratorBrowserWidget::StaticClass(), TEXT("EmbeddedWebConfigurator"));
	WebBridge = NewObject<UConfiguratorWebBridge>(this);
	WebBridge->Initialize(this);
	WebBrowser->SetBridge(WebBridge);
	UCanvasPanelSlot* BrowserSlot = Root->AddChildToCanvas(WebBrowser);
	BrowserSlot->SetAnchors(FAnchors(1.0f, 0.0f, 1.0f, 1.0f));
	BrowserSlot->SetAlignment(FVector2D(1.0f, 0.0f));
	BrowserSlot->SetOffsets(FMargin(
		0.0f,
		HeaderHeight,
		ExpandedPanelWidth,
		0.0f));
	BrowserSlot->SetZOrder(4);

	HeaderBrowser = WidgetTree->ConstructWidget<UConfiguratorBrowserWidget>(
		UConfiguratorBrowserWidget::StaticClass(), TEXT("WebConfiguratorHeader"));
	HeaderBrowser->SetBridge(WebBridge);
	UCanvasPanelSlot* HeaderSlot = Root->AddChildToCanvas(HeaderBrowser);
	HeaderSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 0.0f));
	HeaderSlot->SetOffsets(FMargin(
		0.0f,
		0.0f,
		0.0f,
		HeaderHeight + HeaderShadowHeight));
	HeaderSlot->SetZOrder(6);

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

void UConfiguratorPanel::ApplyWebConfigurationJson(
	const FString& ConfigurationJson)
{
	TMap<FString, FString> Selections;
	TMap<FString, FAutomotiveCustomization> Customizations;
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
	UAutomotiveConfigurationState* State = IsValid(Configurator)
		? Configurator->GetAutomotiveConfigurationState()
		: nullptr;
	if (!IsValid(State) || !State->ApplyTransaction(Selections, Customizations))
	{
		const FString StateError = IsValid(State)
			? State->GetLastErrorCode()
			: TEXT("AUTOMOTIVE_STATE_UNAVAILABLE");
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

bool UConfiguratorPanel::IsValidConfiguratorHeaderStateJson(
	const FString& StateJson,
	FString& OutError)
{
	OutError.Reset();
	if (StateJson.IsEmpty() || StateJson.Len() > 2048)
	{
		OutError = TEXT("Header 状态为空或超过 2 KiB 限制。");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(StateJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Header 状态不是有效 JSON 对象。");
		return false;
	}
	const TSet<FString> AllowedFields = {
		TEXT("categoryId"),
		TEXT("referenceTotalMinor"),
		TEXT("syncState"),
		TEXT("syncMessage"),
		TEXT("dirty"),
		TEXT("online")
	};
	FString CategoryId;
	FString SyncState;
	FString SyncMessage;
	double ReferenceTotalMinor = -1.0;
	bool bDirty = false;
	bool bOnline = false;
	if (!HasOnlyFields(Root, AllowedFields)
		|| Root->Values.Num() != AllowedFields.Num()
		|| !Root->TryGetStringField(TEXT("categoryId"), CategoryId)
		|| !UConfiguratorWebBridge::IsSupportedConfiguratorCategory(CategoryId)
		|| !Root->TryGetNumberField(
			TEXT("referenceTotalMinor"), ReferenceTotalMinor)
		|| ReferenceTotalMinor < 0.0
		|| ReferenceTotalMinor > 1000000000000.0
		|| !FMath::IsNearlyEqual(
			ReferenceTotalMinor,
			FMath::RoundToDouble(ReferenceTotalMinor))
		|| !Root->TryGetStringField(TEXT("syncState"), SyncState)
		|| !(SyncState == TEXT("idle")
			|| SyncState == TEXT("saving")
			|| SyncState == TEXT("saved")
			|| SyncState == TEXT("error"))
		|| !Root->TryGetStringField(TEXT("syncMessage"), SyncMessage)
		|| SyncMessage.Len() > 256
		|| !Root->TryGetBoolField(TEXT("dirty"), bDirty)
		|| !Root->TryGetBoolField(TEXT("online"), bOnline))
	{
		OutError = TEXT("Header 状态包含非白名单字段或非法值。");
		return false;
	}
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
	for (UWidget* Mask : PageMasks)
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

float UConfiguratorPanel::CalculateStageProjectionOffsetX(
	const float ViewportWidth,
	const float LeftInset,
	const float RightInset)
{
	if (!FMath::IsFinite(ViewportWidth)
		|| ViewportWidth <= KINDA_SMALL_NUMBER
		|| !FMath::IsFinite(LeftInset)
		|| !FMath::IsFinite(RightInset))
	{
		return 0.0f;
	}

	// OffCenterProjectionOffset 使用相对于完整视口宽度的归一化偏移。
	// 右侧遮挡比左侧宽时，可见舞台中心向左移动，因此返回正值。
	// 这里限幅只用于防御异常布局；正常 1600px 窗口 + 480px 面板为 0.3。
	const float SafeLeftInset = FMath::Max(LeftInset, 0.0f);
	const float SafeRightInset = FMath::Max(RightInset, 0.0f);
	return FMath::Clamp(
		(SafeRightInset - SafeLeftInset) / ViewportWidth,
		-0.95f,
		0.95f);
}

bool UConfiguratorPanel::PlayExperienceAnimation(const FString& AnimationId)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	return Controller != nullptr
		&& UConfiguratorWebBridge::IsSupportedAnimationId(AnimationId)
		&& Controller->PlayAnimation(FName(*AnimationId));
}

bool UConfiguratorPanel::CloseExperienceAnimation(const FString& AnimationId)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	return Controller != nullptr
		&& UConfiguratorWebBridge::IsSupportedAnimationId(AnimationId)
		&& Controller->CloseAnimation(FName(*AnimationId));
}

bool UConfiguratorPanel::FocusExperienceAnimation(const FString& NextAnimationId)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	return Controller != nullptr
		&& (NextAnimationId.IsEmpty()
			|| UConfiguratorWebBridge::IsSupportedAnimationId(NextAnimationId))
		&& Controller->FocusAnimation(
			NextAnimationId.IsEmpty() ? NAME_None : FName(*NextAnimationId));
}

bool UConfiguratorPanel::SetExperienceCameraId(const FString& CameraId)
{
	AConfigShowroomPlayerController* Controller =
		Cast<AConfigShowroomPlayerController>(GetOwningPlayer());
	return Controller != nullptr && Controller->SetCameraId(CameraId);
}

float UConfiguratorPanel::GetStageProjectionOffsetX() const
{
	if (bWebFullscreen)
	{
		return 0.0f;
	}

	// 使用根控件的 Slate 逻辑宽度，和 480px 面板处于同一坐标系；
	// 二者的 DPI 缩放会在相除时抵消。右侧面板使舞台中心左移，
	// UE 的正 OffCenterProjectionOffset 会把画面主体向左投影。
	const float ViewportWidth = GetCachedGeometry().GetLocalSize().X;
	return CalculateStageProjectionOffsetX(
		ViewportWidth,
		0.0f,
		ExpandedPanelWidth);
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

bool UConfiguratorPanel::SetConfiguratorHeaderStateJson(
	const FString& StateJson)
{
	FString Error;
	if (!IsValidConfiguratorHeaderStateJson(StateJson, Error)
		|| HeaderBrowser == nullptr)
	{
		if (!Error.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("拒绝 Web Header 状态：%s"), *Error);
		}
		return false;
	}
	LatestConfiguratorHeaderStateJson = StateJson;
	const FString Script = FString::Printf(
		TEXT("window.dispatchEvent(new CustomEvent("
			"'ue-configurator-header-state',{detail:%s}));"),
		*StateJson);
	HeaderBrowser->ExecuteJavascript(Script);
	return true;
}

bool UConfiguratorPanel::TriggerConfiguratorHeaderAction(
	const FString& Action)
{
	if (!UConfiguratorWebBridge::IsSupportedConfiguratorHeaderAction(Action)
		|| WebBrowser == nullptr)
	{
		return false;
	}
	const FString Script = FString::Printf(
		TEXT("window.dispatchEvent(new CustomEvent("
			"'ue-configurator-header-action',{detail:'%s'}));"),
		*Action);
	WebBrowser->ExecuteJavascript(Script);
	return true;
}

FString UConfiguratorPanel::GetConfiguratorHeaderStateJson() const
{
	return LatestConfiguratorHeaderStateJson;
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
	State->SetStringField(TEXT("cameraId"), Controller->GetCurrentCameraId());
	State->SetBoolField(
		TEXT("animationEnabled"),
		Controller->IsAnimationEnabled());
	const FName ActiveAnimationId = Controller->GetActiveAnimationId();
	if (ActiveAnimationId.IsNone())
	{
		State->SetField(TEXT("animationId"), MakeShared<FJsonValueNull>());
	}
	else
	{
		State->SetStringField(TEXT("animationId"), ActiveAnimationId.ToString());
	}
	State->SetStringField(TEXT("lightPreset"), Controller->GetLightPreset());
	State->SetStringField(TEXT("renderMode"), Controller->GetRenderMode());
	State->SetStringField(TEXT("quality"), Quality);
	State->SetBoolField(TEXT("fullscreen"), bWebFullscreen);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(State, Writer) ? Json : FString();
}
