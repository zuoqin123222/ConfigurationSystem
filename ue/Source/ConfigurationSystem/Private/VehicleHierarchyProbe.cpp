#include "VehicleHierarchyProbe.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ReversiblePartActuatorComponent.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "VehicleHierarchyAuditor.h"

DEFINE_LOG_CATEGORY_STATIC(LogVehicleHierarchyProbe, Log, All);

namespace VehicleHierarchyProbe
{
	constexpr float FixedStepSeconds = 0.01f;
	constexpr double ErrorTolerance = 0.001;
	constexpr double StepJumpThreshold = 2.2;

	USceneComponent* FindSceneComponentByName(AActor* Actor, const FName Name)
	{
		if (Actor == nullptr)
		{
			return nullptr;
		}

		TInlineComponentArray<USceneComponent*> Components;
		Actor->GetComponents(Components);
		for (USceneComponent* Component : Components)
		{
			if (Component != nullptr && Component->GetFName() == Name)
			{
				return Component;
			}
		}
		return nullptr;
	}

	USceneComponent* AddPivot(
		AActor& Actor,
		const FName Name,
		const FName Tag,
		USceneComponent* Parent,
		const FVector& RelativeLocation)
	{
		USceneComponent* Component = NewObject<USceneComponent>(&Actor, Name);
		Component->SetMobility(EComponentMobility::Movable);
		Component->ComponentTags.Add(Tag);
		Component->SetupAttachment(Parent);
		Component->SetRelativeLocation(RelativeLocation);
		Actor.AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}

	UStaticMeshComponent* AddCube(
		AActor& Actor,
		UStaticMesh* CubeMesh,
		const FName Name,
		const FName Tag,
		USceneComponent* Parent,
		const FVector& RelativeLocation,
		const FVector& RelativeScale)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(&Actor, Name);
		Component->SetMobility(EComponentMobility::Movable);
		if (!Tag.IsNone())
		{
			Component->ComponentTags.Add(Tag);
		}
		Component->SetStaticMesh(CubeMesh);
		Component->SetRelativeLocation(RelativeLocation);
		Component->SetRelativeScale3D(RelativeScale);
		if (Parent != nullptr)
		{
			Component->SetupAttachment(Parent);
		}
		else
		{
			Actor.SetRootComponent(Component);
		}
		Actor.AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}
}

void FVehicleHierarchyProbe::Start()
{
	OutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("VehicleHierarchyProbe"),
		TEXT("VehicleHierarchyProbe.json"));
	FParse::Value(FCommandLine::Get(), TEXT("VehicleHierarchyProbeOutput="), OutputPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(
		this,
		&FVehicleHierarchyProbe::OnEngineLoopInitComplete);
}

void FVehicleHierarchyProbe::Shutdown()
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
	if (ProbeActor.IsValid())
	{
		ProbeActor->Destroy();
		ProbeActor.Reset();
	}
	Actuator = nullptr;
	ActuatedPart = nullptr;
}

void FVehicleHierarchyProbe::OnEngineLoopInitComplete()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}

	StartTimeSeconds = FPlatformTime::Seconds();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FVehicleHierarchyProbe::Tick));
}

bool FVehicleHierarchyProbe::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	if (FPlatformTime::Seconds() - StartTimeSeconds > TimeoutSeconds)
	{
		Finish(false, TEXT("等待 Runtime World 超时。"));
		return false;
	}

	if (GEngine == nullptr || GEngine->GameViewport == nullptr
		|| GEngine->GameViewport->GetWorld() == nullptr)
	{
		return true;
	}

	TickerHandle.Reset();
	if (!BuildCubeHierarchy())
	{
		Finish(false, TEXT("无法创建程序化 Cube 车辆层级。"));
		return false;
	}
	RunChecksAndFinish();
	return false;
}

bool FVehicleHierarchyProbe::BuildCubeHierarchy()
{
	UWorld* World = GEngine->GameViewport->GetWorld();
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	bCubeMeshLoaded = CubeMesh != nullptr;
	if (!bCubeMeshLoaded)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("VehicleHierarchyProbeActor");
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (Actor == nullptr)
	{
		return false;
	}
	ProbeActor = Actor;

	using namespace VehicleHierarchyProbe;
	UStaticMeshComponent* Root = AddCube(
		*Actor, CubeMesh, TEXT("RootCube"), TEXT("Vehicle.Root"), nullptr,
		FVector::ZeroVector, FVector(0.2f, 0.2f, 0.1f));
	UStaticMeshComponent* Body = AddCube(
		*Actor, CubeMesh, TEXT("BodyCube"), TEXT("Vehicle.Body"), Root,
		FVector(0.0f, 0.0f, 60.0f), FVector(2.0f, 1.0f, 0.35f));

	// DoorPivot 位于前缘铰链；DoorMesh 无标签并向门板中心偏移。
	USceneComponent* DoorPivot = AddPivot(
		*Actor,
		TEXT("DoorPivot"),
		TEXT("Vehicle.Part.Door.FrontLeft"),
		Body,
		FVector(-40.0f, -75.0f, 10.0f));
	AddCube(
		*Actor, CubeMesh, TEXT("DoorMesh"), NAME_None, DoorPivot,
		FVector(40.0f, 0.0f, 0.0f), FVector(0.8f, 0.08f, 0.45f));

	struct FPartSpec
	{
		const TCHAR* PivotName;
		const TCHAR* MeshName;
		const TCHAR* Tag;
		FVector Location;
		FVector Scale;
	};
	const FPartSpec PartSpecs[] = {
		{ TEXT("HoodPivot"), TEXT("HoodMesh"), TEXT("Vehicle.Part.Hood"),
			FVector(115.0f, 0.0f, 25.0f), FVector(0.55f, 0.85f, 0.08f) },
		{ TEXT("TrunkPivot"), TEXT("TrunkMesh"), TEXT("Vehicle.Part.Trunk"),
			FVector(-115.0f, 0.0f, 25.0f), FVector(0.45f, 0.85f, 0.08f) },
		{ TEXT("WheelFrontLeftPivot"), TEXT("WheelFrontLeftMesh"),
			TEXT("Vehicle.Part.Wheel.FrontLeft"),
			FVector(90.0f, -80.0f, -35.0f), FVector(0.35f, 0.18f, 0.35f) },
		{ TEXT("WheelFrontRightPivot"), TEXT("WheelFrontRightMesh"),
			TEXT("Vehicle.Part.Wheel.FrontRight"),
			FVector(90.0f, 80.0f, -35.0f), FVector(0.35f, 0.18f, 0.35f) },
		{ TEXT("WheelRearLeftPivot"), TEXT("WheelRearLeftMesh"),
			TEXT("Vehicle.Part.Wheel.RearLeft"),
			FVector(-90.0f, -80.0f, -35.0f), FVector(0.35f, 0.18f, 0.35f) },
		{ TEXT("WheelRearRightPivot"), TEXT("WheelRearRightMesh"),
			TEXT("Vehicle.Part.Wheel.RearRight"),
			FVector(-90.0f, 80.0f, -35.0f), FVector(0.35f, 0.18f, 0.35f) }
	};
	for (const FPartSpec& Spec : PartSpecs)
	{
		USceneComponent* Pivot = AddPivot(
			*Actor, Spec.PivotName, Spec.Tag, Body, Spec.Location);
		AddCube(
			*Actor, CubeMesh, Spec.MeshName, NAME_None, Pivot,
			FVector::ZeroVector, Spec.Scale);
	}

	Actuator = NewObject<UReversiblePartActuatorComponent>(Actor, TEXT("DoorActuator"));
	Actor->AddInstanceComponent(Actuator);
	Actuator->RegisterComponent();
	// 探针使用固定步长手动推进，避免帧率影响数值结果。
	Actuator->SetComponentTickEnabled(false);
	Actuator->Duration = 0.5f;
	const FTransform Closed = DoorPivot->GetRelativeTransform();
	FTransform Open = Closed;
	Open.SetRotation(FRotator(0.0f, -70.0f, 0.0f).Quaternion());
	Actuator->BindPart(DoorPivot, Closed, Open);
	ActuatedPart = DoorPivot;
	return true;
}

double FVehicleHierarchyProbe::TransformError(
	const FTransform& Left,
	const FTransform& Right)
{
	const double LocationError = FVector::Distance(Left.GetLocation(), Right.GetLocation());
	const double ScaleError = FVector::Distance(Left.GetScale3D(), Right.GetScale3D());
	const double RotationError = FMath::RadiansToDegrees(
		Left.GetRotation().AngularDistance(Right.GetRotation()));
	return FMath::Max3(LocationError, ScaleError, RotationError);
}

void FVehicleHierarchyProbe::SampleStep(const float DeltaTime)
{
	const FTransform Before = ActuatedPart->GetRelativeTransform();
	Actuator->AdvanceActuation(DeltaTime);
	const FTransform After = ActuatedPart->GetRelativeTransform();
	MaxStepJump = FMath::Max(MaxStepJump, TransformError(Before, After));

	const float Eased = FMath::SmoothStep(0.0f, 1.0f, Actuator->GetProgress());
	FTransform Expected;
	Expected.Blend(
		Actuator->ClosedRelativeTransform,
		Actuator->OpenRelativeTransform,
		Eased);
	MaxInterpolationError = FMath::Max(
		MaxInterpolationError,
		TransformError(After, Expected));
}

void FVehicleHierarchyProbe::DriveTo(const bool bOpen)
{
	Actuator->SetOpen(bOpen);
	for (int32 Step = 0; Step < 10000 && Actuator->IsMoving(); ++Step)
	{
		SampleStep(VehicleHierarchyProbe::FixedStepSeconds);
	}

	const FTransform& Expected = bOpen
		? Actuator->OpenRelativeTransform
		: Actuator->ClosedRelativeTransform;
	MaxTerminalError = FMath::Max(
		MaxTerminalError,
		TransformError(ActuatedPart->GetRelativeTransform(), Expected));
}

void FVehicleHierarchyProbe::RecordNegativeAudit(
	const FString& Name,
	const FString& ExpectedIssueSubstring,
	TFunction<void()> IntroduceError,
	TFunction<void()> Restore)
{
	FNegativeHierarchyAuditTestResult Result;
	Result.Name = Name;
	Result.ExpectedIssueSubstring = ExpectedIssueSubstring;

	IntroduceError();
	const FVehicleHierarchyAuditResult InvalidAudit =
		UVehicleHierarchyAuditor::AuditActor(ProbeActor.Get());
	Result.bAuditFailed = !InvalidAudit.bPassed;
	Result.Issues = InvalidAudit.Issues;
	Result.bExpectedIssueFound = InvalidAudit.Issues.ContainsByPredicate(
		[&ExpectedIssueSubstring](const FString& Issue)
		{
			return Issue.Contains(ExpectedIssueSubstring);
		});

	Restore();
	Result.bRestoredAuditPassed =
		UVehicleHierarchyAuditor::AuditActor(ProbeActor.Get()).bPassed;
	NegativeAuditTests.Add(MoveTemp(Result));
}

void FVehicleHierarchyProbe::RunChecksAndFinish()
{
	const FVehicleHierarchyAuditResult Audit =
		UVehicleHierarchyAuditor::AuditActor(ProbeActor.Get());
	bHierarchyPassed = Audit.bPassed;

	// 在有效基线之上依次注入四种错误；每次审计后立即恢复，再验证基线有效。
	if (bHierarchyPassed)
	{
		AActor* Actor = ProbeActor.Get();
		USceneComponent* Root = VehicleHierarchyProbe::FindSceneComponentByName(Actor, TEXT("RootCube"));
		USceneComponent* Body = VehicleHierarchyProbe::FindSceneComponentByName(Actor, TEXT("BodyCube"));
		USceneComponent* Hood = VehicleHierarchyProbe::FindSceneComponentByName(Actor, TEXT("HoodPivot"));
		USceneComponent* Trunk = VehicleHierarchyProbe::FindSceneComponentByName(Actor, TEXT("TrunkPivot"));
		USceneComponent* WheelRearRight =
			VehicleHierarchyProbe::FindSceneComponentByName(Actor, TEXT("WheelRearRightPivot"));

		RecordNegativeAudit(
			TEXT("missingHoodTag"),
			TEXT("缺少必需 ComponentTag：Vehicle.Part.Hood。"),
			[Hood]()
			{
				if (Hood != nullptr)
				{
					Hood->ComponentTags.RemoveSingle(TEXT("Vehicle.Part.Hood"));
				}
			},
			[Hood]()
			{
				if (Hood != nullptr)
				{
					Hood->ComponentTags.Add(TEXT("Vehicle.Part.Hood"));
				}
			});

		RecordNegativeAudit(
			TEXT("duplicateHoodTagOnBody"),
			TEXT("ComponentTag Vehicle.Part.Hood 重复"),
			[Body]()
			{
				if (Body != nullptr)
				{
					Body->ComponentTags.Add(TEXT("Vehicle.Part.Hood"));
				}
			},
			[Body]()
			{
				if (Body != nullptr)
				{
					Body->ComponentTags.RemoveSingle(TEXT("Vehicle.Part.Hood"));
				}
			});

		RecordNegativeAudit(
			TEXT("staticTrunkPivot"),
			TEXT("组件 TrunkPivot（标签 Vehicle.Part.Trunk）的 Mobility 不是 Movable。"),
			[Trunk]()
			{
				if (Trunk != nullptr)
				{
					Trunk->SetMobility(EComponentMobility::Static);
				}
			},
			[Trunk]()
			{
				if (Trunk != nullptr)
				{
					Trunk->SetMobility(EComponentMobility::Movable);
				}
			});

		RecordNegativeAudit(
			TEXT("wheelRearRightAttachedToRoot"),
			TEXT("Vehicle.Part.Wheel.RearRight Pivot 必须直接挂接到 Vehicle.Body。"),
			[WheelRearRight, Root]()
			{
				if (WheelRearRight != nullptr && Root != nullptr)
				{
					WheelRearRight->AttachToComponent(
						Root,
						FAttachmentTransformRules::KeepRelativeTransform);
				}
			},
			[WheelRearRight, Body]()
			{
				if (WheelRearRight != nullptr && Body != nullptr)
				{
					WheelRearRight->AttachToComponent(
						Body,
						FAttachmentTransformRules::KeepRelativeTransform);
				}
			});
	}
	bAllInvalidFixturesRejected =
		NegativeAuditTests.Num() == 4
		&& NegativeAuditTests.ContainsByPredicate(
			[](const FNegativeHierarchyAuditTestResult& Result)
			{
				return !Result.Passed();
			}) == false;

	// 先开到约 40%，命令反向前后 Transform 必须完全连续，再回到闭合端点。
	Actuator->SetOpen(true);
	while (Actuator->GetProgress() < 0.4f && Actuator->IsMoving())
	{
		SampleStep(VehicleHierarchyProbe::FixedStepSeconds);
	}
	const float ReverseProgress = Actuator->GetProgress();
	const FTransform BeforeIdempotentCommand = ActuatedPart->GetRelativeTransform();
	Actuator->SetOpen(true);
	const FTransform AfterIdempotentCommand = ActuatedPart->GetRelativeTransform();
	MaxIdempotentCommandJump = FMath::Max(
		MaxIdempotentCommandJump,
		TransformError(BeforeIdempotentCommand, AfterIdempotentCommand));
	bSetOpenSameValuePassed =
		MaxIdempotentCommandJump <= VehicleHierarchyProbe::ErrorTolerance;

	const FTransform BeforeReverse = ActuatedPart->GetRelativeTransform();
	Actuator->SetOpen(false);
	const FTransform AfterReverse = ActuatedPart->GetRelativeTransform();
	MaxCommandJump = FMath::Max(
		MaxCommandJump,
		TransformError(BeforeReverse, AfterReverse));
	DriveTo(false);
	bReverseAtFortyPercentPassed =
		FMath::IsNearlyEqual(ReverseProgress, 0.4f, 0.021f)
		&& MaxCommandJump <= VehicleHierarchyProbe::ErrorTolerance
		&& MaxTerminalError <= VehicleHierarchyProbe::ErrorTolerance;

	// 一次完整开关验证两个端点均可达。
	DriveTo(true);
	DriveTo(false);
	bFullOpenClosePassed =
		MaxTerminalError <= VehicleHierarchyProbe::ErrorTolerance
		&& MaxInterpolationError <= VehicleHierarchyProbe::ErrorTolerance;
	bStepJumpPassed = MaxStepJump <= VehicleHierarchyProbe::StepJumpThreshold;

	// 连续三轮复用同一执行器，验证状态不会随重复操作漂移。
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		DriveTo(true);
		DriveTo(false);
		if (MaxTerminalError <= VehicleHierarchyProbe::ErrorTolerance)
		{
			++CompletedCycles;
		}
	}
	bThreeCyclesPassed = CompletedCycles == 3;

	const bool bSuccess =
		bCubeMeshLoaded
		&& bHierarchyPassed
		&& bAllInvalidFixturesRejected
		&& bStepJumpPassed
		&& bSetOpenSameValuePassed
		&& bReverseAtFortyPercentPassed
		&& bFullOpenClosePassed
		&& bThreeCyclesPassed;
	Finish(bSuccess, bSuccess ? FString() : TEXT("一个或多个 P0-4 自动检查失败。"));
}

void FVehicleHierarchyProbe::Finish(
	const bool bSuccess,
	const FString& FailureReason)
{
	const FVehicleHierarchyAuditResult Audit =
		UVehicleHierarchyAuditor::AuditActor(ProbeActor.Get());

	TArray<TSharedPtr<FJsonValue>> IssueValues;
	for (const FString& Issue : Audit.Issues)
	{
		IssueValues.Add(MakeShared<FJsonValueString>(Issue));
	}
	TSharedRef<FJsonObject> TagCounts = MakeShared<FJsonObject>();
	for (const TPair<FName, int32>& Pair : Audit.TaggedComponentCounts)
	{
		TagCounts->SetNumberField(Pair.Key.ToString(), Pair.Value);
	}
	TSharedRef<FJsonObject> Hierarchy = MakeShared<FJsonObject>();
	Hierarchy->SetBoolField(TEXT("passed"), Audit.bPassed);
	Hierarchy->SetObjectField(TEXT("taggedComponentCounts"), TagCounts);
	Hierarchy->SetArrayField(TEXT("issues"), IssueValues);

	TArray<TSharedPtr<FJsonValue>> RequiredTagValues;
	for (const FName Tag : UVehicleHierarchyAuditor::GetRequiredTags())
	{
		RequiredTagValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	TSharedRef<FJsonObject> TagSchema = MakeShared<FJsonObject>();
	TagSchema->SetStringField(TEXT("root"), TEXT("Vehicle.Root"));
	TagSchema->SetStringField(TEXT("body"), TEXT("Vehicle.Body"));
	TagSchema->SetStringField(TEXT("partPrefix"), TEXT("Vehicle.Part."));
	TagSchema->SetArrayField(TEXT("requiredTags"), RequiredTagValues);
	TagSchema->SetBoolField(TEXT("uniqueAndMovableRequired"), true);
	Hierarchy->SetObjectField(TEXT("tagSchema"), TagSchema);

	TSharedRef<FJsonObject> PivotChecks = MakeShared<FJsonObject>();
	PivotChecks->SetBoolField(
		TEXT("allPartPivotsDirectlyAttachedToBody"),
		Audit.bPartPivotsDirectlyAttachedToBody);
	PivotChecks->SetBoolField(TEXT("doorMeshIsDirectPivotChild"), Audit.bDoorMeshIsPivotChild);
	PivotChecks->SetBoolField(TEXT("doorMeshHasNoTags"), Audit.bDoorMeshHasNoTags);
	PivotChecks->SetBoolField(TEXT("doorMeshOffsetFromHingePivot"), Audit.bDoorMeshOffsetFromPivot);
	PivotChecks->SetBoolField(
		TEXT("actuatorTargetsDoorPivot"),
		ActuatedPart != nullptr
			&& ActuatedPart->GetFName() == TEXT("DoorPivot")
			&& Actuator != nullptr
			&& Actuator->TargetComponent == ActuatedPart);
	Hierarchy->SetObjectField(TEXT("pivotChecks"), PivotChecks);

	TSharedRef<FJsonObject> Tests = MakeShared<FJsonObject>();
	Tests->SetBoolField(TEXT("setOpenSameValueIsIdempotent"), bSetOpenSameValuePassed);
	Tests->SetNumberField(TEXT("maximumIdempotentCommandJump"), MaxIdempotentCommandJump);
	Tests->SetBoolField(TEXT("reverseAtApproximatelyFortyPercent"), bReverseAtFortyPercentPassed);
	Tests->SetBoolField(TEXT("fullOpenClose"), bFullOpenClosePassed);
	Tests->SetBoolField(TEXT("threeRepeatedCycles"), bThreeCyclesPassed);
	Tests->SetNumberField(TEXT("completedCycles"), CompletedCycles);
	Tests->SetNumberField(TEXT("maximumCommandJump"), MaxCommandJump);
	Tests->SetNumberField(TEXT("maximumStepJump"), MaxStepJump);
	Tests->SetNumberField(TEXT("maximumStepJumpThreshold"), VehicleHierarchyProbe::StepJumpThreshold);
	Tests->SetBoolField(TEXT("stepJumpWithinThreshold"), bStepJumpPassed);
	Tests->SetNumberField(TEXT("maximumInterpolationError"), MaxInterpolationError);
	Tests->SetNumberField(TEXT("maximumTerminalError"), MaxTerminalError);

	TArray<TSharedPtr<FJsonValue>> NegativeAuditValues;
	for (const FNegativeHierarchyAuditTestResult& Result : NegativeAuditTests)
	{
		TArray<TSharedPtr<FJsonValue>> NegativeIssueValues;
		for (const FString& Issue : Result.Issues)
		{
			NegativeIssueValues.Add(MakeShared<FJsonValueString>(Issue));
		}

		TSharedRef<FJsonObject> NegativeAudit = MakeShared<FJsonObject>();
		NegativeAudit->SetStringField(TEXT("name"), Result.Name);
		NegativeAudit->SetStringField(
			TEXT("expectedIssueSubstring"),
			Result.ExpectedIssueSubstring);
		NegativeAudit->SetBoolField(TEXT("auditPassed"), !Result.bAuditFailed);
		NegativeAudit->SetBoolField(TEXT("rejected"), Result.bAuditFailed);
		NegativeAudit->SetBoolField(TEXT("expectedIssueFound"), Result.bExpectedIssueFound);
		NegativeAudit->SetBoolField(TEXT("restoredAuditPassed"), Result.bRestoredAuditPassed);
		NegativeAudit->SetBoolField(TEXT("passed"), Result.Passed());
		NegativeAudit->SetArrayField(TEXT("issues"), NegativeIssueValues);
		NegativeAuditValues.Add(MakeShared<FJsonValueObject>(NegativeAudit));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 3);
	Root->SetStringField(TEXT("probe"), TEXT("VehicleHierarchyProbe"));
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetBoolField(TEXT("actualVehicleAssetPresent"), false);
	Root->SetStringField(
		TEXT("status"),
		bSuccess ? TEXT("PROTOTYPE_PASS_ASSET_PENDING") : TEXT("PROTOTYPE_FAIL_ASSET_PENDING"));
	Root->SetBoolField(TEXT("cubeHierarchyGenerated"), ProbeActor.IsValid() && bCubeMeshLoaded);
	Root->SetObjectField(TEXT("hierarchyAudit"), Hierarchy);
	Root->SetArrayField(TEXT("negativeAuditTests"), NegativeAuditValues);
	Root->SetBoolField(TEXT("allInvalidFixturesRejected"), bAllInvalidFixturesRejected);
	Root->SetObjectField(TEXT("actuatorTests"), Tests);
	Root->SetBoolField(TEXT("passed"), bSuccess);
	Root->SetStringField(TEXT("failureReason"), FailureReason);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bSerialized = FJsonSerializer::Serialize(Root, Writer);
	const bool bDirectoryReady = IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(OutputPath),
		true);
	const bool bWritten = bSerialized && bDirectoryReady
		&& FFileHelper::SaveStringToFile(
			JsonText,
			*OutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bWritten)
	{
		UE_LOG(LogVehicleHierarchyProbe, Display, TEXT("P0-4 探针 JSON 已写入：%s"), *OutputPath);
	}
	else
	{
		UE_LOG(LogVehicleHierarchyProbe, Error, TEXT("无法写入 P0-4 探针 JSON：%s"), *OutputPath);
	}

	FPlatformMisc::RequestExitWithStatus(false, bWritten && bSuccess ? 0 : 2);
}
