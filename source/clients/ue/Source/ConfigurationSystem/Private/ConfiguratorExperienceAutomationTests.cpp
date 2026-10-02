#if WITH_DEV_AUTOMATION_TESTS

#include "ConfiguratorExperienceSaveGame.h"
#include "ReversiblePartActuatorComponent.h"
#include "SmoothWheelControllerComponent.h"

#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReversiblePartActuatorAutomationTest,
	"ConfigurationSystem.Runtime.Experience.ReversibleActuator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReversiblePartActuatorAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	USceneComponent* Target = NewObject<USceneComponent>();
	UReversiblePartActuatorComponent* Actuator =
		NewObject<UReversiblePartActuatorComponent>();
	Actuator->Duration = 1.0f;
	Actuator->BindPart(
		Target,
		FTransform::Identity,
		FTransform(FRotator(0.0, 90.0, 0.0), FVector(100.0, 0.0, 0.0)));

	Actuator->SetOpen(true);
	Actuator->AdvanceActuation(0.5f);
	const float ForwardProgress = Actuator->GetProgress();
	TestTrue(TEXT("半程进度在开闭区间内"), ForwardProgress > 0.0f && ForwardProgress < 1.0f);
	TestTrue(TEXT("半程 Transform 已被平滑驱动"), Target->GetRelativeLocation().X > 0.0);

	Actuator->Toggle();
	Actuator->AdvanceActuation(0.25f);
	TestTrue(TEXT("运动中反向沿当前进度连续回退"), Actuator->GetProgress() < ForwardProgress);
	TestFalse(TEXT("回到闭合端后不再移动前仍保持闭合目标"), Actuator->IsOpenRequested());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSmoothWheelControllerAutomationTest,
	"ConfigurationSystem.Runtime.Experience.SmoothWheels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothWheelControllerAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<USceneComponent*> Wheels;
	TArray<USceneComponent*> SpinPivots;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Wheels.Add(NewObject<USceneComponent>());
		USceneComponent* SpinPivot = NewObject<USceneComponent>();
		SpinPivot->SetRelativeRotation(FRotator(0.0, 0.0, 90.0));
		SpinPivots.Add(SpinPivot);
	}
	USmoothWheelControllerComponent* Controller =
		NewObject<USmoothWheelControllerComponent>();
	Controller->BindWheels(Wheels, SpinPivots);
	Controller->SetWheelTargets(30.0f, 180.0f);
	Controller->AdvanceWheels(0.1f);

	TestTrue(
		TEXT("转向按响应速度平滑逼近而非瞬移"),
		Controller->GetSmoothedSteeringDegrees() > 0.0f
			&& Controller->GetSmoothedSteeringDegrees() < 30.0f);
	TestTrue(TEXT("前轮转向 Pivot 获得偏转"), !Wheels[0]->GetRelativeRotation().IsZero());
	TestTrue(TEXT("后轮转向 Pivot 保持不偏转"), Wheels[2]->GetRelativeRotation().IsZero());
	TestTrue(TEXT("四轮独立 Spin Pivot 均绕已校正轮轴滚动"),
		!SpinPivots[0]->GetRelativeRotation().Equals(FRotator(0.0, 0.0, 90.0))
			&& !SpinPivots[3]->GetRelativeRotation().Equals(FRotator(0.0, 0.0, 90.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfiguratorPersistenceAutomationTest,
	"ConfigurationSystem.Runtime.Experience.AtomicPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConfiguratorPersistenceAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Slot(TEXT("AutomationExperience"));
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	UConfiguratorPersistenceSubsystem* Persistence =
		NewObject<UConfiguratorPersistenceSubsystem>(GameInstance);
	UConfiguratorExperienceSaveGame* Source =
		NewObject<UConfiguratorExperienceSaveGame>();
	Source->Configuration.Paint = TEXT("paint-silver");
	Source->Configuration.Wheel = TEXT("wheel-forged");
	Source->Configuration.Interior = TEXT("interior-ivory");
	Source->Configuration.Frame = TEXT("frame-black");
	Source->EnvironmentIndex = 1;
	Source->bPanelVisible = false;

	FString Error;
	TestTrue(TEXT("快照经临时文件原子替换写入"), Persistence->SaveAtomic(Slot, Source, Error));
	TestTrue(TEXT("写入后无残留临时文件"), !IFileManager::Get().FileExists(
		*(UConfiguratorPersistenceSubsystem::GetSlotFilename(Slot) + TEXT(".tmp"))));
	UConfiguratorExperienceSaveGame* Loaded = Persistence->Load(Slot, Error);
	TestNotNull(TEXT("可反序列化快照"), Loaded);
	if (Loaded != nullptr)
	{
		TestEqual(TEXT("恢复配置"), Loaded->Configuration.Paint, FString(TEXT("paint-silver")));
		TestEqual(TEXT("恢复环境"), Loaded->EnvironmentIndex, 1);
		TestFalse(TEXT("恢复 UI 显隐"), Loaded->bPanelVisible);
	}
	TestNull(TEXT("SaveGame 不包含瞬态镜头"), FindFProperty<FProperty>(
		UConfiguratorExperienceSaveGame::StaticClass(), TEXT("CameraIndex")));
	TestNull(TEXT("SaveGame 不包含瞬态 Path Tracing"), FindFProperty<FProperty>(
		UConfiguratorExperienceSaveGame::StaticClass(), TEXT("bPathTracingEnabled")));

	const FString FinalPath = UConfiguratorPersistenceSubsystem::GetSlotFilename(Slot);
	const FString TempPath = FinalPath + TEXT(".tmp");
	TArray<uint8> OriginalBytes;
	TestTrue(TEXT("读取旧正式存档作为回滚基线"),
		FFileHelper::LoadFileToArray(OriginalBytes, *FinalPath));
	IFileManager::Get().Delete(*TempPath, false, true);
	FString RollbackError;
	TestFalse(TEXT("temp 缺失时可靠替换必须失败"),
		UConfiguratorPersistenceSubsystem::ReplaceFileReliably(
			FinalPath, TempPath, RollbackError));
	TArray<uint8> RolledBackBytes;
	TestTrue(TEXT("替换失败后正式存档仍可读"),
		FFileHelper::LoadFileToArray(RolledBackBytes, *FinalPath));
	TestEqual(TEXT("替换失败后恢复旧正式存档"), RolledBackBytes, OriginalBytes);
	TestFalse(TEXT("回滚后不残留 backup"),
		IFileManager::Get().FileExists(*(FinalPath + TEXT(".bak"))));
	IFileManager::Get().Delete(
		*FinalPath, false, true);
	return true;
}

#endif
