#include "AutomotiveMaterialGuiProbe.h"

#include "AutomotiveConfigurationState.h"
#include "AutomotiveMaterialBinder.h"
#include "AutomotiveMaterialLibrary.h"
#include "CarConfiguratorSubsystem.h"
#include "Components/MeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAutomotiveMaterialGuiProbe, Log, All);

void UAutomotiveMaterialGuiProbe::Start()
{
	OutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("AutomotiveMaterialGuiProbe"),
		TEXT("report.json"));
	ScreenshotPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("AutomotiveMaterialGuiProbe"),
		TEXT("mapped-slots.png"));
	FParse::Value(
		FCommandLine::Get(),
		TEXT("AutomotiveMaterialGuiProbeOutput="),
		OutputPath);
	FParse::Value(
		FCommandLine::Get(),
		TEXT("AutomotiveMaterialGuiProbeScreenshot="),
		ScreenshotPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);
	ScreenshotPath = FPaths::ConvertRelativePathToFull(ScreenshotPath);
	IFileManager::Get().Delete(*ScreenshotPath, false, true);
	AddToRoot();
	EngineInitCompleteHandle =
		FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(
			this,
			&UAutomotiveMaterialGuiProbe::OnEngineLoopInitComplete);
}

void UAutomotiveMaterialGuiProbe::Shutdown()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (IsRooted())
	{
		RemoveFromRoot();
	}
}

void UAutomotiveMaterialGuiProbe::OnEngineLoopInitComplete()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAutomotiveMaterialGuiProbe::Tick));
}

bool UAutomotiveMaterialGuiProbe::Tick(const float DeltaTime)
{
	ElapsedSeconds += DeltaTime;
	if (!bTransactionApplied && ElapsedSeconds >= 2.0)
	{
		bTransactionApplied = TryApplyTransaction();
	}
	if (bTransactionApplied
		&& !bScreenshotRequested
		&& ElapsedSeconds >= 20.0
		&& (GShaderCompilingManager == nullptr
			|| !GShaderCompilingManager->IsCompiling()))
	{
		RequestScreenshot();
	}
	if (bScreenshotRequested
		&& IFileManager::Get().FileExists(*ScreenshotPath))
	{
		WriteReportAndExit(true, FString());
		return false;
	}
	if (ElapsedSeconds >= 60.0)
	{
		WriteReportAndExit(
			false,
			bTransactionApplied
				? TEXT("真实 viewport 截图超时。")
				: TEXT("展厅车辆或材质 Binder 在超时前未就绪。"));
		return false;
	}
	return true;
}

bool UAutomotiveMaterialGuiProbe::TryApplyTransaction()
{
	if (GEngine == nullptr)
	{
		return false;
	}
	UWorld* GameWorld = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() != nullptr
			&& (Context.WorldType == EWorldType::Game
				|| Context.WorldType == EWorldType::PIE))
		{
			GameWorld = Context.World();
			break;
		}
	}
	UGameInstance* GameInstance =
		GameWorld != nullptr ? GameWorld->GetGameInstance() : nullptr;
	UCarConfiguratorSubsystem* Configurator =
		IsValid(GameInstance)
			? GameInstance->GetSubsystem<UCarConfiguratorSubsystem>()
			: nullptr;
	UAutomotiveConfigurationState* State =
		IsValid(Configurator)
			? Configurator->GetAutomotiveConfigurationState()
			: nullptr;
	UAutomotiveMaterialBinder* Binder =
		IsValid(Configurator)
			? Configurator->GetAutomotiveMaterialBinder()
			: nullptr;
	UAutomotiveMaterialLibrary* Library =
		IsValid(Configurator)
			? Configurator->GetAutomotiveMaterialLibrary()
			: nullptr;
	if (!IsValid(State) || !IsValid(Binder) || !IsValid(Library)
		|| !IsValid(Binder->GetPaintComponent())
		|| !IsValid(Binder->GetInteriorComponent()))
	{
		return false;
	}

	TMap<FString, FString> Selections = State->GetSelections();
	Selections.Add(
		UAutomotiveMaterialBinder::PaintSurfaceId,
		TEXT("body-cover-custom"));
	Selections.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		TEXT("door-middle-leather"));
	TMap<FString, FAutomotiveCustomization> Customizations =
		State->GetCustomizations();
	FAutomotiveCustomization PaintCustomization;
	PaintCustomization.Kind = EAutomotiveCustomizationKind::Paint;
	PaintCustomization.Paint.ColorHex = TEXT("#245E9A");
	PaintCustomization.Paint.Metallic = 0.62;
	PaintCustomization.Paint.Roughness = 0.2;
	PaintCustomization.Paint.ClearCoat = 0.9;
	PaintCustomization.Paint.OrangePeel = 0.1;
	PaintCustomization.Paint.FlakeIntensity = 0.4;
	Customizations.Add(
		UAutomotiveMaterialBinder::PaintSurfaceId,
		PaintCustomization);
	FAutomotiveCustomization VariantCustomization;
	VariantCustomization.Kind = EAutomotiveCustomizationKind::MaterialVariant;
	VariantCustomization.MaterialVariantId = TEXT("leather-p10-1217");
	Customizations.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		VariantCustomization);

	const FAutomotiveMaterialTransactionResult Result =
		Binder->ApplyTransaction(Selections, Customizations);
	ReceiptJson = Result.ToJson();
	if (!Result.bSuccess
		|| !Result.AppliedSurfaceIds.Contains(
			UAutomotiveMaterialBinder::PaintSurfaceId)
		|| !Result.AppliedSurfaceIds.Contains(
			UAutomotiveMaterialBinder::InteriorProxySurfaceId)
		|| !Result.AppliedSlotIds.Contains(TEXT("CS_Validation_Paint"))
		|| !Result.AppliedSlotIds.Contains(TEXT("CS_Validation_Interior")))
	{
		return false;
	}
	UMaterialInstanceDynamic* PaintMid = Binder->GetPaintMaterialInstance();
	UMaterialInterface* Interior =
		Binder->GetAppliedMaterialForSurface(
			UAutomotiveMaterialBinder::InteriorProxySurfaceId);
	if (!IsValid(PaintMid)
		|| Interior != Library->LoadVariantMaterial(TEXT("leather-p10-1217")))
	{
		return false;
	}
	const FLinearColor Expected =
		FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#245E9A")));
	if (!PaintMid->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(
		Expected,
		0.001f))
	{
		return false;
	}
	PaintComponentName = Binder->GetPaintComponent()->GetName();
	InteriorComponentName = Binder->GetInteriorComponent()->GetName();
	PaintMaterialName = PaintMid->GetPathName();
	InteriorMaterialName = Interior->GetPathName();
	return true;
}

void UAutomotiveMaterialGuiProbe::RequestScreenshot()
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath), true);
	FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false);
	bScreenshotRequested = true;
}

void UAutomotiveMaterialGuiProbe::WriteReportAndExit(
	const bool bSuccess,
	const FString& FailureReason)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schemaVersion"), TEXT("1.0.0"));
	Root->SetStringField(TEXT("probe"), TEXT("AutomotiveMaterialGuiProbe"));
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetBoolField(TEXT("success"), bSuccess);
	Root->SetStringField(TEXT("failureReason"), FailureReason);
	Root->SetStringField(
		TEXT("screenshot"),
		FPaths::GetCleanFilename(ScreenshotPath));
	Root->SetStringField(TEXT("paintComponent"), PaintComponentName);
	Root->SetStringField(TEXT("interiorComponent"), InteriorComponentName);
	Root->SetStringField(TEXT("paintMaterial"), PaintMaterialName);
	Root->SetStringField(TEXT("interiorMaterial"), InteriorMaterialName);
	Root->SetStringField(TEXT("transactionReceiptJson"), ReceiptJson);
	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bWritten =
		FJsonSerializer::Serialize(Root, Writer)
		&& IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true)
		&& FFileHelper::SaveStringToFile(
			JsonText,
			*OutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (bSuccess && bWritten)
	{
		UE_LOG(
			LogAutomotiveMaterialGuiProbe,
			Display,
			TEXT("阶段4 GUI 材质槽探针通过，报告=%s，截图=%s"),
			*OutputPath,
			*ScreenshotPath);
	}
	else
	{
		UE_LOG(
			LogAutomotiveMaterialGuiProbe,
			Error,
			TEXT("阶段4 GUI 材质槽探针失败：%s，报告=%s"),
			*FailureReason,
			*OutputPath);
	}
	Shutdown();
	FPlatformMisc::RequestExitWithStatus(
		false,
		bSuccess && bWritten ? 0 : 11);
}
