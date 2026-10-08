#include "MaterialVisualBaselineProbeActor.h"

#include "AutomotiveMaterialLibrary.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMaterialVisualBaseline, Log, All);

namespace MaterialVisualBaseline
{
	constexpr double ProbeTimeoutSeconds = 900.0;
	const FTransform CameraTransform(
		FRotator(-36.2538, 90.0, 0.0),
		FVector(0.0, -300.0, 220.0),
		FVector::OneVector);
	constexpr float CameraFov = 35.0f;
	constexpr float ExposureBias = 12.0f;
	constexpr float WhiteTemperature = 6500.0f;
}

AMaterialVisualBaselineProbeActor::AMaterialVisualBaselineProbeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Plane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaselinePlane"));
	Plane->SetupAttachment(SceneRoot);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		Plane->SetStaticMesh(PlaneMesh.Object);
	}
	Plane->SetMobility(EComponentMobility::Movable);
	Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plane->SetCastShadow(false);
	Plane->SetRelativeTransform(FTransform(
		FRotator::ZeroRotator,
		FVector::ZeroVector,
		FVector(4.0, 4.0, 4.0)));

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(
		TEXT("BaselineSceneCapture"));
	SceneCapture->SetupAttachment(SceneRoot);
	SceneCapture->SetRelativeTransform(MaterialVisualBaseline::CameraTransform);
	SceneCapture->FOVAngle = MaterialVisualBaseline::CameraFov;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->bAlwaysPersistRenderingState = true;
	SceneCapture->PostProcessBlendWeight = 1.0f;
	SceneCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	SceneCapture->PostProcessSettings.AutoExposureMethod =
		EAutoExposureMethod::AEM_Manual;
	SceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
	SceneCapture->PostProcessSettings.AutoExposureBias =
		MaterialVisualBaseline::ExposureBias;
	SceneCapture->PostProcessSettings.bOverride_WhiteTemp = true;
	SceneCapture->PostProcessSettings.WhiteTemp =
		MaterialVisualBaseline::WhiteTemperature;
	SceneCapture->PostProcessSettings.bOverride_WhiteTint = true;
	SceneCapture->PostProcessSettings.WhiteTint = 0.0f;
}

void AMaterialVisualBaselineProbeActor::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("MaterialVisualBaselineProbe")))
	{
		SetActorTickEnabled(false);
		return;
	}

	FString Output;
	FParse::Value(
		FCommandLine::Get(),
		TEXT("MaterialVisualBaselineOutput="),
		Output);
	float WaitSeconds = DefaultStableWaitSeconds;
	FParse::Value(
		FCommandLine::Get(),
		TEXT("MaterialVisualBaselineStableSeconds="),
		WaitSeconds);
	StartProbe(TEXT("runtime"), Output, WaitSeconds, true);
}

void AMaterialVisualBaselineProbeActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bRunning)
	{
		return;
	}
	AdvanceProbe(DeltaSeconds);
}

bool AMaterialVisualBaselineProbeActor::BuildVariantPlan(
	const UAutomotiveMaterialLibrary* InLibrary,
	TArray<FMaterialVisualBaselineVariant>& OutVariants,
	FString& OutError)
{
	OutVariants.Reset();
	OutError.Reset();
	if (!IsValid(InLibrary))
	{
		OutError = TEXT("无法加载 DA_SC01MaterialLibrary。");
		return false;
	}
	if (InLibrary->Variants.Num() != ExpectedVariantCount)
	{
		OutError = FString::Printf(
			TEXT("材质库 variant 数量应为 %d，实际为 %d。"),
			ExpectedVariantCount,
			InLibrary->Variants.Num());
		return false;
	}

	TArray<FString> VariantIds;
	InLibrary->Variants.GetKeys(VariantIds);
	VariantIds.Sort();
	OutVariants.Reserve(VariantIds.Num());
	for (const FString& VariantId : VariantIds)
	{
		const TSoftObjectPtr<UMaterialInterface>* Material =
			InLibrary->Variants.Find(VariantId);
		if (VariantId.IsEmpty() || Material == nullptr || Material->IsNull())
		{
			OutError = FString::Printf(
				TEXT("variant '%s' 没有有效材质软引用。"),
				*VariantId);
			OutVariants.Reset();
			return false;
		}
		FMaterialVisualBaselineVariant& Entry =
			OutVariants.AddDefaulted_GetRef();
		Entry.VariantId = VariantId;
		Entry.Material = *Material;
	}
	return true;
}

bool AMaterialVisualBaselineProbeActor::StartProbe(
	const FString& InMode,
	const FString& InOutputDirectory,
	const float InStableWaitSeconds,
	const bool bInExitOnComplete)
{
	if (bRunning)
	{
		UE_LOG(LogMaterialVisualBaseline, Warning, TEXT("材质视觉基准探针已在运行。"));
		return false;
	}

	Mode = InMode;
	OutputDirectory = InOutputDirectory.IsEmpty()
		? FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("MaterialVisualBaseline"),
			FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")))
		: InOutputDirectory;
	OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	ManifestPath = FPaths::Combine(OutputDirectory, TEXT("manifest.json"));
	StableWaitSeconds = FMath::Max(0.0f, InStableWaitSeconds);
	bExitOnComplete = bInExitOnComplete;
	bRunning = true;

	Library = LoadObject<UAutomotiveMaterialLibrary>(nullptr, LibraryObjectPath);
	FString Error;
	if (!BuildVariantPlan(Library, Variants, Error))
	{
		Finish(Error);
		return false;
	}
	if (!IsValid(SceneCapture))
	{
		Finish(TEXT("离屏 SceneCaptureComponent2D 无效。"));
		return false;
	}
	RenderTarget = NewObject<UTextureRenderTarget2D>(
		this,
		TEXT("MaterialVisualBaselineRenderTarget"));
	if (!IsValid(RenderTarget))
	{
		Finish(TEXT("无法创建离屏 TextureRenderTarget2D。"));
		return false;
	}
	RenderTarget->ClearColor = FLinearColor::Black;
	RenderTarget->TargetGamma = 2.2f;
	RenderTarget->InitCustomFormat(
		RenderWidth,
		RenderHeight,
		PF_B8G8R8A8,
		false);
	RenderTarget->UpdateResourceImmediate(true);
	SceneCapture->TextureTarget = RenderTarget;

	Results.Reset(ExpectedVariantCount);
	CurrentVariantIndex = 0;
	TotalElapsedSeconds = 0.0;
	bSucceeded = false;
	bVariantPrepared = false;
	SetActorTickEnabled(Mode == TEXT("runtime"));
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);

	UE_LOG(
		LogMaterialVisualBaseline,
		Display,
		TEXT("开始材质视觉基准探针：mode=%s variants=%d output=%s"),
		*Mode,
		Variants.Num(),
		*OutputDirectory);
	return true;
}

bool AMaterialVisualBaselineProbeActor::AdvanceProbe(
	const float DeltaSeconds)
{
	if (!bRunning)
	{
		return false;
	}
	TotalElapsedSeconds += DeltaSeconds;
	if (TotalElapsedSeconds > MaterialVisualBaseline::ProbeTimeoutSeconds)
	{
		Finish(TEXT("探针总超时。"));
		return false;
	}

	if (!bVariantPrepared)
	{
		FString Error;
		if (!PrepareVariant(Error))
		{
			Finish(Error);
			return false;
		}
		return true;
	}

	VariantElapsedSeconds += DeltaSeconds;
	const bool bShadersStable =
		GShaderCompilingManager == nullptr
		|| !GShaderCompilingManager->IsCompiling();
	if (bShadersStable)
	{
		++StableFrameCount;
	}
	else
	{
		StableFrameCount = 0;
	}
	if (VariantElapsedSeconds < StableWaitSeconds
		|| StableFrameCount < RequiredStableFrames)
	{
		return true;
	}
	FString Error;
	if (!CaptureVariant(Error))
	{
		Finish(Error);
		return false;
	}
	++CurrentVariantIndex;
	bVariantPrepared = false;
	if (CurrentVariantIndex == Variants.Num())
	{
		Finish(FString());
		return false;
	}
	return true;
}

bool AMaterialVisualBaselineProbeActor::PrepareVariant(FString& OutError)
{
	if (!Variants.IsValidIndex(CurrentVariantIndex) || !IsValid(Plane))
	{
		OutError = TEXT("探针状态或测试平面无效。");
		return false;
	}
	const FMaterialVisualBaselineVariant& Entry = Variants[CurrentVariantIndex];
	UMaterialInterface* Material = Entry.Material.LoadSynchronous();
	if (!IsValid(Material))
	{
		OutError = FString::Printf(
			TEXT("无法加载 variant '%s' 的材质。"),
			*Entry.VariantId);
		return false;
	}
	Plane->SetMaterial(0, Material);
	Plane->MarkRenderStateDirty();
	CurrentVariantId = Entry.VariantId;
	CurrentMaterialPath = Material->GetPathName();
	VariantElapsedSeconds = 0.0;
	StableFrameCount = 0;
	bVariantPrepared = true;
	return true;
}

bool AMaterialVisualBaselineProbeActor::CaptureVariant(
	FString& OutError)
{
	if (!IsValid(SceneCapture) || !IsValid(RenderTarget)
		|| SceneCapture->TextureTarget != RenderTarget)
	{
		OutError = FString::Printf(
			TEXT("variant '%s' 的离屏渲染资源无效。"),
			*CurrentVariantId);
		return false;
	}

	SceneCapture->CaptureScene();
	FTextureRenderTargetResource* RenderTargetResource =
		RenderTarget->GameThread_GetRenderTargetResource();
	const FIntPoint Size(RenderTarget->SizeX, RenderTarget->SizeY);
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(true);
	if (Size != FIntPoint(RenderWidth, RenderHeight)
		|| RenderTargetResource == nullptr
		|| !RenderTargetResource->ReadPixels(Pixels, ReadFlags)
		|| Pixels.Num() != static_cast<int64>(Size.X) * Size.Y)
	{
		OutError = FString::Printf(
			TEXT("variant '%s' 的 RenderTarget 回读失败。"),
			*CurrentVariantId);
		return false;
	}
	uint64 LuminanceSum = 0;
	int32 VisiblePixelCount = 0;
	for (const FColor& Pixel : Pixels)
	{
		const uint8 Luminance = static_cast<uint8>(
			(54 * static_cast<uint32>(Pixel.R)
				+ 183 * static_cast<uint32>(Pixel.G)
				+ 19 * static_cast<uint32>(Pixel.B)) >> 8);
		LuminanceSum += Luminance;
		VisiblePixelCount += Luminance >= 8 ? 1 : 0;
	}
	const double MeanLuminance =
		static_cast<double>(LuminanceSum) / Pixels.Num();
	const double VisiblePixelRatio =
		static_cast<double>(VisiblePixelCount) / Pixels.Num();
	if (MeanLuminance < 1.0 || VisiblePixelRatio < 0.01)
	{
		OutError = FString::Printf(
			TEXT("variant '%s' 截图无有效照明：mean=%.3f visibleRatio=%.5f。"),
			*CurrentVariantId,
			MeanLuminance,
			VisiblePixelRatio);
		return false;
	}

	const FString RelativePath = FString::Printf(
		TEXT("renders/%04d-%s.png"),
		CurrentVariantIndex,
		*MakeSafeFilename(CurrentVariantId));
	const FString AbsolutePath =
		FPaths::Combine(OutputDirectory, RelativePath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), true);
	if (!FImageUtils::SaveImageByExtension(
		*AbsolutePath,
		FImageView(Pixels.GetData(), Size.X, Size.Y, EGammaSpace::sRGB)))
	{
		OutError = FString::Printf(
			TEXT("variant '%s' 的 PNG 写入失败。"),
			*CurrentVariantId);
		return false;
	}

	TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetNumberField(TEXT("index"), CurrentVariantIndex);
	Item->SetStringField(TEXT("variantId"), CurrentVariantId);
	Item->SetStringField(TEXT("materialPath"), CurrentMaterialPath);
	Item->SetStringField(
		TEXT("path"),
		RelativePath.Replace(TEXT("\\"), TEXT("/")));
	Item->SetNumberField(TEXT("width"), Size.X);
	Item->SetNumberField(TEXT("height"), Size.Y);
	Item->SetStringField(TEXT("format"), TEXT("png"));
	Item->SetStringField(TEXT("colorSpace"), TEXT("sRGB"));
	Item->SetNumberField(TEXT("meanLuminance"), MeanLuminance);
	Item->SetNumberField(TEXT("visiblePixelRatio"), VisiblePixelRatio);
	Item->SetNumberField(TEXT("stableWaitSeconds"), VariantElapsedSeconds);
	Item->SetNumberField(TEXT("stableFrames"), StableFrameCount);
	Item->SetStringField(TEXT("status"), TEXT("ready"));
	Results.Add(MakeShared<FJsonValueObject>(Item));
	return true;
}

bool AMaterialVisualBaselineProbeActor::WriteManifest(
	const FString& FatalError)
{
	int32 ReadyCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Results)
	{
		if (Value.IsValid()
			&& Value->AsObject()->GetStringField(TEXT("status"))
				== TEXT("ready"))
		{
			++ReadyCount;
		}
	}
	TSharedRef<FJsonObject> Scene = MakeShared<FJsonObject>();
	Scene->SetStringField(TEXT("map"), TEXT("/Game/Maps/L_MaterialVisualBaseline"));
	Scene->SetStringField(TEXT("plane"), TEXT("origin, normal +Z, scale 4"));
	Scene->SetStringField(TEXT("directionalLight"), TEXT("vertical-down"));
	Scene->SetNumberField(TEXT("directionalLightLux"), 20000.0);
	Scene->SetStringField(
		TEXT("cameraTransform"),
		TEXT("Location=(0,-300,220) Rotation=(-36,90,0) FOV=35"));
	Scene->SetStringField(TEXT("exposure"), TEXT("manual, bias 12, whiteTemp 6500K"));
	Scene->SetStringField(TEXT("renderer"), TEXT("SceneCaptureComponent2D"));
	Scene->SetNumberField(TEXT("renderWidth"), RenderWidth);
	Scene->SetNumberField(TEXT("renderHeight"), RenderHeight);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schemaVersion"), TEXT("1.0.0"));
	Root->SetStringField(TEXT("probe"), TEXT("SC01MaterialVisualBaseline"));
	Root->SetStringField(TEXT("generatedAt"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("mode"), Mode);
	Root->SetStringField(TEXT("library"), LibraryObjectPath);
	Root->SetNumberField(TEXT("expectedVariantCount"), ExpectedVariantCount);
	Root->SetNumberField(TEXT("capturedVariantCount"), ReadyCount);
	Root->SetBoolField(
		TEXT("success"),
		FatalError.IsEmpty() && ReadyCount == ExpectedVariantCount);
	Root->SetStringField(TEXT("failureReason"), FatalError);
	Root->SetNumberField(TEXT("minimumStableWaitSeconds"), StableWaitSeconds);
	Root->SetNumberField(TEXT("requiredStableFrames"), RequiredStableFrames);
	Root->SetObjectField(TEXT("scene"), Scene);
	Root->SetArrayField(TEXT("variants"), Results);

	FString Json;
	const bool bSerialized = FJsonSerializer::Serialize(
		Root,
		TJsonWriterFactory<>::Create(&Json));
	return bSerialized
		&& IFileManager::Get().MakeDirectory(*OutputDirectory, true)
		&& FFileHelper::SaveStringToFile(
			Json,
			*ManifestPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AMaterialVisualBaselineProbeActor::Finish(const FString& FatalError)
{
	if (!bRunning && ManifestPath.IsEmpty())
	{
		UE_LOG(LogMaterialVisualBaseline, Error, TEXT("%s"), *FatalError);
		return;
	}
	if (!FatalError.IsEmpty())
	{
		AppendRemainingFailures(FatalError);
	}
	const bool bWritten = WriteManifest(FatalError);
	bSucceeded =
		bWritten && FatalError.IsEmpty()
		&& Results.Num() == ExpectedVariantCount;
	bRunning = false;
	SetActorTickEnabled(false);
	if (bSucceeded)
	{
		UE_LOG(
			LogMaterialVisualBaseline,
			Display,
			TEXT("材质视觉基准探针完成：%s"),
			*ManifestPath);
	}
	else
	{
		UE_LOG(
			LogMaterialVisualBaseline,
			Error,
			TEXT("材质视觉基准探针失败：%s"),
			*ManifestPath);
	}
	if (bExitOnComplete)
	{
		FPlatformMisc::RequestExitWithStatus(false, bSucceeded ? 0 : 12);
	}
}

void AMaterialVisualBaselineProbeActor::AppendRemainingFailures(
	const FString& Error)
{
	for (int32 Index = Results.Num(); Index < Variants.Num(); ++Index)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("index"), Index);
		Item->SetStringField(TEXT("variantId"), Variants[Index].VariantId);
		Item->SetStringField(
			TEXT("materialPath"),
			Variants[Index].Material.ToSoftObjectPath().ToString());
		Item->SetStringField(TEXT("path"), TEXT(""));
		Item->SetStringField(TEXT("status"), TEXT("failed"));
		Item->SetStringField(TEXT("error"), Error);
		Results.Add(MakeShared<FJsonValueObject>(Item));
	}
}

FString AMaterialVisualBaselineProbeActor::MakeSafeFilename(
	const FString& VariantId)
{
	FString Result = VariantId;
	for (TCHAR& Character : Result)
	{
		if (!FChar::IsAlnum(Character)
			&& Character != TEXT('-')
			&& Character != TEXT('_'))
		{
			Character = TEXT('_');
		}
	}
	return Result;
}

bool AMaterialVisualBaselineProbeActor::IsProbeRunning() const
{
	return bRunning;
}

bool AMaterialVisualBaselineProbeActor::DidProbeSucceed() const
{
	return bSucceeded;
}

const FString& AMaterialVisualBaselineProbeActor::GetManifestPath() const
{
	return ManifestPath;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialVisualBaselinePlanAutomationTest,
	"ConfigurationSystem.Runtime.MaterialVisualBaseline.VariantPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialVisualBaselinePlanAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UAutomotiveMaterialLibrary* ProjectLibrary =
		LoadObject<UAutomotiveMaterialLibrary>(
			nullptr,
			AMaterialVisualBaselineProbeActor::LibraryObjectPath);
	TestNotNull(TEXT("加载 DA_SC01MaterialLibrary"), ProjectLibrary);
	TArray<FMaterialVisualBaselineVariant> ProjectPlan;
	FString ProjectError;
	TestTrue(
		*FString::Printf(TEXT("真实材质库可构建 352 项计划：%s"), *ProjectError),
		AMaterialVisualBaselineProbeActor::BuildVariantPlan(
			ProjectLibrary,
			ProjectPlan,
			ProjectError));
	TestEqual(
		TEXT("真实材质库计划包含 352 项"),
		ProjectPlan.Num(),
		AMaterialVisualBaselineProbeActor::ExpectedVariantCount);

	UAutomotiveMaterialLibrary* TestLibrary =
		NewObject<UAutomotiveMaterialLibrary>(GetTransientPackage());
	for (int32 Index =
			AMaterialVisualBaselineProbeActor::ExpectedVariantCount - 1;
		Index >= 0;
		--Index)
	{
		TestLibrary->Variants.Add(
			FString::Printf(TEXT("variant-%03d"), Index),
			TSoftObjectPtr<UMaterialInterface>(
				FSoftObjectPath(TEXT("/Engine/EngineMaterials/DefaultMaterial."
					"DefaultMaterial"))));
	}
	TArray<FMaterialVisualBaselineVariant> Plan;
	FString Error;
	TestTrue(
		*FString::Printf(TEXT("构建 352 variant 固定计划：%s"), *Error),
		AMaterialVisualBaselineProbeActor::BuildVariantPlan(
			TestLibrary,
			Plan,
			Error));
	TestEqual(
		TEXT("固定计划包含 352 项"),
		Plan.Num(),
		AMaterialVisualBaselineProbeActor::ExpectedVariantCount);
	if (Plan.Num()
		== AMaterialVisualBaselineProbeActor::ExpectedVariantCount)
	{
		TestEqual(TEXT("计划按 variantId 排序"), Plan[0].VariantId,
			FString(TEXT("variant-000")));
		TestEqual(TEXT("计划末项稳定"), Plan.Last().VariantId,
			FString(TEXT("variant-351")));
	}
	TestLibrary->Variants.Remove(TEXT("variant-351"));
	TestFalse(
		TEXT("variant 数量不是 352 时拒绝"),
		AMaterialVisualBaselineProbeActor::BuildVariantPlan(
			TestLibrary,
			Plan,
			Error));
	return true;
}

#endif
