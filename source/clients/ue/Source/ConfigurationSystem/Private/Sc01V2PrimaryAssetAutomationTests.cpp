#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Misc/AutomationTest.h"
#include "Sc01V2CatalogData.h"
#include "Sc01V2ConfigurationState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSc01V2PrimaryAssetLoadAutomationTest,
	"ConfigurationSystem.Runtime.SC01V2.PrimaryAssetLoadAndState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSc01V2PrimaryAssetLoadAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UAssetManager& AssetManager = UAssetManager::Get();
	const FPrimaryAssetId AssetId(
		USc01V2CatalogData::PrimaryAssetType,
		USc01V2CatalogData::DefaultAssetName);
	const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
	TestTrue(TEXT("AssetManager 扫描到 DA_SC01Catalog"), AssetPath.IsValid());
	if (!AssetPath.IsValid())
	{
		return false;
	}

	const TSharedPtr<FStreamableHandle> LoadHandle =
		AssetManager.LoadPrimaryAsset(AssetId);
	if (LoadHandle.IsValid())
	{
		LoadHandle->WaitUntilComplete();
	}
	USc01V2CatalogData* CatalogAsset =
		Cast<USc01V2CatalogData>(AssetManager.GetPrimaryAssetObject(AssetId));
	TestNotNull(TEXT("通过 Primary Asset ID 加载 catalog"), CatalogAsset);
	if (CatalogAsset == nullptr)
	{
		return false;
	}
	TestFalse(TEXT("catalog JSON 已内嵌"), CatalogAsset->CatalogJson.IsEmpty());

	USc01V2ConfigurationState* State =
		NewObject<USc01V2ConfigurationState>(GetTransientPackage());
	TestTrue(TEXT("从 Primary Asset 初始化 v2 状态"), State->Initialize(CatalogAsset));
	TestEqual(TEXT("默认状态仅包含 29 个显式标配项目"), State->GetSelections().Num(), 29);
	TestEqual(
		TEXT("车漆读取首个显式标配项"),
		State->GetSelections().FindRef(TEXT("exterior-body-cover")),
		FString(TEXT("body-cover-red")));
	TestFalse(
		TEXT("可选机舱盖板默认不选装"),
		State->GetSelections().Contains(TEXT("engine-bay-cover")));
	TestFalse(TEXT("初始化已派生 configurationId"), State->GetConfigurationId().IsEmpty());
	TestFalse(TEXT("初始化已派生 renderKey"), State->GetRenderKey().IsEmpty());

	int32 BroadcastCount = 0;
	State->OnChangedNative.AddLambda([&BroadcastCount]()
	{
		++BroadcastCount;
	});

	const TMap<FString, FString> Defaults = State->GetSelections();
	TestTrue(
		TEXT("无变化事务成功"),
		State->ApplyTransaction(Defaults, State->GetCustomizations()));
	TestEqual(TEXT("无变化事务不广播"), BroadcastCount, 0);

	TMap<FString, FString> Invalid = Defaults;
	Invalid.Remove(TEXT("seat-backrest"));
	const FString IdBeforeInvalid = State->GetConfigurationId();
	TestFalse(
		TEXT("非法事务被拒绝"),
		State->ApplyTransaction(Invalid, State->GetCustomizations()));
	TestEqual(TEXT("非法事务不广播"), BroadcastCount, 0);
	TestEqual(TEXT("非法事务不产生部分提交"), State->GetConfigurationId(), IdBeforeInvalid);

	TMap<FString, FString> PaintSelections = Defaults;
	PaintSelections[TEXT("exterior-body-cover")] = Sc01V2::CustomPaintOptionId;
	FSc01V2PaintCustomization Paint;
	Paint.ColorHex = TEXT("#336699");
	Paint.Metallic = 0.45;
	Paint.Roughness = 0.25;
	Paint.ClearCoat = 0.9;
	Paint.OrangePeel = 0.12;
	Paint.FlakeIntensity = 0.3;
	FSc01V2Customization CustomPaint;
	CustomPaint.Kind = ESc01V2CustomizationKind::Paint;
	CustomPaint.Paint = Paint;
	TMap<FString, FSc01V2Customization> Customizations;
	Customizations.Add(TEXT("exterior-body-cover"), CustomPaint);

	TestTrue(
		TEXT("选择与定制作为一个事务提交"),
		State->ApplyTransaction(PaintSelections, Customizations));
	TestEqual(TEXT("完整事务只广播一次"), BroadcastCount, 1);
	TestTrue(
		TEXT("事务重新派生 configurationId"),
		State->GetConfigurationId() != IdBeforeInvalid);
	TestEqual(
		TEXT("车漆颜色被规范为大写"),
		State->GetCustomizations()[TEXT("exterior-body-cover")].Paint.ColorHex,
		FString(TEXT("#336699")));
	TestTrue(
		TEXT("从 custom paint 切换普通 option 成功"),
		State->SelectOption(TEXT("exterior-body-cover"), TEXT("body-cover-red")));
	TestFalse(
		TEXT("切换 option 同事务清除不兼容定制"),
		State->GetCustomizations().Contains(TEXT("exterior-body-cover")));
	TestEqual(TEXT("切换 option 仅广播一次"), BroadcastCount, 2);

	TMap<FString, FString> InvalidForPreflight = State->GetSelections();
	InvalidForPreflight.Remove(TEXT("seat-backrest"));
	const FString IdBeforePreflight = State->GetConfigurationId();
	TestFalse(
		TEXT("只读事务预检拒绝非法状态"),
		State->CanApplyTransaction(InvalidForPreflight, State->GetCustomizations()));
	TestEqual(
		TEXT("只读事务预检不修改状态"),
		State->GetConfigurationId(),
		IdBeforePreflight);
	TestTrue(
		TEXT("可选项目可显式选择"),
		State->SelectOption(TEXT("pedal"), TEXT("pedal-racing")));
	TestTrue(
		TEXT("可选项目可恢复为不选装"),
		State->ClearOptionalSelection(TEXT("pedal")));
	TestFalse(
		TEXT("不允许清除必选项目"),
		State->ClearOptionalSelection(TEXT("seat-backrest")));
	return true;
}

#endif
