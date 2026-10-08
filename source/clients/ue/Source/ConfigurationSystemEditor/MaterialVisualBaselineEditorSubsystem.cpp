#include "MaterialVisualBaselineEditorSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "MaterialVisualBaselineProbeActor.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogMaterialVisualBaselineEditor, Log, All);

namespace MaterialVisualBaselineEditor
{
	const FTransform ProbeTransform(
		FRotator::ZeroRotator,
		FVector::ZeroVector,
		FVector::OneVector);
	const FTransform LightTransform(
		FRotator(-90.0, 0.0, 0.0),
		FVector(0.0, 0.0, 300.0),
		FVector::OneVector);
	const FTransform CameraTransform(
		FRotator(-36.2538, 90.0, 0.0),
		FVector(0.0, -300.0, 220.0),
		FVector::OneVector);
	constexpr float CameraFov = 35.0f;
	constexpr float DirectionalLightLux = 5.0f;
	constexpr float ExposureBias = 8.0f;
	constexpr float WhiteTemperature = 6500.0f;
	constexpr TCHAR CameraTag[] = TEXT("MaterialVisualBaseline.Camera");

	template <typename ActorType>
	ActorType* SpawnOnlyActor(
		UWorld* World,
		const TCHAR* Label,
		const FTransform& Transform)
	{
		ActorType* Result = nullptr;
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			if (It->GetActorLabel() != Label)
			{
				continue;
			}
			if (Result == nullptr)
			{
				Result = *It;
			}
			else
			{
				World->EditorDestroyActor(*It, false);
			}
		}
		if (Result == nullptr)
		{
			Result = World->SpawnActor<ActorType>(
				ActorType::StaticClass(),
				Transform.GetLocation(),
				Transform.Rotator());
		}
		if (Result != nullptr)
		{
			Result->SetActorLabel(Label);
			Result->SetActorTransform(Transform);
			Result->Tags.AddUnique(TEXT("MaterialVisualBaseline"));
		}
		return Result;
	}

	template <typename ActorType>
	ActorType* FindActorByLabel(UWorld* World, const TCHAR* Label)
	{
		for (TActorIterator<ActorType> It(World); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				return *It;
			}
		}
		return nullptr;
	}
}

void UMaterialVisualBaselineEditorSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CreateSceneConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ConfigurationSystem.MaterialVisualBaseline.CreateScene"),
		TEXT("幂等创建 /Game/Maps/L_MaterialVisualBaseline 固定材质视觉基准场景。"),
		FConsoleCommandWithArgsDelegate::CreateUObject(
			this,
			&UMaterialVisualBaselineEditorSubsystem::CreateSceneCommand),
		ECVF_Default);
	RunProbeConsoleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ConfigurationSystem.MaterialVisualBaseline.Run"),
		TEXT("在当前 Editor viewport 遍历 352 个 variant；可传 Output= 与 StableSeconds=。"),
		FConsoleCommandWithArgsDelegate::CreateUObject(
			this,
			&UMaterialVisualBaselineEditorSubsystem::RunProbeCommand),
		ECVF_Default);
}

void UMaterialVisualBaselineEditorSubsystem::Deinitialize()
{
	if (ProbeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ProbeTickerHandle);
		ProbeTickerHandle.Reset();
	}
	if (CreateSceneConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(
			CreateSceneConsoleCommand);
		CreateSceneConsoleCommand = nullptr;
	}
	if (RunProbeConsoleCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(
			RunProbeConsoleCommand);
		RunProbeConsoleCommand = nullptr;
	}
	ActiveProbe.Reset();
	Super::Deinitialize();
}

bool UMaterialVisualBaselineEditorSubsystem::CreateOrRefreshScene(
	FString& OutError)
{
	using namespace MaterialVisualBaselineEditor;
	OutError.Reset();
	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
	if (World == nullptr)
	{
		OutError = TEXT("无法创建空白 Editor 世界。");
		return false;
	}
	World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();

	AMaterialVisualBaselineProbeActor* Probe =
		SpawnOnlyActor<AMaterialVisualBaselineProbeActor>(
			World,
			ProbeActorLabel,
			ProbeTransform);
	ADirectionalLight* Light = SpawnOnlyActor<ADirectionalLight>(
		World,
		LightActorLabel,
		LightTransform);
	ACameraActor* Camera = SpawnOnlyActor<ACameraActor>(
		World,
		CameraActorLabel,
		CameraTransform);
	APostProcessVolume* Exposure = SpawnOnlyActor<APostProcessVolume>(
		World,
		ExposureActorLabel,
		FTransform::Identity);
	if (!IsValid(Probe) || !IsValid(Light) || !IsValid(Camera)
		|| !IsValid(Exposure))
	{
		OutError = TEXT("固定平面、DirectionalLight 或相机创建失败。");
		return false;
	}

	Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Light->GetLightComponent()->SetIntensity(DirectionalLightLux);
	Light->GetLightComponent()->SetLightColor(FLinearColor::White);
	Light->GetLightComponent()->SetCastShadows(false);

	Camera->Tags.AddUnique(CameraTag);
	UCameraComponent* CameraComponent = Camera->GetCameraComponent();
	CameraComponent->SetFieldOfView(CameraFov);
	CameraComponent->SetConstraintAspectRatio(false);
	CameraComponent->PostProcessBlendWeight = 1.0f;
	CameraComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
	CameraComponent->PostProcessSettings.AutoExposureMethod =
		EAutoExposureMethod::AEM_Manual;
	CameraComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
	CameraComponent->PostProcessSettings.AutoExposureBias = ExposureBias;
	CameraComponent->PostProcessSettings.bOverride_WhiteTemp = true;
	CameraComponent->PostProcessSettings.WhiteTemp = WhiteTemperature;
	CameraComponent->PostProcessSettings.bOverride_WhiteTint = true;
	CameraComponent->PostProcessSettings.WhiteTint = 0.0f;

	Exposure->bUnbound = true;
	Exposure->Priority = 1000.0f;
	Exposure->BlendWeight = 1.0f;
	Exposure->Settings.bOverride_AutoExposureMethod = true;
	Exposure->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Exposure->Settings.bOverride_AutoExposureBias = true;
	Exposure->Settings.AutoExposureBias = ExposureBias;
	Exposure->Settings.bOverride_WhiteTemp = true;
	Exposure->Settings.WhiteTemp = WhiteTemperature;
	Exposure->Settings.bOverride_WhiteTint = true;
	Exposure->Settings.WhiteTint = 0.0f;

	const bool bSaved =
		UEditorLoadingAndSavingUtils::SaveMap(World, MapPackageName);
	if (!bSaved)
	{
		OutError = TEXT("固定材质视觉基准地图保存失败。");
	}
	return bSaved;
}

void UMaterialVisualBaselineEditorSubsystem::CreateSceneCommand(
	const TArray<FString>& Args)
{
	(void)Args;
	FString Error;
	if (CreateOrRefreshScene(Error))
	{
		UE_LOG(
			LogMaterialVisualBaselineEditor,
			Display,
			TEXT("固定材质视觉基准地图已创建/刷新：%s"),
			MapPackageName);
	}
	else
	{
		UE_LOG(
			LogMaterialVisualBaselineEditor,
			Error,
			TEXT("创建材质视觉基准地图失败：%s"),
			*Error);
	}
}

void UMaterialVisualBaselineEditorSubsystem::RunProbeCommand(
	const TArray<FString>& Args)
{
	using namespace MaterialVisualBaselineEditor;
	if (ProbeTickerHandle.IsValid())
	{
		UE_LOG(LogMaterialVisualBaselineEditor, Warning, TEXT("Editor 材质基准探针已在运行。"));
		return;
	}

	FString MapFilename;
	if (!FPackageName::DoesPackageExist(MapPackageName, &MapFilename))
	{
		UE_LOG(
			LogMaterialVisualBaselineEditor,
			Error,
			TEXT("基准地图不存在；请先执行 ConfigurationSystem.MaterialVisualBaseline.CreateScene。"));
		return;
	}
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapFilename);
	AMaterialVisualBaselineProbeActor* Probe =
		FindActorByLabel<AMaterialVisualBaselineProbeActor>(
			World,
			ProbeActorLabel);
	ACameraActor* Camera = FindActorByLabel<ACameraActor>(
		World,
		CameraActorLabel);
	FViewport* Viewport = GEditor != nullptr ? GEditor->GetActiveViewport() : nullptr;
	FEditorViewportClient* ViewportClient = Viewport != nullptr
		? static_cast<FEditorViewportClient*>(Viewport->GetClient())
		: nullptr;
	if (!IsValid(Probe) || !IsValid(Camera) || Viewport == nullptr
		|| ViewportClient == nullptr)
	{
		UE_LOG(
			LogMaterialVisualBaselineEditor,
			Error,
			TEXT("固定场景或真实 Editor viewport 未就绪。"));
		return;
	}

	ViewportClient->SetViewLocation(CameraTransform.GetLocation());
	ViewportClient->SetViewRotation(CameraTransform.Rotator());
	ViewportClient->ViewFOV = CameraFov;
	ViewportClient->SetViewMode(VMI_Lit);
	ViewportClient->SetGameView(true);
	ViewportClient->SetRealtime(true);
	ViewportClient->Invalidate();

	const FString Arguments = FString::Join(Args, TEXT(" "));
	FString Output;
	FParse::Value(*Arguments, TEXT("Output="), Output);
	float WaitSeconds =
		AMaterialVisualBaselineProbeActor::DefaultStableWaitSeconds;
	FParse::Value(*Arguments, TEXT("StableSeconds="), WaitSeconds);
	if (!Probe->StartProbe(TEXT("editor"), Output, WaitSeconds, false))
	{
		return;
	}
	ActiveProbe = Probe;
	ProbeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(
			this,
			&UMaterialVisualBaselineEditorSubsystem::TickProbe));
}

bool UMaterialVisualBaselineEditorSubsystem::TickProbe(
	const float DeltaSeconds)
{
	FViewport* Viewport = GEditor != nullptr ? GEditor->GetActiveViewport() : nullptr;
	if (Viewport != nullptr)
	{
		Viewport->InvalidateDisplay();
	}
	if (!ActiveProbe.IsValid()
		|| !ActiveProbe->AdvanceProbe(DeltaSeconds, Viewport))
	{
		if (ActiveProbe.IsValid())
		{
			if (ActiveProbe->DidProbeSucceed())
			{
				UE_LOG(
					LogMaterialVisualBaselineEditor,
					Display,
					TEXT("Editor 材质视觉基准 manifest：%s"),
					*ActiveProbe->GetManifestPath());
			}
			else
			{
				UE_LOG(
					LogMaterialVisualBaselineEditor,
					Error,
					TEXT("Editor 材质视觉基准失败，manifest：%s"),
					*ActiveProbe->GetManifestPath());
			}
		}
		ActiveProbe.Reset();
		ProbeTickerHandle.Reset();
		return false;
	}
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMaterialVisualBaselineSceneAutomationTest,
	"ConfigurationSystem.Editor.MaterialVisualBaseline.SceneContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMaterialVisualBaselineSceneAutomationTest::RunTest(
	const FString& Parameters)
{
	using namespace MaterialVisualBaselineEditor;
	(void)Parameters;
	FString Error;
	TestTrue(
		*FString::Printf(TEXT("创建固定视觉基准场景：%s"), *Error),
		UMaterialVisualBaselineEditorSubsystem::CreateOrRefreshScene(Error));
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	AMaterialVisualBaselineProbeActor* Probe =
		FindActorByLabel<AMaterialVisualBaselineProbeActor>(
			World,
			UMaterialVisualBaselineEditorSubsystem::ProbeActorLabel);
	ADirectionalLight* Light =
		FindActorByLabel<ADirectionalLight>(
			World,
			UMaterialVisualBaselineEditorSubsystem::LightActorLabel);
	ACameraActor* Camera =
		FindActorByLabel<ACameraActor>(
			World,
			UMaterialVisualBaselineEditorSubsystem::CameraActorLabel);
	APostProcessVolume* Exposure =
		FindActorByLabel<APostProcessVolume>(
			World,
			UMaterialVisualBaselineEditorSubsystem::ExposureActorLabel);
	TestNotNull(TEXT("场景含唯一测试平面 Actor"), Probe);
	TestNotNull(TEXT("场景含唯一 DirectionalLight"), Light);
	TestNotNull(TEXT("场景含唯一固定相机"), Camera);
	TestNotNull(TEXT("场景含唯一固定曝光 Volume"), Exposure);
	if (!IsValid(Probe) || !IsValid(Light) || !IsValid(Camera)
		|| !IsValid(Exposure))
	{
		return false;
	}
	TestTrue(TEXT("测试平面位于原点"),
		Probe->GetActorLocation().Equals(FVector::ZeroVector));
	TestTrue(TEXT("测试平面朝 +Z"),
		Probe->GetActorRotation().Equals(FRotator::ZeroRotator));
	TestTrue(TEXT("DirectionalLight 垂直向下"),
		Light->GetActorRotation().Equals(LightTransform.Rotator(), 0.001));
	TestTrue(TEXT("固定相机 Transform"),
		Camera->GetActorTransform().Equals(CameraTransform, 0.001));
	TestEqual(
		TEXT("DirectionalLight 固定照度"),
		Light->GetLightComponent()->Intensity,
		DirectionalLightLux);
	TestEqual(
		TEXT("固定相机 FOV"),
		Camera->GetCameraComponent()->FieldOfView,
		CameraFov);
	TestEqual(
		TEXT("固定手动曝光"),
		Camera->GetCameraComponent()->PostProcessSettings.AutoExposureMethod,
		EAutoExposureMethod::AEM_Manual);
	TestEqual(
		TEXT("固定曝光补偿"),
		Camera->GetCameraComponent()->PostProcessSettings.AutoExposureBias,
		ExposureBias);
	TestTrue(TEXT("固定曝光 Volume 为无限范围"), Exposure->bUnbound);
	TestEqual(
		TEXT("固定曝光 Volume 使用手动曝光"),
		Exposure->Settings.AutoExposureMethod,
		EAutoExposureMethod::AEM_Manual);
	return true;
}

#endif
