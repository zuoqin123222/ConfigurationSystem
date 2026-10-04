#if WITH_DEV_AUTOMATION_TESTS

#include "ConfiguratorExperienceSaveGame.h"
#include "ConfiguratorPanel.h"
#include "ConfiguratorWebBridge.h"
#include "ConfiguratorVehicleActor.h"
#include "ConfigShowroomPlayerController.h"
#include "PathTracingExperienceSubsystem.h"
#include "ReversiblePartActuatorComponent.h"
#include "SmoothWheelControllerComponent.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPathTracingWarmupPolicyAutomationTest,
	"ConfigurationSystem.Runtime.Experience.PathTracingWarmupPolicy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FPathTracingWarmupPolicyAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const uint64 OneGiB = 1024ull * 1024ull * 1024ull;
	TestFalse(
		TEXT("不足 6 GiB 独立显存时拒绝 Path Tracing"),
		FPathTracingWarmupPolicy::HasEnoughVideoMemory(6ull * OneGiB - 1ull));
	TestTrue(
		TEXT("6 GiB 边界允许 Path Tracing"),
		FPathTracingWarmupPolicy::HasEnoughVideoMemory(6ull * OneGiB));
	TestFalse(
		TEXT("NVIDIA 581.95 低于 UE 5.8 安全门槛"),
		FPathTracingWarmupPolicy::IsDriverSupported(
			TEXT("NVIDIA GeForce RTX 5080"), TEXT("581.95")));
	TestTrue(
		TEXT("NVIDIA 610.00 达到 UE 5.8 安全门槛"),
		FPathTracingWarmupPolicy::IsDriverSupported(
			TEXT("NVIDIA GeForce RTX 5080"), TEXT("610.00")));
	TestTrue(
		TEXT("非 NVIDIA 适配器不应用 NVIDIA 驱动门槛"),
		FPathTracingWarmupPolicy::IsDriverSupported(
			TEXT("AMD Radeon"), TEXT("31.0.0")));

	TestEqual(
		TEXT("渲染栅栏未完成时保持等待"),
		FPathTracingWarmupPolicy::AdvanceWaitState(
			EPathTracingWarmupState::WaitingForRenderFence,
			false,
			0,
			1.0),
		EPathTracingWarmupState::WaitingForRenderFence);
	TestEqual(
		TEXT("渲染栅栏完成后开始观察 PipelineStateCache"),
		FPathTracingWarmupPolicy::AdvanceWaitState(
			EPathTracingWarmupState::WaitingForRenderFence,
			true,
			0,
			1.0),
		EPathTracingWarmupState::WaitingForPipelineCache);
	TestEqual(
		TEXT("仍有活跃 PSO 任务时不允许切换"),
		FPathTracingWarmupPolicy::AdvanceWaitState(
			EPathTracingWarmupState::WaitingForPipelineCache,
			true,
			1,
			10.0),
		EPathTracingWarmupState::WaitingForPipelineCache);
	TestEqual(
		TEXT("活跃 PSO 任务清零后预热就绪"),
		FPathTracingWarmupPolicy::AdvanceWaitState(
			EPathTracingWarmupState::WaitingForPipelineCache,
			true,
			0,
			10.0),
		EPathTracingWarmupState::Ready);
	TestEqual(
		TEXT("达到 90 秒时进入失败态"),
		FPathTracingWarmupPolicy::AdvanceWaitState(
			EPathTracingWarmupState::WaitingForPipelineCache,
			true,
			1,
			FPathTracingWarmupPolicy::TimeoutSeconds),
		EPathTracingWarmupState::Failed);
	TestFalse(
		TEXT("未请求 Path Tracing 时预热完成仍保持 Lit"),
		FPathTracingWarmupPolicy::ShouldApplyPathTracing(
			EPathTracingWarmupState::Ready,
			false));
	TestTrue(
		TEXT("预热完成且请求仍有效时才切换 Path Tracing"),
		FPathTracingWarmupPolicy::ShouldApplyPathTracing(
			EPathTracingWarmupState::Ready,
			true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWebConfiguratorDirectionAutomationTest,
	"ConfigurationSystem.Runtime.ConfiguratorPanel.WebDirection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FWebConfiguratorDirectionAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Url = UConfiguratorPanel::GetConfiguredWebUrl();
	TestTrue(TEXT("网页选配 URL 使用 HTTP(S)"), Url.StartsWith(TEXT("http://"))
		|| Url.StartsWith(TEXT("https://")));
	TestTrue(TEXT("UE 入口包含 source=ue"), Url.Contains(TEXT("source=ue")));
	TestTrue(TEXT("UE 入口启用 embedded 视图"), Url.Contains(TEXT("view=embedded")));
	const FString ControlsUrl = UConfiguratorPanel::GetControlsWebUrl();
	TestTrue(TEXT("控制层使用独立 controls 视图"), ControlsUrl.Contains(TEXT("view=controls")));
	TestFalse(TEXT("控制层不加载选配 embedded 视图"), ControlsUrl.Contains(TEXT("view=embedded")));
	const FString HeaderUrl = UConfiguratorPanel::GetHeaderWebUrl();
	TestTrue(TEXT("顶部栏使用独立 header 视图"), HeaderUrl.Contains(TEXT("view=header")));
	TestFalse(TEXT("顶部栏不加载选配 embedded 视图"), HeaderUrl.Contains(TEXT("view=embedded")));
	TestEqual(
		TEXT("健康检查使用网页 URL 的同一 scheme 与 authority"),
		UConfiguratorPanel::BuildHealthUrl(
			TEXT("https://localhost:9443/configurator?source=ue#panel")),
		FString(TEXT("https://localhost:9443/health")));
	TestEqual(
		TEXT("默认网页入口映射到同 Endpoint 的 /health"),
		UConfiguratorPanel::BuildHealthUrl(Url),
		FString(TEXT("http://127.0.0.1:8080/health")));
	TestTrue(
		TEXT("非 HTTP(S) URL 不生成健康检查地址"),
		UConfiguratorPanel::BuildHealthUrl(TEXT("file:///index.html")).IsEmpty());

	const TArray<float> ExpectedBackoff = {1.0f, 2.0f, 4.0f, 8.0f};
	for (int32 CompletedAttempt = 1;
		CompletedAttempt < UConfiguratorPanel::MaxHealthProbeAttempts;
		++CompletedAttempt)
	{
		TestEqual(
			*FString::Printf(TEXT("第 %d 次失败后的退避"), CompletedAttempt),
			UConfiguratorPanel::GetHealthRetryDelaySeconds(CompletedAttempt),
			ExpectedBackoff[CompletedAttempt - 1]);
	}
	TestEqual(
		TEXT("最多五次后停止重试"),
		UConfiguratorPanel::GetHealthRetryDelaySeconds(
			UConfiguratorPanel::MaxHealthProbeAttempts),
		0.0f);
	TestEqual(
		TEXT("无已完成请求时不安排退避"),
		UConfiguratorPanel::GetHealthRetryDelaySeconds(0),
		0.0f);
	TestTrue(TEXT("bridge 接受六个固定镜头中的最后一个"),
		UConfiguratorWebBridge::IsSupportedCameraIndex(5));
	TestFalse(TEXT("bridge 拒绝越界镜头"),
		UConfiguratorWebBridge::IsSupportedCameraIndex(6));
	TestTrue(TEXT("bridge 接受户外灯光白名单"),
		UConfiguratorWebBridge::IsSupportedLightPreset(TEXT("outdoor")));
	TestFalse(TEXT("bridge 拒绝未知灯光预设"),
		UConfiguratorWebBridge::IsSupportedLightPreset(TEXT("night")));
	TestTrue(TEXT("bridge 接受光追渲染白名单"),
		UConfiguratorWebBridge::IsSupportedRenderMode(TEXT("path-tracing")));
	TestFalse(TEXT("bridge 拒绝任意渲染命令"),
		UConfiguratorWebBridge::IsSupportedRenderMode(TEXT("r.HighResScreenshot")));
	TestTrue(TEXT("bridge 接受 Web 画质极高白名单"),
		UConfiguratorWebBridge::IsSupportedQualityLevel(TEXT("epic")));
	TestFalse(TEXT("bridge 拒绝任意 scalability 命令"),
		UConfiguratorWebBridge::IsSupportedQualityLevel(TEXT("sg.ViewDistanceQuality 0")));
	TestTrue(TEXT("bridge 接受固定选配阶段"),
		UConfiguratorWebBridge::IsSupportedConfiguratorCategory(TEXT("interior")));
	TestFalse(TEXT("bridge 拒绝任意选配阶段"),
		UConfiguratorWebBridge::IsSupportedConfiguratorCategory(TEXT("admin")));
	TestTrue(TEXT("bridge 接受 Header 保存动作"),
		UConfiguratorWebBridge::IsSupportedConfiguratorHeaderAction(TEXT("save")));
	TestTrue(TEXT("bridge 接受 Header 分享动作"),
		UConfiguratorWebBridge::IsSupportedConfiguratorHeaderAction(TEXT("share")));
	TestFalse(TEXT("bridge 拒绝 Header 调试动作"),
		UConfiguratorWebBridge::IsSupportedConfiguratorHeaderAction(TEXT("debug")));
	FString HeaderStateError;
	TestTrue(
		TEXT("bridge 接受完整的 Header 白名单状态"),
		UConfiguratorPanel::IsValidConfiguratorHeaderStateJson(
			TEXT("{\"categoryId\":\"interior\","
				"\"referenceTotalMinor\":23940000,"
				"\"syncState\":\"saved\","
				"\"syncMessage\":\"已同步\","
				"\"dirty\":false,\"online\":true}"),
			HeaderStateError));
	TestFalse(
		TEXT("bridge 拒绝 Header 状态中的额外 debug 字段"),
		UConfiguratorPanel::IsValidConfiguratorHeaderStateJson(
			TEXT("{\"categoryId\":\"interior\","
				"\"referenceTotalMinor\":23940000,"
				"\"syncState\":\"saved\","
				"\"syncMessage\":\"已同步\","
				"\"dirty\":false,\"online\":true,\"debug\":true}"),
			HeaderStateError));
	TestFalse(
		TEXT("bridge 拒绝 Header 状态中的负总价"),
		UConfiguratorPanel::IsValidConfiguratorHeaderStateJson(
			TEXT("{\"categoryId\":\"interior\","
				"\"referenceTotalMinor\":-1,"
				"\"syncState\":\"saved\","
				"\"syncMessage\":\"已同步\","
				"\"dirty\":false,\"online\":true}"),
			HeaderStateError));

	TMap<FString, FString> Selections;
	TMap<FString, FSc01V2Customization> Customizations;
	FString ParseError;
	const FString ValidConfigurationJson = TEXT(
		"{\"schemaVersion\":\"2.0.0\","
		"\"selections\":{\"exterior-body-cover\":\"body-cover-custom\","
		"\"door-middle\":\"door-middle-leather\"},"
		"\"customizations\":{"
		"\"exterior-body-cover\":{\"colorHex\":\"#123456\","
		"\"metallic\":0.4,\"roughness\":0.2,\"clearCoat\":0.8,"
		"\"orangePeel\":0.1,\"flakeIntensity\":0.3},"
		"\"door-middle\":{\"materialVariantId\":\"leather-p10-1217\"}}}");
	TestTrue(
		TEXT("白名单 bridge 解析 v2 selection 与两类材质定制"),
		UConfiguratorPanel::ParseWebConfigurationJson(
			ValidConfigurationJson,
			Selections,
			Customizations,
			ParseError));
	TestEqual(
		TEXT("解析车身 option"),
		Selections.FindRef(TEXT("exterior-body-cover")),
		FString(TEXT("body-cover-custom")));
	TestEqual(
		TEXT("车漆颜色规范为大写"),
		Customizations.FindRef(TEXT("exterior-body-cover")).Paint.ColorHex,
		FString(TEXT("#123456")));
	TestEqual(
		TEXT("解析材质 variant"),
		Customizations.FindRef(TEXT("door-middle")).MaterialVariantId,
		FString(TEXT("leather-p10-1217")));

	TestFalse(
		TEXT("bridge 拒绝非白名单根字段"),
		UConfiguratorPanel::ParseWebConfigurationJson(
			TEXT("{\"schemaVersion\":\"2.0.0\",\"selections\":{},"
				"\"customizations\":{},\"setRenderMode\":\"path-tracing\"}"),
			Selections,
			Customizations,
			ParseError));
	TestFalse(
		TEXT("bridge 拒绝越界车漆参数"),
		UConfiguratorPanel::ParseWebConfigurationJson(
			TEXT("{\"schemaVersion\":\"2.0.0\",\"selections\":{},"
				"\"customizations\":{\"exterior-body-cover\":{"
				"\"colorHex\":\"#123456\",\"metallic\":1.1,\"roughness\":0.2,"
				"\"clearCoat\":0.8,\"orangePeel\":0.1,\"flakeIntensity\":0.3}}}"),
			Selections,
			Customizations,
			ParseError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfiguratorCameraOrbitAutomationTest,
	"ConfigurationSystem.Runtime.Experience.CameraOrbit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConfiguratorCameraOrbitAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FVector Pivot(0.0, 0.0, 100.0);
	const FVector Start(1000.0, 0.0, 200.0);
	const FVector End(0.0, 1000.0, 300.0);
	const FVector Mid = AConfigShowroomPlayerController::InterpolateOrbitLocation(
		Start, End, Pivot, 0.5f);

	TestTrue(TEXT("外部镜头中点保持绕车半径而非直线切角"),
		FMath::IsNearlyEqual(FVector2D(Mid.X, Mid.Y).Size(), 1000.0, 0.1));
	TestTrue(TEXT("外部镜头沿最短 90 度圆弧经过 45 度"),
		FMath::IsNearlyEqual(Mid.X, Mid.Y, 0.1));
	TestTrue(TEXT("外部镜头高度平滑插值"),
		FMath::IsNearlyEqual(Mid.Z, 250.0, 0.1));
	FMinimalViewInfo SidePOV;
	SidePOV.Location = FVector(0.0, 880.0, 250.0);
	SidePOV.Rotation = FRotator(-10.0, -90.0, 0.0);
	FMinimalViewInfo OppositeSidePOV;
	OppositeSidePOV.Location = FVector(0.0, -880.0, 250.0);
	OppositeSidePOV.Rotation = FRotator(-10.0, 90.0, 0.0);
	const FMinimalViewInfo OppositeMidPOV =
		AConfigShowroomPlayerController::InterpolateCameraPOV(
			SidePOV, OppositeSidePOV, Pivot, 0.5f);
	TestTrue(TEXT("对向外部机位中点仍沿半圆而非穿过车辆"),
		FVector2D(
			OppositeMidPOV.Location.X - Pivot.X,
			OppositeMidPOV.Location.Y - Pivot.Y).Size() > 870.0f);
	TestTrue(TEXT("侧面到右后过渡中点持续朝向车辆而非朝向圆弧外侧"),
		FVector::DotProduct(
			OppositeMidPOV.Rotation.Vector().GetSafeNormal(),
			(Pivot - OppositeMidPOV.Location).GetSafeNormal()) > 0.99f);
	TestTrue(TEXT("轨迹起点稳定"),
		AConfigShowroomPlayerController::InterpolateOrbitLocation(
			Start, End, Pivot, 0.0f).Equals(Start, 0.1));
	TestTrue(TEXT("轨迹终点稳定"),
		AConfigShowroomPlayerController::InterpolateOrbitLocation(
			Start, End, Pivot, 1.0f).Equals(End, 0.1));

	FMinimalViewInfo RevealTarget;
	RevealTarget.Location = Start;
	RevealTarget.Rotation = (Pivot - Start).Rotation();
	RevealTarget.FOV = 42.0f;
	const FMinimalViewInfo RevealStart =
		AConfigShowroomPlayerController::BuildRevealStartPOV(RevealTarget, Pivot);
	TestTrue(TEXT("首次 Reveal 从更远处开始拉近"),
		FVector::Distance(RevealStart.Location, Pivot)
			> FVector::Distance(RevealTarget.Location, Pivot));
	TestTrue(TEXT("首次 Reveal 起点绕开默认机位而非直线后退"),
		!FMath::IsNearlyEqual(
			(RevealStart.Location - Pivot).Rotation().Yaw,
			(RevealTarget.Location - Pivot).Rotation().Yaw,
			0.1f));
	TestTrue(TEXT("首次 Reveal 始终看向车辆 Pivot"),
		RevealStart.Rotation.Equals((Pivot - RevealStart.Location).Rotation(), 0.1f));
	FMinimalViewInfo OffAxisPOV = RevealTarget;
	OffAxisPOV.Rotation += FRotator(3.0f, -2.0f, 0.0f);
	const FVector ViewAlignedPivot =
		AConfigShowroomPlayerController::CalculateViewAlignedOrbitPivot(
			OffAxisPOV, Pivot);
	TestTrue(TEXT("交互轨道中心沿当前视线建立，首次旋转不会重构构图"),
		(ViewAlignedPivot - OffAxisPOV.Location).GetSafeNormal().Equals(
			OffAxisPOV.Rotation.Vector().GetSafeNormal(), 0.001f));

	const FVector Rotated =
		AConfigShowroomPlayerController::RotateExteriorCameraLocation(
			Start, Pivot, 90.0f, 10.0f);
	TestTrue(TEXT("车外旋转保持到车辆 Pivot 的三维半径"),
		FMath::IsNearlyEqual(
			FVector::Distance(Start, Pivot),
			FVector::Distance(Rotated, Pivot),
			0.1f));
	TestTrue(TEXT("车外旋转同时支持水平和俯仰"),
		!FMath::IsNearlyEqual(Rotated.Y, Start.Y)
			&& !FMath::IsNearlyEqual(Rotated.Z, Start.Z));
	const FRotator PanRotation(-10.0, 35.0, 0.0);
	const FVector PanDelta =
		AConfigShowroomPlayerController::CalculateOrbitPanDelta(
			PanRotation, 25.0f, -12.0f);
	const FVector PannedCamera = Start + PanDelta;
	const FVector PannedPivot = Pivot + PanDelta;
	TestTrue(TEXT("右键平移对相机与独立 OrbitPivot 应用同一位移"),
		(PannedCamera - PannedPivot).Equals(Start - Pivot, 0.01f));
	const FVector RotatedAfterPan =
		AConfigShowroomPlayerController::RotateExteriorCameraLocation(
			PannedCamera, PannedPivot, 15.0f, 0.0f);
	TestTrue(TEXT("平移后左键旋转仍围绕平移后的 Pivot，不回拽到车辆中心"),
		FMath::IsNearlyEqual(
			FVector::Distance(RotatedAfterPan, PannedPivot),
			FVector::Distance(PannedCamera, PannedPivot),
			0.1f));

	for (int32 From = 0; From < 4; ++From)
	{
		for (int32 To = 0; To < 4; ++To)
		{
			TestFalse(
				*FString::Printf(TEXT("车外预设 %d 到 %d 使用绕车插值"), From, To),
				AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(From, To));
		}
	}
	TestTrue(TEXT("车外到驾驶位使用黑屏"),
		AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(0, 4));
	TestTrue(TEXT("驾驶位到车外使用黑屏"),
		AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(4, 0));
	TestTrue(TEXT("驾驶位到副驾位也使用黑屏"),
		AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(4, 5));
	TestFalse(TEXT("重复选择同一车内机位不触发黑屏"),
		AConfigShowroomPlayerController::ShouldUseBlackCameraTransition(4, 4));
	TestTrue(TEXT("副驾位属于车内预设"),
		AConfigShowroomPlayerController::IsInteriorCameraPreset(5));
	TestTrue(TEXT("车外允许平移"),
		AConfigShowroomPlayerController::IsCameraPanAllowed(3));
	TestFalse(TEXT("驾驶位车内禁止平移"),
		AConfigShowroomPlayerController::IsCameraPanAllowed(4));
	TestFalse(TEXT("副驾位车内禁止平移"),
		AConfigShowroomPlayerController::IsCameraPanAllowed(5));
	TestTrue(TEXT("淡黑期间重选当前机位会取消待切换机位"),
		AConfigShowroomPlayerController::ShouldCancelPendingCameraTransition(0, 4, 0));
	TestFalse(TEXT("淡黑期间重选目标机位不会误判为取消"),
		AConfigShowroomPlayerController::ShouldCancelPendingCameraTransition(0, 4, 4));
	TestFalse(TEXT("没有待切换机位时不需要取消"),
		AConfigShowroomPlayerController::ShouldCancelPendingCameraTransition(
			0, INDEX_NONE, 0));
	TestTrue(TEXT("淡黑期间改选另一车外机位会替换旧目标"),
		AConfigShowroomPlayerController::ShouldReplacePendingCameraTransition(
			0, 4, 1));
	TestTrue(TEXT("淡黑期间改选另一车内机位也会替换旧目标"),
		AConfigShowroomPlayerController::ShouldReplacePendingCameraTransition(
			0, 4, 5));
	TestFalse(TEXT("没有待切换目标时无需替换"),
		AConfigShowroomPlayerController::ShouldReplacePendingCameraTransition(
			0, INDEX_NONE, 1));

	const FVector InteriorPreset(10.0, 20.0, 30.0);
	TestTrue(TEXT("车内推拉未超限时保留候选位置"),
		AConfigShowroomPlayerController::ClampInteriorCameraLocation(
			InteriorPreset,
			InteriorPreset + FVector(60.0, 0.0, 0.0),
			120.0f).Equals(InteriorPreset + FVector(60.0, 0.0, 0.0), 0.1));
	const FVector ClampedInterior =
		AConfigShowroomPlayerController::ClampInteriorCameraLocation(
			InteriorPreset,
			InteriorPreset + FVector(300.0, 400.0, 0.0),
			120.0f);
	TestTrue(TEXT("车内推拉始终以预设位置为基准限幅"),
		FMath::IsNearlyEqual(
			FVector::Distance(InteriorPreset, ClampedInterior),
			120.0f,
			0.1f));
	TestTrue(TEXT("负限幅按零距离处理"),
		AConfigShowroomPlayerController::ClampInteriorCameraLocation(
			InteriorPreset,
			InteriorPreset + FVector(1.0, 0.0, 0.0),
			-1.0f).Equals(InteriorPreset, 0.1));

	FMinimalViewInfo StartPOV;
	StartPOV.Location = Start;
	StartPOV.Rotation = FRotator(-5.0, 0.0, 0.0);
	StartPOV.FOV = 70.0f;
	FMinimalViewInfo EndPOV;
	EndPOV.Location = End;
	EndPOV.Rotation = FRotator(-12.0, 90.0, 2.0);
	EndPOV.FOV = 42.0f;
	EndPOV.DesiredFOV = 44.0f;
	EndPOV.FirstPersonFOV = 55.0f;
	EndPOV.FirstPersonScale = 0.75f;
	EndPOV.OrthoWidth = 768.0f;
	EndPOV.bAutoCalculateOrthoPlanes = false;
	EndPOV.AutoPlaneShift = 12.0f;
	EndPOV.bUpdateOrthoPlanes = true;
	EndPOV.bUseCameraHeightAsViewTarget = true;
	EndPOV.OrthoNearClipPlane = 4.0f;
	EndPOV.OrthoFarClipPlane = 4096.0f;
	EndPOV.PerspectiveNearClipPlane = 7.0f;
	EndPOV.AspectRatio = 2.39f;
	EndPOV.AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
	EndPOV.bConstrainAspectRatio = true;
	EndPOV.bUseFirstPersonParameters = true;
	EndPOV.bUseFieldOfViewForLOD = false;
	EndPOV.ProjectionMode = ECameraProjectionMode::Orthographic;
	EndPOV.PostProcessBlendWeight = 0.65f;
	EndPOV.OffCenterProjectionOffset = FVector2D(0.1, -0.2);
	EndPOV.PreviousViewTransform = FTransform(
		FRotator(1.0, 2.0, 3.0),
		FVector(4.0, 5.0, 6.0));
	EndPOV.ApplyOverscan(0.1f, true, true);
	const FMinimalViewInfo FinalPOV =
		AConfigShowroomPlayerController::InterpolateCameraPOV(
			StartPOV,
			EndPOV,
			Pivot,
			1.0f);
	TestTrue(TEXT("过渡完成位置与目标 POV 一致"), FinalPOV.Location.Equals(EndPOV.Location, 0.01));
	TestTrue(TEXT("过渡完成旋转与目标 POV 一致"), FinalPOV.Rotation.Equals(EndPOV.Rotation, 0.01));
	TestTrue(TEXT("过渡完成 FOV 与目标一致"), FMath::IsNearlyEqual(FinalPOV.FOV, EndPOV.FOV));
	const FMinimalViewInfo MidPOV =
		AConfigShowroomPlayerController::InterpolateCameraPOV(
			StartPOV, EndPOV, Pivot, 0.5f);
	TestTrue(TEXT("完整 POV 插值保留绕车轨迹"),
		FMath::IsNearlyEqual(
			FVector2D(MidPOV.Location.X, MidPOV.Location.Y).Size(),
			1000.0,
			0.1));
	TestTrue(TEXT("完整 POV 插值包含视场角"),
		FMath::IsNearlyEqual(
			MidPOV.FOV,
			FMath::Lerp(StartPOV.FOV, EndPOV.FOV, 0.5f),
			0.1f));
	AConfigRuntimeCameraActor* RuntimeCamera =
		NewObject<AConfigRuntimeCameraActor>(GetTransientPackage());
	RuntimeCamera->ApplyCameraPOV(FinalPOV);
	FMinimalViewInfo AppliedPOV;
	RuntimeCamera->CalcCamera(0.0f, AppliedPOV);
	TestTrue(TEXT("过渡视图目标完整保留 FMinimalViewInfo"), AppliedPOV.Equals(FinalPOV));
	TestTrue(TEXT("运行时相机 Actor 同步 POV 位置"),
		RuntimeCamera->GetActorLocation().Equals(FinalPOV.Location, 0.01));
	TestTrue(TEXT("运行时相机 Actor 同步 POV 旋转"),
		RuntimeCamera->GetActorRotation().Equals(FinalPOV.Rotation, 0.01));
	TestEqual(TEXT("过渡视图保留偏轴投影"), AppliedPOV.OffCenterProjectionOffset, EndPOV.OffCenterProjectionOffset);
	TestEqual(TEXT("过渡视图保留透视近裁剪面"), AppliedPOV.PerspectiveNearClipPlane, EndPOV.PerspectiveNearClipPlane);
	TestEqual(TEXT("过渡视图保留正交近裁剪面"), AppliedPOV.OrthoNearClipPlane, EndPOV.OrthoNearClipPlane);
	TestEqual(TEXT("过渡视图保留正交远裁剪面"), AppliedPOV.OrthoFarClipPlane, EndPOV.OrthoFarClipPlane);
	TestFalse(TEXT("过渡视图保留 FOV LOD 开关"), AppliedPOV.bUseFieldOfViewForLOD);
	return true;
}

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
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FSmoothWheelControllerAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TArray<USceneComponent*> Wheels;
	TArray<USceneComponent*> SpinPivots;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Wheels.Add(NewObject<USceneComponent>());
		USceneComponent* SpinPivot = NewObject<USceneComponent>();
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
	const FQuat SpinRotation = SpinPivots[0]->GetRelativeRotation().Quaternion();
	TestTrue(TEXT("Spin Pivot 滚动保持本地 Y 轮轴不变"),
		SpinRotation.RotateVector(FVector::YAxisVector).Equals(
			FVector::YAxisVector, 0.001));
	TestFalse(TEXT("Spin Pivot 确实绕本地 Y 推进而非保持零旋转"),
		SpinRotation.RotateVector(FVector::ForwardVector).Equals(
			FVector::ForwardVector, 0.001));
	TestTrue(TEXT("四轮独立 Spin Pivot 均发生滚动"),
		!SpinPivots[0]->GetRelativeRotation().IsZero()
			&& !SpinPivots[3]->GetRelativeRotation().IsZero());

	AConfiguratorVehicleActor* Vehicle =
		NewObject<AConfiguratorVehicleActor>(GetTransientPackage());
	Vehicle->SetWheelAnimationEnabled(true);
	TestTrue(TEXT("显式 Set 开启动画并返回真实状态"), Vehicle->IsWheelAnimationEnabled());
	Vehicle->SetWheelAnimationEnabled(false);
	TestFalse(TEXT("显式 Set 关闭动画并返回真实状态"), Vehicle->IsWheelAnimationEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVehiclePresentationHierarchyAutomationTest,
	"ConfigurationSystem.Runtime.Experience.VehiclePresentationHierarchy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVehiclePresentationHierarchyAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AConfiguratorVehicleActor* Vehicle =
		NewObject<AConfiguratorVehicleActor>(GetTransientPackage());
	const auto FindScene = [Vehicle](const TCHAR* Name)
	{
		return FindObjectFast<USceneComponent>(Vehicle, FName(Name));
	};
	const auto FindMesh = [Vehicle](const TCHAR* Name)
	{
		return FindObjectFast<UStaticMeshComponent>(Vehicle, FName(Name));
	};

	USceneComponent* ContentRoot = FindScene(TEXT("VehicleContentRoot"));
	TestNotNull(TEXT("车辆包含独立内容根"), ContentRoot);
	if (ContentRoot != nullptr)
	{
		const FVector Offset = ContentRoot->GetRelativeLocation();
		TestTrue(TEXT("内容根使整车 X 几何中心归零"),
			FMath::IsNearlyZero(
				Offset.X + (-358.6124268 + 109.1891403) * 0.5,
				0.01));
		TestTrue(TEXT("内容根把轮胎最低点抬到地面 Z=0"),
			FMath::IsNearlyZero(Offset.Z - 32.3401680, 0.01));
	}

	const TCHAR* WheelNames[] = {
		TEXT("WheelFrontLeft"), TEXT("WheelFrontRight"),
		TEXT("WheelRearLeft"), TEXT("WheelRearRight")
	};
	for (const TCHAR* WheelName : WheelNames)
	{
		USceneComponent* SteeringPivot = FindScene(
			*FString::Printf(TEXT("%sSteeringPivot"), WheelName));
		USceneComponent* SpinPivot = FindScene(
			*FString::Printf(TEXT("%sSpinPivot"), WheelName));
		UStaticMeshComponent* Rim = FindMesh(WheelName);
		UStaticMeshComponent* Tire = FindMesh(
			*FString::Printf(TEXT("%sTire"), WheelName));
		UStaticMeshComponent* Rotor = FindMesh(
			*FString::Printf(TEXT("%sRotor"), WheelName));
		UStaticMeshComponent* Caliper = FindMesh(
			*FString::Printf(TEXT("%sCaliper"), WheelName));
		TestTrue(TEXT("轮毂独立网格可加载"),
			Rim != nullptr && Rim->GetStaticMesh() != nullptr
				&& Rim->GetStaticMesh()->GetPathName().Contains(
					TEXT("/_ImportStaging/")));
		TestTrue(TEXT("轮胎独立网格可加载"),
			Tire != nullptr && Tire->GetStaticMesh() != nullptr);
		TestTrue(TEXT("制动盘独立网格可加载"),
			Rotor != nullptr && Rotor->GetStaticMesh() != nullptr);
		TestTrue(TEXT("卡钳独立网格可加载"),
			Caliper != nullptr && Caliper->GetStaticMesh() != nullptr);
		TestTrue(TEXT("轮毂挂在滚动 Pivot"),
			Rim != nullptr && Rim->GetAttachParent() == SpinPivot);
		TestTrue(TEXT("轮胎挂在滚动 Pivot"),
			Tire != nullptr && Tire->GetAttachParent() == SpinPivot);
		TestTrue(TEXT("制动盘随轮胎滚动"),
			Rotor != nullptr && Rotor->GetAttachParent() == SpinPivot);
		TestTrue(TEXT("卡钳只随 Steering Pivot，不随滚动"),
			Caliper != nullptr && Caliper->GetAttachParent() == SteeringPivot
				&& Caliper->GetAttachParent() != SpinPivot);
	}

	USceneComponent* LeftDoorPivot = FindScene(TEXT("LeftDoorHingePivot"));
	USceneComponent* RightDoorPivot = FindScene(TEXT("RightDoorHingePivot"));
	const TCHAR* MirrorNames[] = {
		TEXT("LeftDoorMirrorBase"), TEXT("LeftDoorMirrorBody"),
		TEXT("LeftDoorMirrorGlass"), TEXT("RightDoorMirrorBase"),
		TEXT("RightDoorMirrorBody"), TEXT("RightDoorMirrorGlass")
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(MirrorNames); ++Index)
	{
		UStaticMeshComponent* Mirror = FindMesh(MirrorNames[Index]);
		const USceneComponent* ExpectedDoor =
			Index < 3 ? LeftDoorPivot : RightDoorPivot;
		TestTrue(
			*FString::Printf(TEXT("%s 随对应车门"), MirrorNames[Index]),
			Mirror != nullptr && Mirror->GetAttachParent() == ExpectedDoor);
	}

	const FVector OpenHoodPoint =
		AConfiguratorVehicleActor::GetHoodOpenRotation().RotateVector(
			FVector(135.0, 0.0, 0.0));
	const FVector OpenTrunkPoint =
		AConfiguratorVehicleActor::GetTrunkOpenRotation().RotateVector(
			FVector(-105.0, 0.0, 0.0));
	TestTrue(TEXT("机盖自由端开启时向上"), OpenHoodPoint.Z > 0.0);
	TestTrue(TEXT("后盖自由端开启时向上"), OpenTrunkPoint.Z > 0.0);

	UStaticMeshComponent* Trunk = FindMesh(TEXT("Trunk"));
	TestTrue(TEXT("后盖仅引用语义 TrunkMesh，不携带敞篷收纳机构"),
		Trunk != nullptr && Trunk->GetStaticMesh() != nullptr
			&& Trunk->GetStaticMesh()->GetPathName().Contains(
				TEXT("/_ImportStaging/"))
			&& Trunk->GetStaticMesh()->GetFName() == TEXT("TrunkMesh"));

	TArray<FString> Errors;
	TestTrue(TEXT("车辆展示层级审计通过"), Vehicle->HasStablePlaceholderBindings(Errors));
	for (const FString& Error : Errors)
	{
		AddError(Error);
	}
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
	Source->bHasSc01V2State = true;
	Source->Sc01V2Selections.Add(
		TEXT("exterior-body-cover"),
		TEXT("body-cover-custom"));
	FSc01V2Customization PaintCustomization;
	PaintCustomization.Kind = ESc01V2CustomizationKind::Paint;
	PaintCustomization.Paint.ColorHex = TEXT("#336699");
	PaintCustomization.Paint.Metallic = 0.45;
	PaintCustomization.Paint.Roughness = 0.25;
	PaintCustomization.Paint.ClearCoat = 0.9;
	PaintCustomization.Paint.OrangePeel = 0.12;
	PaintCustomization.Paint.FlakeIntensity = 0.3;
	Source->Sc01V2Customizations.Add(
		TEXT("exterior-body-cover"),
		PaintCustomization);

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
		TestTrue(TEXT("恢复 SC01 v2 状态标记"), Loaded->bHasSc01V2State);
		TestEqual(
			TEXT("恢复 SC01 v2 selection"),
			Loaded->Sc01V2Selections.FindRef(TEXT("exterior-body-cover")),
			FString(TEXT("body-cover-custom")));
		TestEqual(
			TEXT("恢复 SC01 v2 custom paint"),
			Loaded->Sc01V2Customizations.FindRef(
				TEXT("exterior-body-cover")).Paint.ColorHex,
			FString(TEXT("#336699")));
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
