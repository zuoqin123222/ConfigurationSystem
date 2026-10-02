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
