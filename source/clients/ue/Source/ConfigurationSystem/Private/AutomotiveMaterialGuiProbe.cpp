#include "AutomotiveMaterialGuiProbe.h"

#include "AutomotiveConfigurationState.h"
#include "AutomotiveMaterialBinder.h"
#include "AutomotiveMaterialLibrary.h"
#include "CarConfiguratorSubsystem.h"
#include "Components/MeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PathTracingExperienceSubsystem.h"
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
	if (!bTraversalInitialized && ElapsedSeconds >= 2.0)
	{
		bTraversalInitialized = TryInitializeTraversal();
	}
	if (bTraversalInitialized && !bTraversalComplete)
	{
		FString FailureReason;
		if (!ProcessNextSurface(FailureReason))
		{
			WriteReportAndExit(false, FailureReason);
			return false;
		}
		if (SurfaceIndex == SurfaceIds.Num())
		{
			bTraversalComplete = true;
			TraversalCompletedSeconds = ElapsedSeconds;
		}
	}
	if (bTraversalComplete
		&& !bPathTracingEntered
		&& ElapsedSeconds - TraversalCompletedSeconds >= 2.0)
	{
		FString FailureReason;
		if (!ValidateAllSurfaceMaterialSlots(FailureReason))
		{
			WriteReportAndExit(false, TEXT("Lit 全槽检查失败：") + FailureReason);
			return false;
		}
		FString PathTracingError;
		if (IsValid(PathTracing)
			&& PathTracing->SetPathTracingEnabled(true, PathTracingError))
		{
			bPathTracingEntered = true;
			RenderModeChangedSeconds = ElapsedSeconds;
		}
		else if (!IsValid(PathTracing)
			|| PathTracing->GetAvailability() == TEXT("unavailable"))
		{
			WriteReportAndExit(
				false,
				TEXT("无法进入 Path Tracing：") + PathTracingError);
			return false;
		}
	}
	if (bPathTracingEntered
		&& !bLitRestored
		&& ElapsedSeconds - RenderModeChangedSeconds >= 3.0)
	{
		FString FailureReason;
		if (!ValidateAllSurfaceMaterialSlots(FailureReason))
		{
			WriteReportAndExit(false, TEXT("Path Tracing 全槽检查失败：") + FailureReason);
			return false;
		}
		FString RestoreError;
		if (!IsValid(PathTracing)
			|| !PathTracing->SetPathTracingEnabled(false, RestoreError))
		{
			WriteReportAndExit(false, TEXT("无法恢复 Lit：") + RestoreError);
			return false;
		}
		bLitRestored = true;
		RenderModeChangedSeconds = ElapsedSeconds;
	}
	if (bLitRestored
		&& !bScreenshotRequested
		&& ElapsedSeconds - RenderModeChangedSeconds >= 2.0
		&& (GShaderCompilingManager == nullptr
			|| !GShaderCompilingManager->IsCompiling()))
	{
		FString FailureReason;
		if (!ValidateAllSurfaceMaterialSlots(FailureReason))
		{
			WriteReportAndExit(false, TEXT("恢复 Lit 后全槽检查失败：") + FailureReason);
			return false;
		}
		RequestScreenshot();
	}
	if (bScreenshotRequested
		&& IFileManager::Get().FileExists(*ScreenshotPath))
	{
		WriteReportAndExit(true, FString());
		return false;
	}
	if (ElapsedSeconds >= 180.0)
	{
		WriteReportAndExit(
			false,
			bTraversalComplete
				? TEXT("真实 viewport 截图超时。")
				: TEXT("真实 Game viewport、展厅车辆或材质 Binder 在超时前未就绪。"));
		return false;
	}
	return true;
}

bool UAutomotiveMaterialGuiProbe::TryInitializeTraversal()
{
	bReadinessViewport = GEngine != nullptr
		&& GEngine->GameViewport != nullptr
		&& GEngine->GameViewport->Viewport != nullptr;
	if (!bReadinessViewport)
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
	bReadinessWorld = IsValid(GameWorld);
	ReadinessWorldPath =
		bReadinessWorld ? GameWorld->GetPathName() : FString();
	UGameInstance* GameInstance =
		GameWorld != nullptr ? GameWorld->GetGameInstance() : nullptr;
	UCarConfiguratorSubsystem* Configurator =
		IsValid(GameInstance)
			? GameInstance->GetSubsystem<UCarConfiguratorSubsystem>()
			: nullptr;
	bReadinessSubsystem = IsValid(Configurator);
	ReadinessSubsystemPath = bReadinessSubsystem
		? Configurator->GetPathName()
		: FString();
	UAutomotiveConfigurationState* CandidateState =
		IsValid(Configurator)
			? Configurator->GetAutomotiveConfigurationState()
			: nullptr;
	UAutomotiveMaterialBinder* CandidateBinder =
		IsValid(Configurator)
			? Configurator->GetAutomotiveMaterialBinder()
			: nullptr;
	UAutomotiveMaterialLibrary* CandidateLibrary =
		IsValid(Configurator)
			? Configurator->GetAutomotiveMaterialLibrary()
			: nullptr;
	UPathTracingExperienceSubsystem* CandidatePathTracing =
		IsValid(GameInstance)
			? GameInstance->GetSubsystem<UPathTracingExperienceSubsystem>()
			: nullptr;
	bReadinessState =
		IsValid(CandidateState) && CandidateState->IsInitialized();
	bReadinessLibrary = IsValid(CandidateLibrary);
	ReadinessStatePath =
		IsValid(CandidateState) ? CandidateState->GetPathName() : FString();
	ReadinessLibraryPath =
		bReadinessLibrary ? CandidateLibrary->GetPathName() : FString();
	ReadinessBinderPath =
		IsValid(CandidateBinder) ? CandidateBinder->GetPathName() : FString();
	ReadinessBinderLastError =
		IsValid(CandidateBinder) ? CandidateBinder->GetLastError() : FString();
	ReadinessBoundSlotCounts.Reset();
	bReadinessBinder =
		IsValid(CandidateBinder) && bReadinessState && bReadinessLibrary
			&& IsValid(CandidatePathTracing);
	if (!bReadinessState || !IsValid(CandidateBinder) || !bReadinessLibrary)
	{
		return false;
	}
	const TArray<FString>& CandidateSurfaceIds =
		CandidateState->GetCatalogIndex().GetCatalog().SelectionOrder;
	if (CandidateSurfaceIds.Num() != AutomotiveCatalog::RequiredSelectionCount
		|| !CandidateState->GetCatalogIndex().GetCatalog()
			.VehicleSurfaceBinding.UnsupportedSurfaceIds.IsEmpty())
	{
		bReadinessBinder = false;
		return false;
	}
	for (const FString& SurfaceId : CandidateSurfaceIds)
	{
		const int32 BoundSlotCount =
			CandidateBinder->GetBoundSlotCount(SurfaceId);
		ReadinessBoundSlotCounts.Add(SurfaceId, BoundSlotCount);
		if (BoundSlotCount != 1)
		{
			bReadinessBinder = false;
		}
	}
	if (!bReadinessBinder)
	{
		return false;
	}
	State = CandidateState;
	Binder = CandidateBinder;
	Library = CandidateLibrary;
	PathTracing = CandidatePathTracing;
	SurfaceIds = CandidateSurfaceIds;
	SurfaceResults.Reset(SurfaceIds.Num());
	UnsupportedSurfaceIds.Reset();
	SurfaceIndex = 0;
	return true;
}

bool UAutomotiveMaterialGuiProbe::ValidateAllSurfaceMaterialSlots(
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	for (const FString& SurfaceId : SurfaceIds)
	{
		UMeshComponent* Component = nullptr;
		FName SlotId;
		int32 MaterialIndex = INDEX_NONE;
		if (!IsValid(Binder)
			|| !Binder->GetSingleBoundSlot(
				SurfaceId,
				Component,
				SlotId,
				MaterialIndex)
			|| !IsValid(Component)
			|| Component->GetNumMaterials() <= 0)
		{
			OutFailureReason = FString::Printf(
				TEXT("surfaceId=%s 没有有效静态代理材质槽。"),
				*SurfaceId);
			return false;
		}
		UMaterialInterface* ExpectedMaterial = Component->GetMaterial(0);
		if (!IsValid(ExpectedMaterial))
		{
			OutFailureReason = FString::Printf(
				TEXT("surfaceId=%s 的槽 0 材质为空。"),
				*SurfaceId);
			return false;
		}
		for (int32 Index = 1; Index < Component->GetNumMaterials(); ++Index)
		{
			if (Component->GetMaterial(Index) != ExpectedMaterial)
			{
				OutFailureReason = FString::Printf(
					TEXT("surfaceId=%s 的槽 %d 与槽 0 材质不一致。"),
					*SurfaceId,
					Index);
				return false;
			}
		}
	}
	return true;
}

FString UAutomotiveMaterialGuiProbe::DescribeMaterial(
	UMaterialInterface* Material)
{
	if (!IsValid(Material))
	{
		return TEXT("<null>");
	}
	FString Description = Material->GetPathName();
	if (UMaterialInstanceDynamic* Dynamic =
		Cast<UMaterialInstanceDynamic>(Material))
	{
		const FLinearColor Color =
			Dynamic->K2_GetVectorParameterValue(TEXT("BaseColor"));
		Description += FString::Printf(
			TEXT("|BaseColor=%s"),
			*Color.ToString());
	}
	return Description;
}

bool UAutomotiveMaterialGuiProbe::ProcessNextSurface(
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!IsValid(State) || !IsValid(Binder) || !IsValid(Library)
		|| !SurfaceIds.IsValidIndex(SurfaceIndex))
	{
		OutFailureReason = TEXT("遍历状态无效。");
		return false;
	}

	const FString SurfaceId = SurfaceIds[SurfaceIndex];
	const AutomotiveCatalog::FCatalogIndex& CatalogIndex =
		State->GetCatalogIndex();
	const TArray<FString>* Options =
		CatalogIndex.FindOptionIdsForSurface(SurfaceId);
	TMap<FString, FString> Selections = State->GetSelections();
	const FString CurrentOption = Selections.FindRef(SurfaceId);
	const FString* NextOptionId = Options != nullptr
		? Options->FindByPredicate(
			[&CurrentOption](const FString& Value)
			{
				return Value != CurrentOption;
			})
		: nullptr;
	if (NextOptionId == nullptr
		&& Options != nullptr
		&& Options->Num() == 1
		&& CurrentOption == (*Options)[0])
	{
		TMap<FString, FString> BaselineSelections = Selections;
		BaselineSelections.Remove(SurfaceId);
		TMap<FString, FAutomotiveCustomization> BaselineCustomizations =
			State->GetCustomizations();
		BaselineCustomizations.Remove(SurfaceId);
		const FAutomotiveMaterialTransactionResult BaselineResult =
			Binder->ApplyTransaction(
				BaselineSelections,
				BaselineCustomizations);
		if (!BaselineResult.bSuccess)
		{
			OutFailureReason = FString::Printf(
				TEXT("surfaceId=%s 无法建立未选择基线：%s"),
				*SurfaceId,
				*BaselineResult.ToJson());
			return false;
		}
		NextOptionId = &CurrentOption;
	}
	if (NextOptionId == nullptr)
	{
		OutFailureReason = FString::Printf(
			TEXT("surfaceId=%s 没有可选择的非默认 option。"),
			*SurfaceId);
		return false;
	}
	const AutomotiveCatalog::FOption* NextOption =
		CatalogIndex.FindOption(*NextOptionId);
	if (NextOption == nullptr)
	{
		OutFailureReason = FString::Printf(
			TEXT("surfaceId=%s 的候选 option 无效。"),
			*SurfaceId);
		return false;
	}

	UMeshComponent* Component = nullptr;
	FName SlotId;
	int32 MaterialIndex = INDEX_NONE;
	const bool bUniqueSlot = Binder->GetBoundSlotCount(SurfaceId) == 1
		&& Binder->GetSingleBoundSlot(
			SurfaceId,
			Component,
			SlotId,
			MaterialIndex);
	if (!bUniqueSlot || !IsValid(Component)
		|| !Component->IsVisible() || Component->bHiddenInGame)
	{
		OutFailureReason = FString::Printf(
			TEXT("surfaceId=%s 未命中唯一可见运行时目标。"),
			*SurfaceId);
		return false;
	}

	UMaterialInterface* BeforeMaterial = Component->GetMaterial(MaterialIndex);
	const FString Before = DescribeMaterial(BeforeMaterial);
	Selections.Add(SurfaceId, *NextOptionId);
	TMap<FString, FAutomotiveCustomization> Customizations =
		State->GetCustomizations();
	Customizations.Remove(SurfaceId);
	FString VariantId;
	if (NextOption->SupportsMaterialVariants())
	{
		const TArray<FString>* Variants =
			CatalogIndex.FindVariantIdsForMaterialFamily(
				NextOption->MaterialFamilyId.GetValue());
		if (Variants == nullptr || Variants->IsEmpty())
		{
			OutFailureReason = FString::Printf(
				TEXT("surfaceId=%s 的非默认 option 没有可用 variant。"),
				*SurfaceId);
			return false;
		}
		VariantId = (*Variants)[0];
		FAutomotiveCustomization VariantCustomization;
		VariantCustomization.Kind =
			EAutomotiveCustomizationKind::MaterialVariant;
		VariantCustomization.MaterialVariantId = VariantId;
		Customizations.Add(SurfaceId, VariantCustomization);
	}
	else if (NextOption->SupportsCustomColor())
	{
		FAutomotiveCustomization PaintCustomization;
		PaintCustomization.Kind = EAutomotiveCustomizationKind::Paint;
		PaintCustomization.Paint.ColorHex = TEXT("#245E9A");
		PaintCustomization.Paint.Metallic = 0.62;
		PaintCustomization.Paint.Roughness = 0.2;
		PaintCustomization.Paint.ClearCoat = 0.9;
		PaintCustomization.Paint.OrangePeel = 0.1;
		PaintCustomization.Paint.FlakeIntensity = 0.4;
		Customizations.Add(SurfaceId, PaintCustomization);
	}

	const FAutomotiveMaterialTransactionResult Result =
		Binder->ApplyTransaction(Selections, Customizations);
	for (const FString& Unsupported : Result.UnsupportedSurfaceIds)
	{
		UnsupportedSurfaceIds.AddUnique(Unsupported);
	}
	UMaterialInterface* AfterMaterial = Component->GetMaterial(MaterialIndex);
	const FString After = DescribeMaterial(AfterMaterial);
	const bool bVisible = Component->IsVisible() && !Component->bHiddenInGame;
	FString ColorPolicy = TEXT("neutral-gray");
	if (!VariantId.IsEmpty())
	{
		ColorPolicy = TEXT("material-variant");
	}
	else if (NextOption->SupportsCustomColor())
	{
		ColorPolicy = TEXT("custom-color");
	}
	else if (NextOption->ColorCode.IsSet())
	{
		ColorPolicy = TEXT("color-code");
	}
	else if (NextOption->DisplayColorHex.IsSet())
	{
		ColorPolicy = TEXT("display-color-hex");
	}
	const bool bNeutralProxy = ColorPolicy == TEXT("neutral-gray");
	bool bNeutralProxyColorValid = true;
	if (bNeutralProxy)
	{
		UMaterialInstanceDynamic* Dynamic =
			Cast<UMaterialInstanceDynamic>(AfterMaterial);
		if (!IsValid(Dynamic))
		{
			bNeutralProxyColorValid = false;
		}
		else
		{
			const FLinearColor Color =
				Dynamic->K2_GetVectorParameterValue(TEXT("BaseColor"));
			bNeutralProxyColorValid =
				FMath::IsNearlyEqual(Color.R, Color.G, 0.0001f)
				&& FMath::IsNearlyEqual(Color.G, Color.B, 0.0001f);
		}
	}
	bool bAllSlotsMatch = IsValid(AfterMaterial);
	for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
	{
		bAllSlotsMatch = bAllSlotsMatch
			&& Component->GetMaterial(Index) == AfterMaterial;
	}
	const bool bReceiptValid =
		Result.bSuccess
		&& Result.Code == TEXT("APPLIED")
		&& Result.AppliedSurfaceIds == TArray<FString>({SurfaceId})
		&& Result.AppliedSlotIds == TArray<FName>({SlotId})
		&& Result.UnsupportedSurfaceIds.IsEmpty();
	const bool bChanged = IsValid(AfterMaterial) && Before != After;
	if (!bReceiptValid || !bVisible || !bChanged
		|| !bNeutralProxyColorValid || !bAllSlotsMatch)
	{
		OutFailureReason = FString::Printf(
			TEXT("surfaceId=%s 验证失败：receipt=%s visible=%s neutralProxyColor=%s allSlotsMatch=%s before=%s after=%s"),
			*SurfaceId,
			*Result.ToJson(),
			bVisible ? TEXT("true") : TEXT("false"),
			bNeutralProxyColorValid ? TEXT("true") : TEXT("false"),
			bAllSlotsMatch ? TEXT("true") : TEXT("false"),
			*Before,
			*After);
		return false;
	}

	FAutomotiveMaterialGuiProbeSurfaceResult& Report =
		SurfaceResults.AddDefaulted_GetRef();
	Report.SurfaceId = SurfaceId;
	Report.OptionId = *NextOptionId;
	Report.MaterialVariantId = VariantId;
	Report.Component = Component->GetPathName();
	Report.Slot = SlotId.ToString();
	Report.Before = Before;
	Report.After = After;
	Report.ReceiptCode = Result.Code;
	Report.ColorPolicy = ColorPolicy;
	Report.ComponentMaterialSlotCount = Component->GetNumMaterials();
	Report.bVisible = bVisible;
	Report.bUniqueSlotHit = bUniqueSlot;
	Report.bAllSlotsMatch = bAllSlotsMatch;
	Report.bChanged = bChanged;
	Report.bNeutralProxy = bNeutralProxy;
	++SurfaceIndex;
	return true;
}

void UAutomotiveMaterialGuiProbe::RequestScreenshot()
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath), true);
	FScreenshotRequest::RequestScreenshot(
		ScreenshotPath,
		FParse::Param(FCommandLine::Get(), TEXT("FeedbackGuiProbe")),
		false);
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
	Root->SetNumberField(TEXT("expectedSurfaceCount"),
		AutomotiveCatalog::RequiredSelectionCount);
	Root->SetNumberField(TEXT("validatedSurfaceCount"), SurfaceResults.Num());
	Root->SetBoolField(TEXT("pathTracingEntered"), bPathTracingEntered);
	Root->SetBoolField(TEXT("litRestored"), bLitRestored);
	TSharedRef<FJsonObject> Readiness = MakeShared<FJsonObject>();
	Readiness->SetBoolField(TEXT("viewport"), bReadinessViewport);
	Readiness->SetBoolField(TEXT("world"), bReadinessWorld);
	Readiness->SetBoolField(TEXT("subsystem"), bReadinessSubsystem);
	Readiness->SetBoolField(TEXT("state"), bReadinessState);
	Readiness->SetBoolField(TEXT("library"), bReadinessLibrary);
	Readiness->SetBoolField(TEXT("binder"), bReadinessBinder);
	Readiness->SetStringField(TEXT("worldPath"), ReadinessWorldPath);
	Readiness->SetStringField(TEXT("subsystemPath"), ReadinessSubsystemPath);
	Readiness->SetStringField(TEXT("statePath"), ReadinessStatePath);
	Readiness->SetStringField(TEXT("libraryPath"), ReadinessLibraryPath);
	Readiness->SetStringField(TEXT("binderPath"), ReadinessBinderPath);
	Readiness->SetStringField(
		TEXT("binderLastError"),
		ReadinessBinderLastError);
	TSharedRef<FJsonObject> BoundSlotCounts = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : ReadinessBoundSlotCounts)
	{
		BoundSlotCounts->SetNumberField(Pair.Key, Pair.Value);
	}
	Readiness->SetObjectField(
		TEXT("surfaceBoundSlotCounts"),
		BoundSlotCounts);
	Root->SetObjectField(TEXT("readiness"), Readiness);
	Root->SetStringField(
		TEXT("screenshot"),
		FPaths::GetCleanFilename(ScreenshotPath));
	TArray<TSharedPtr<FJsonValue>> UnsupportedValues;
	for (const FString& SurfaceId : UnsupportedSurfaceIds)
	{
		UnsupportedValues.Add(MakeShared<FJsonValueString>(SurfaceId));
	}
	Root->SetArrayField(
		TEXT("unsupportedSurfaceIds"),
		MoveTemp(UnsupportedValues));
	TArray<TSharedPtr<FJsonValue>> SurfaceValues;
	for (const FAutomotiveMaterialGuiProbeSurfaceResult& Result :
		SurfaceResults)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("surfaceId"), Result.SurfaceId);
		Item->SetStringField(TEXT("optionId"), Result.OptionId);
		Item->SetStringField(
			TEXT("materialVariantId"),
			Result.MaterialVariantId);
		Item->SetStringField(TEXT("component"), Result.Component);
		Item->SetStringField(TEXT("slot"), Result.Slot);
		Item->SetStringField(TEXT("before"), Result.Before);
		Item->SetStringField(TEXT("after"), Result.After);
		Item->SetStringField(TEXT("colorPolicy"), Result.ColorPolicy);
		Item->SetNumberField(
			TEXT("componentMaterialSlotCount"),
			Result.ComponentMaterialSlotCount);
		Item->SetBoolField(TEXT("neutralProxy"), Result.bNeutralProxy);
		Item->SetBoolField(TEXT("change"), Result.bChanged);
		Item->SetBoolField(TEXT("visible"), Result.bVisible);
		Item->SetBoolField(TEXT("uniqueSlotHit"), Result.bUniqueSlotHit);
		Item->SetBoolField(TEXT("allSlotsMatch"), Result.bAllSlotsMatch);
		Item->SetStringField(TEXT("receiptCode"), Result.ReceiptCode);
		SurfaceValues.Add(MakeShared<FJsonValueObject>(Item));
	}
	Root->SetArrayField(TEXT("surfaces"), MoveTemp(SurfaceValues));
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
			TEXT("GUI 材质槽探针通过：40 surface，报告=%s，截图=%s"),
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
