#if WITH_DEV_AUTOMATION_TESTS

#include "CarConfiguratorSubsystem.h"
#include "ConfigShowroomGameMode.h"
#include "ConfigShowroomPlayerController.h"
#include "ConfiguratorPanel.h"
#include "ConfiguratorVehicleActor.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfiguratorAtomicStageAutomationTest,
	"ConfigurationSystem.Runtime.Configurator.AtomicStage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConfiguratorAtomicStageAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	int64 BasePriceMinor = 0;
	TArray<FCarConfigurationOption> Options;
	TArray<FCarConfigurationTemplate> Templates;
	FCarConfigurationSelection Defaults;
	UCarConfiguratorSubsystem::BuildMvpCatalog(
		BasePriceMinor,
		Options,
		Templates,
		Defaults);
	TestEqual(TEXT("基础价来自共享 MVP fixture"), BasePriceMinor, int64(30000000));
	TestEqual(TEXT("四分区各两个选项"), Options.Num(), 8);
	TestEqual(TEXT("两个模板"), Templates.Num(), 2);

	UCarConfigurationState* State = NewObject<UCarConfigurationState>();
	TestTrue(
		TEXT("现有 UCarConfigurationState 接受 Subsystem catalog"),
		State->Initialize(BasePriceMinor, Options, Templates, Defaults));
	TestEqual(
		TEXT("默认 canonical key"),
		State->GetCanonicalKey(),
		FString(TEXT("paint-red__wheel-sport__interior-dark__frame-black")));
	TestEqual(TEXT("默认总价"), State->GetTotalPrice(), int64(30000000));
	TestTrue(TEXT("豪华模板可原子应用"), State->ApplyTemplate(TEXT("luxury")));
	TestEqual(
		TEXT("豪华模板 canonical key"),
		State->GetCanonicalKey(),
		FString(TEXT("paint-silver__wheel-forged__interior-ivory__frame-black")));
	TestEqual(TEXT("豪华模板总价"), State->GetTotalPrice(), int64(32760000));
	FCarConfigurationSelection InvalidRestore = State->GetSelection();
	InvalidRestore.Wheel = TEXT("unknown-wheel");
	const FString KeyBeforeInvalidRestore = State->GetCanonicalKey();
	TestFalse(TEXT("Load 可先只校验无效完整选择"), State->CanApplySelection(InvalidRestore));
	TestEqual(TEXT("只校验阶段不修改状态"), State->GetCanonicalKey(), KeyBeforeInvalidRestore);
	TestFalse(TEXT("持久化恢复拒绝无效完整选择"), State->ApplySelection(InvalidRestore));
	TestEqual(
		TEXT("无效完整选择不会产生部分更新"),
		State->GetCanonicalKey(),
		KeyBeforeInvalidRestore);
	TestTrue(TEXT("有效完整选择可一次恢复"), State->ApplySelection(Defaults));

	const AConfiguratorVehicleActor* Vehicle =
		GetDefault<AConfiguratorVehicleActor>();
	TArray<FString> BindingErrors;
	TestTrue(
		TEXT("占位车四分区标签/槽稳定且明确标记临时资源"),
		Vehicle->HasStablePlaceholderBindings(BindingErrors));
	for (const FString& Error : BindingErrors)
	{
		AddError(Error);
	}

	TestEqual(TEXT("UMG 分区数量"), UConfiguratorPanel::PartCount, 4);
	TestEqual(TEXT("UMG 选项按钮数量"), UConfiguratorPanel::OptionButtonCount, 8);
	TestEqual(TEXT("UMG 模板按钮数量"), UConfiguratorPanel::TemplateButtonCount, 2);

	const AConfigShowroomGameMode* GameMode = GetDefault<AConfigShowroomGameMode>();
	TestTrue(
		TEXT("GameMode 使用展厅 PlayerController"),
		GameMode->PlayerControllerClass == AConfigShowroomPlayerController::StaticClass());
	TestNull(TEXT("展厅无需 Pawn"), GameMode->DefaultPawnClass);
	return true;
}

#endif
