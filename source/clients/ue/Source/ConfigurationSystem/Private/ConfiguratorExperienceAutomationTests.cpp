#if WITH_DEV_AUTOMATION_TESTS

#include "ConfiguratorExperienceSaveGame.h"
#include "ConfiguratorPanel.h"
#include "ConfigShowroomPlayerController.h"
#include "ReversiblePartActuatorComponent.h"
#include "SmoothWheelControllerComponent.h"

#include "Components/Image.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConfiguratorThumbnailCacheAutomationTest,
	"ConfigurationSystem.Runtime.ConfiguratorPanel.ThumbnailCache",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FConfiguratorThumbnailCacheAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FMapProperty* CacheProperty = FindFProperty<FMapProperty>(
		UConfiguratorPanel::StaticClass(),
		TEXT("VariantThumbnailCache"));
	TestNotNull(TEXT("ConfiguratorPanel 使用 UPROPERTY 持有缩略图缓存"), CacheProperty);
	if (CacheProperty != nullptr)
	{
		TestTrue(TEXT("缩略图缓存仅为瞬态生命周期"), CacheProperty->HasAnyPropertyFlags(CPF_Transient));
		const FObjectPropertyBase* ValueProperty =
			CastField<FObjectPropertyBase>(CacheProperty->ValueProp);
		TestTrue(
			TEXT("缩略图缓存值由反射系统作为 UTexture2D 强引用追踪"),
			ValueProperty != nullptr
				&& ValueProperty->PropertyClass == UTexture2D::StaticClass());
	}

	TStrongObjectPtr<UConfiguratorPanel> Panel(NewObject<UConfiguratorPanel>());
	Panel->bAcceptThumbnailResults = true;
	int32 DecodeCount = 0;
	Panel->VariantThumbnailLoaderOverride =
		[&DecodeCount](const FString& ThumbnailUrl) -> UTexture2D*
		{
			++DecodeCount;
			return ThumbnailUrl.Contains(TEXT("missing-"))
				? nullptr
				: NewObject<UTexture2D>();
		};
	const FString ThumbnailUrl(TEXT("/sc01/thumbnails/alcantara-p2-1045.webp"));
	TStrongObjectPtr<UImage> FirstImage(NewObject<UImage>());
	Panel->RequestVariantThumbnail(ThumbnailUrl, FirstImage.Get());
	TestEqual(TEXT("请求阶段不在游戏线程同步解码"), DecodeCount, 0);
	TestEqual(TEXT("请求阶段只登记一个待加载 URL"), Panel->PendingVariantThumbnailUrls.Num(), 1);
	Panel->PumpVariantThumbnailLoads();
	TestEqual(TEXT("分批泵送后执行一次加载"), DecodeCount, 1);
	TestEqual(TEXT("有效缩略图进入受 GC 追踪的缓存"), Panel->VariantThumbnailCache.Num(), 1);
	UTexture2D* FirstTexture = Cast<UTexture2D>(
		FirstImage->GetBrush().GetResourceObject());
	TestNotNull(TEXT("完成加载后真实纹理绑定到色卡 Image"), FirstTexture);

	TWeakObjectPtr<UTexture2D> TextureAfterGc(FirstTexture);
	FirstTexture = nullptr;
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("UPROPERTY 缓存在 GC 后仍持有纹理"), TextureAfterGc.IsValid());
	TStrongObjectPtr<UImage> SecondImage(NewObject<UImage>());
	Panel->RequestVariantThumbnail(ThumbnailUrl, SecondImage.Get());
	TestTrue(
		TEXT("同一 thumbnailUrl 刷新时复用同一纹理"),
		SecondImage->GetBrush().GetResourceObject() == TextureAfterGc.Get());
	TestEqual(TEXT("重复加载不增加缓存项"), Panel->VariantThumbnailCache.Num(), 1);
	TestEqual(TEXT("重复加载不再次解码"), DecodeCount, 1);

	const FInt32Range InitialRange =
		UConfiguratorPanel::CalculateThumbnailRequestRange(0.0f, 440.0f, 160);
	TestEqual(TEXT("首帧只预取横向视口附近条目"), InitialRange.GetLowerBoundValue(), 0);
	TestTrue(
		TEXT("首帧不会请求全部 160 张色卡"),
		InitialRange.GetUpperBoundValue() <= 9);
	const FInt32Range ScrolledRange =
		UConfiguratorPanel::CalculateThumbnailRequestRange(
			86.0f * 80.0f,
			440.0f,
			160);
	TestTrue(TEXT("横向滚动后请求窗口随偏移移动"), ScrolledRange.GetLowerBoundValue() >= 77);
	TestTrue(TEXT("滚动请求仍保持小批窗口"), ScrolledRange.GetUpperBoundValue() <= 89);

	for (int32 Index = 0;
		Index < UConfiguratorPanel::MaxVariantThumbnailCacheEntries + 8;
		++Index)
	{
		Panel->StoreVariantThumbnail(
			FString::Printf(TEXT("/sc01/thumbnails/cache-%d.webp"), Index),
			NewObject<UTexture2D>());
	}
	TestEqual(
		TEXT("成功纹理 LRU 缓存限制最大条目数"),
		Panel->VariantThumbnailCache.Num(),
		UConfiguratorPanel::MaxVariantThumbnailCacheEntries);
	TestEqual(
		TEXT("成功纹理 LRU 顺序表保持同样上限"),
		Panel->VariantThumbnailCacheOrder.Num(),
		UConfiguratorPanel::MaxVariantThumbnailCacheEntries);
	TestFalse(
		TEXT("最早且非可见的缓存纹理被淘汰"),
		Panel->VariantThumbnailCache.Contains(TEXT("/sc01/thumbnails/cache-0.webp")));

	for (int32 Index = 0;
		Index < UConfiguratorPanel::MaxFailedThumbnailCacheEntries + 8;
		++Index)
	{
		Panel->RememberFailedVariantThumbnail(FString::Printf(
			TEXT("/sc01/thumbnails/missing-%d.webp"), Index));
	}
	TestEqual(
		TEXT("失败 URL 缓存限制最大条目数"),
		Panel->FailedVariantThumbnailUrls.Num(),
		UConfiguratorPanel::MaxFailedThumbnailCacheEntries);
	TestEqual(
		TEXT("失败 URL 淘汰顺序与集合保持同样上限"),
		Panel->FailedVariantThumbnailOrder.Num(),
		UConfiguratorPanel::MaxFailedThumbnailCacheEntries);
	TStrongObjectPtr<UImage> MissingImage(NewObject<UImage>());
	const int32 DecodeCountBeforeCachedFailure = DecodeCount;
	Panel->RequestVariantThumbnail(
		FString::Printf(
			TEXT("/sc01/thumbnails/missing-%d.webp"),
			UConfiguratorPanel::MaxFailedThumbnailCacheEntries + 7),
		MissingImage.Get());
	Panel->PumpVariantThumbnailLoads();
	TestEqual(TEXT("缓存的失败 URL 不重复解码"), DecodeCount, DecodeCountBeforeCachedFailure);
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
	TestTrue(TEXT("轨迹起点稳定"),
		AConfigShowroomPlayerController::InterpolateOrbitLocation(
			Start, End, Pivot, 0.0f).Equals(Start, 0.1));
	TestTrue(TEXT("轨迹终点稳定"),
		AConfigShowroomPlayerController::InterpolateOrbitLocation(
			Start, End, Pivot, 1.0f).Equals(End, 0.1));

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
