#if WITH_DEV_AUTOMATION_TESTS

#include "ConfigurationBatchBake.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigurationBatchBakePlanTest,
	"ConfigurationSystem.Runtime.BatchBake.TaskCompleteness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigurationBatchBakeCameraTest,
	"ConfigurationSystem.Runtime.BatchBake.CameraUniqueness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigurationBatchBakeSha256Test,
	"ConfigurationSystem.Runtime.BatchBake.Sha256",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigurationBatchBakeOutputProfileTest,
	"ConfigurationSystem.Runtime.BatchBake.OutputProfiles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigurationBatchBakeDynamicCountTest,
	"ConfigurationSystem.Runtime.BatchBake.DynamicTaskCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigurationBatchBakeV2PlanTest,
	"ConfigurationSystem.Runtime.BatchBake.V2Plan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfigurationBatchBakeShaderWaitTrackerTest,
	"ConfigurationSystem.Runtime.BatchBake.ShaderWaitUsesIndependentTimeout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

namespace ConfigurationBatchBakeAutomation
{
	FString FixturePath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(),
			TEXT("../../../contracts/fixtures/published-configurations.mvp.json")));
	}

	bool LoadPlan(FAutomationTestBase& Test, FConfigurationBakePlan& OutPlan)
	{
		TArray<FString> Errors;
		const bool bLoaded = FConfigurationBakePlan::Load(FixturePath(), OutPlan, Errors);
		Test.TestTrue(
			*FString::Printf(TEXT("16 配置 fixture 可生成计划：%s"), *FString::Join(Errors, TEXT(" "))),
			bLoaded);
		return bLoaded;
	}
}

bool FConfigurationBatchBakePlanTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FConfigurationBakePlan Plan;
	if (!ConfigurationBatchBakeAutomation::LoadPlan(*this, Plan))
	{
		return false;
	}

	TestEqual(TEXT("发布配置声明 64 个渲染"), Plan.ExpectedRenderCount, 64);
	TestEqual(TEXT("生成 64 个任务"), Plan.Tasks.Num(), 64);
	TMap<FString, TSet<FString>> ViewsByConfiguration;
	TSet<FString> RelativePaths;
	for (const FConfigurationBakeTask& Task : Plan.Tasks)
	{
		ViewsByConfiguration.FindOrAdd(Task.ConfigurationKey).Add(Task.RenderViewId);
		RelativePaths.Add(Task.RelativePath);
		TestTrue(
			TEXT("输出固定在 publication/vehicle/configuration/view.png"),
			Task.RelativePath.StartsWith(TEXT("renders/mvp-v1/demo-car/"))
				&& Task.RelativePath.EndsWith(Task.RenderViewId + TEXT(".png")));
	}
	TestEqual(TEXT("配置数量固定为 16"), ViewsByConfiguration.Num(), 16);
	TestEqual(TEXT("64 个输出路径互不覆盖"), RelativePaths.Num(), 64);
	for (const TPair<FString, TSet<FString>>& Pair : ViewsByConfiguration)
	{
		TestEqual(*FString::Printf(TEXT("%s 包含四个视角"), *Pair.Key), Pair.Value.Num(), 4);
	}
	return true;
}

bool FConfigurationBatchBakeCameraTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FConfigurationBakePlan Plan;
	if (!ConfigurationBatchBakeAutomation::LoadPlan(*this, Plan))
	{
		return false;
	}

	TSet<FString> ViewIds;
	TSet<FName> Tags;
	TSet<FString> Transforms;
	for (const FConfigurationBakeCamera& Camera : Plan.Cameras)
	{
		ViewIds.Add(Camera.RenderViewId);
		Tags.Add(Camera.ActorTag);
		Transforms.Add(Camera.Transform.ToHumanReadableString());
		TestEqual(
			*FString::Printf(TEXT("%s 使用独立 RenderView 标签"), *Camera.RenderViewId),
			Camera.ActorTag,
			FName(*FString::Printf(TEXT("RenderView.%s"), *Camera.RenderViewId)));
	}
	TestEqual(TEXT("四个 RenderView 定义完整"), ViewIds.Num(), 4);
	TestEqual(TEXT("四个相机标签唯一"), Tags.Num(), 4);
	TestEqual(TEXT("四个相机 Transform 唯一"), Transforms.Num(), 4);
	return true;
}

bool FConfigurationBatchBakeOutputProfileTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FConfigurationBakeOutputSettings Settings;
	FString Error;
	TestTrue(TEXT("Debug 档位可解析"),
		FConfigurationBakeOutputSettings::Resolve(TEXT("debug"), false, Settings, Error));
	TestEqual(TEXT("Debug 输出宽度"), Settings.Width, 1022);
	TestEqual(TEXT("Debug 输出高度"), Settings.Height, 664);
	TestEqual(TEXT("Debug 样本数"), Settings.SamplesPerPixel, 64);
	TestTrue(TEXT("Debug 保持桌面左舞台比例"), Settings.HasDesktopStageAspectRatio());

	TestTrue(TEXT("Shipping 档位可解析"),
		FConfigurationBakeOutputSettings::Resolve(TEXT("shipping"), false, Settings, Error));
	TestEqual(TEXT("Shipping 输出适配 2K 左舞台"), Settings.Width, 2044);
	TestEqual(TEXT("Shipping 输出高度"), Settings.Height, 1328);
	TestEqual(TEXT("Shipping 使用高采样"), Settings.SamplesPerPixel, 512);
	TestTrue(TEXT("Shipping 默认启用 Path Tracing"), Settings.bPathTracing);
	TestTrue(TEXT("Shipping 默认启用降噪"), Settings.bDenoiser);
	TestTrue(TEXT("Shipping 保持桌面左舞台比例"), Settings.HasDesktopStageAspectRatio());

	TestTrue(TEXT("Shipping 构建默认选择 Shipping 档位"),
		FConfigurationBakeOutputSettings::Resolve(TEXT(""), true, Settings, Error));
	TestEqual(TEXT("默认 Shipping 输出宽度"), Settings.Width, 2044);
	TestFalse(TEXT("未知档位被拒绝"),
		FConfigurationBakeOutputSettings::Resolve(TEXT("cinema"), false, Settings, Error));
	TestFalse(TEXT("未知档位提供错误"), Error.IsEmpty());
	return true;
}

bool FConfigurationBatchBakeShaderWaitTrackerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FConfigurationBakeShaderWaitTracker Tracker;
	constexpr double TimeoutSeconds = 120.0;

	TestEqual(
		TEXT("整批运行很久后首次出现 Shader 编译仍从当前时刻开始等待"),
		Tracker.Update(true, 3600.0, TimeoutSeconds),
		EConfigurationBakeShaderWaitResult::Waiting);
	TestEqual(
		TEXT("本次 Shader 编译未超过独立窗口时继续等待"),
		Tracker.Update(true, 3719.0, TimeoutSeconds),
		EConfigurationBakeShaderWaitResult::Waiting);
	TestEqual(
		TEXT("本次 Shader 编译结束会报告完成，以便重置任务采样计时"),
		Tracker.Update(false, 3720.0, TimeoutSeconds),
		EConfigurationBakeShaderWaitResult::Completed);
	TestEqual(
		TEXT("完成状态只报告一次"),
		Tracker.Update(false, 3721.0, TimeoutSeconds),
		EConfigurationBakeShaderWaitResult::NotCompiling);

	TestEqual(
		TEXT("下一批 Shader 使用新的独立等待窗口"),
		Tracker.Update(true, 7200.0, TimeoutSeconds),
		EConfigurationBakeShaderWaitResult::Waiting);
	TestEqual(
		TEXT("只有单次 Shader 编译超过窗口才超时"),
		Tracker.Update(true, 7321.0, TimeoutSeconds),
		EConfigurationBakeShaderWaitResult::TimedOut);
	return true;
}

bool FConfigurationBatchBakeDynamicCountTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FConfigurationBakePlan Plan;
	Plan.SchemaVersion = TEXT("1.0.0");
	Plan.ExpectedRenderCount = 4;
	Plan.RenderViewIds = {TEXT("front"), TEXT("front-left"), TEXT("side"), TEXT("rear-right")};
	const FVector Locations[] = {
		FVector(1.0, 0.0, 0.0),
		FVector(0.0, 1.0, 0.0),
		FVector(-1.0, 0.0, 0.0),
		FVector(0.0, -1.0, 0.0)
	};
	for (int32 Index = 0; Index < Plan.RenderViewIds.Num(); ++Index)
	{
		FConfigurationBakeCamera& Camera = Plan.Cameras.AddDefaulted_GetRef();
		Camera.RenderViewId = Plan.RenderViewIds[Index];
		Camera.ActorTag = FName(*FString::Printf(TEXT("RenderView.%s"), *Camera.RenderViewId));
		Camera.Transform = FTransform(FRotator::ZeroRotator, Locations[Index]);
		FConfigurationBakeTask& Task = Plan.Tasks.AddDefaulted_GetRef();
		Task.ConfigurationKey = TEXT("paint-red__wheel-a__interior-dark__frame-black");
		Task.RenderViewId = Camera.RenderViewId;
		Task.Selections.Add(TEXT("paint"), TEXT("paint-red"));
	}
	TArray<FString> Errors;
	TestTrue(
		*FString::Printf(TEXT("单配置完整视角不受历史 64 项限制：%s"),
			*FString::Join(Errors, TEXT(" "))),
		Plan.Validate(Errors));
	Plan.ExpectedRenderCount = 64;
	TestFalse(TEXT("声明数量与动态任务数不一致时拒绝"), Plan.Validate(Errors));
	return true;
}

bool FConfigurationBatchBakeV2PlanTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("BatchBakeTests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);
	const FString PlanPath = FPaths::Combine(TestDirectory, TEXT("v2-plan.json"));
	const FString RenderKey =
		TEXT("sc01__sc01-draft-20261007__render-0123456789abcdef01234567");
	const FString Json = FString::Printf(TEXT(R"JSON({
		"schemaVersion":"2.0.0",
		"publicationVersion":"sc01-v2",
		"catalogVersion":"sc01-draft-20261007",
		"vehicleId":"sc01",
		"renderViewIds":["front","front-left","side","rear-right"],
		"expectedRenderCount":4,
		"configurations":[{
			"configurationKey":"%s",
			"configurationId":"cfg-0123456789abcdef01234567",
			"renderKey":"%s",
			"selections":{
				"exterior-body-cover":"body-cover-red",
				"door-middle":"door-middle-leather"
			},
			"customizations":{
				"door-middle":{"materialVariantId":"leather-p10-1217"},
				"exterior-body-cover":{
					"colorHex":"#112233",
					"metallic":0.8,
					"roughness":0.2,
					"clearCoat":0.9,
					"orangePeel":0.1,
					"flakeIntensity":0.7
				}
			}
		}]
	})JSON"), *RenderKey, *RenderKey);
	TestTrue(TEXT("写入 v2 published plan"), FFileHelper::SaveStringToFile(Json, *PlanPath));

	FConfigurationBakePlan Plan;
	TArray<FString> Errors;
	TestTrue(
		*FString::Printf(TEXT("FConfigurationBakePlan::Load 可读取 v2：%s"),
			*FString::Join(Errors, TEXT(" "))),
		FConfigurationBakePlan::Load(PlanPath, Plan, Errors));
	TestEqual(TEXT("v2 生成四个视角任务"), Plan.Tasks.Num(), 4);
	if (!Plan.Tasks.IsEmpty())
	{
		const FConfigurationBakeTask& Task = Plan.Tasks[0];
		TestEqual(TEXT("configurationKey 保持 renderKey"), Task.ConfigurationKey, RenderKey);
		TestEqual(TEXT("通用 selections 被读取"), Task.Selections.Num(), 2);
		TestEqual(TEXT("通用 customizations 被读取"), Task.Customizations.Num(), 2);
		TestEqual(
			TEXT("materialVariant customization 被读取"),
			Task.Customizations.FindChecked(TEXT("door-middle")).MaterialVariantId,
			FString(TEXT("leather-p10-1217")));
		TestEqual(
			TEXT("paint customization 被读取"),
			Task.Customizations.FindChecked(TEXT("exterior-body-cover")).Paint.ColorHex,
			FString(TEXT("#112233")));
	}
	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
	return true;
}

bool FConfigurationBatchBakeSha256Test::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(),
		TEXT("BatchBakeTests"),
		FGuid::NewGuid().ToString(EGuidFormats::Digits));
	IFileManager::Get().MakeDirectory(*TestDirectory, true);

	struct FSha256Vector
	{
		const TCHAR* Name;
		TArray<uint8> Bytes;
		const TCHAR* Expected;
	};

	TArray<uint8> MillionAs;
	MillionAs.Init(static_cast<uint8>('a'), 1000000);
	const TArray<FSha256Vector> Vectors{
		{
			TEXT("空文件"),
			{},
			TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
		},
		{
			TEXT("abc"),
			{static_cast<uint8>('a'), static_cast<uint8>('b'), static_cast<uint8>('c')},
			TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
		},
		{
			TEXT("百万个 a"),
			MoveTemp(MillionAs),
			TEXT("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")
		}
	};

	for (const FSha256Vector& Vector : Vectors)
	{
		const FString FilePath = FPaths::Combine(TestDirectory, Vector.Name + FString(TEXT(".bin")));
		TestTrue(
			*FString::Printf(TEXT("写入 SHA-256 标准向量：%s"), Vector.Name),
			FFileHelper::SaveArrayToFile(Vector.Bytes, *FilePath));

		FString Actual;
		FString Error;
		TestTrue(
			*FString::Printf(TEXT("计算 SHA-256 标准向量：%s"), Vector.Name),
			FConfigurationBatchBake::ComputeFileSha256(FilePath, Actual, Error));
		TestEqual(
			*FString::Printf(TEXT("SHA-256 与标准文件哈希一致：%s"), Vector.Name),
			Actual,
			FString(Vector.Expected));
	}

	FString MissingHash = TEXT("stale");
	FString MissingError;
	TestFalse(
		TEXT("不存在的文件返回失败"),
		FConfigurationBatchBake::ComputeFileSha256(
			FPaths::Combine(TestDirectory, TEXT("missing.png")),
			MissingHash,
			MissingError));
	TestTrue(TEXT("失败时清空哈希"), MissingHash.IsEmpty());
	TestFalse(TEXT("失败时提供错误"), MissingError.IsEmpty());

	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
	return true;
}

#endif
