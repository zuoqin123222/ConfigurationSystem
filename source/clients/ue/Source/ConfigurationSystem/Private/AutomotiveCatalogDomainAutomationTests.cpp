#if WITH_DEV_AUTOMATION_TESTS

#include "AutomotiveCatalogDomain.h"

#include "Algo/Reverse.h"
#include "Algo/AllOf.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace AutomotiveCatalogAutomation
{
	FString ContractFile(const TCHAR* RelativePath)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("../../../contracts"), RelativePath));
	}

	bool ReadJsonObject(
		const FString& Filename,
		TSharedPtr<FJsonObject>& OutObject)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Filename))
		{
			return false;
		}
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), OutObject)
			&& OutObject.IsValid();
	}

	bool ReadSelections(
		const TSharedPtr<FJsonObject>& Object,
		AutomotiveCatalog::FSelections& OutSelections)
	{
		const TSharedPtr<FJsonObject>* Selections = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(TEXT("selections"), Selections))
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Selections)->Values)
		{
			FString OptionId;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetString(OptionId))
			{
				return false;
			}
			OutSelections.Add(Pair.Key, MoveTemp(OptionId));
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveCatalogGoldenVectorsAutomationTest,
	"ConfigurationSystem.Runtime.AutomotiveCatalog.GoldenVectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveCatalogGoldenVectorsAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AutomotiveCatalog::FError Error;
	AutomotiveCatalog::FCatalogIndex Catalog;
	TestTrue(
		TEXT("加载并校验车型目录 v2"),
		Catalog.LoadJsonFile(
			AutomotiveCatalogAutomation::ContractFile(TEXT("fixtures/sc01.catalog.draft.v2.json")),
			Error));
	if (!Catalog.IsValid())
	{
		AddError(Error.Code + TEXT(": ") + Error.Message);
		return false;
	}
	TestEqual(TEXT("必选表面数"), Catalog.GetCatalog().SelectionOrder.Num(), 40);
	TestEqual(TEXT("选项索引覆盖 catalog"), Catalog.GetCatalog().Options.Num(), 175);
	TestEqual(TEXT("色卡索引覆盖 catalog"), Catalog.GetCatalog().MaterialVariants.Num(), 352);
	TestEqual(TEXT("车辆 displayName"), Catalog.GetCatalog().VehicleDisplayName, FString(TEXT("SC01")));
	TestEqual(TEXT("基础价读取为 22980000 分"), Catalog.GetCatalog().BasePriceMinor, int64(22980000));
	TestEqual(TEXT("region 数"), Catalog.GetCatalog().Regions.Num(), 4);
	TestEqual(TEXT("四阶段 category 数"), Catalog.GetCatalog().Categories.Num(), 4);
	TestEqual(TEXT("component 数"), Catalog.GetCatalog().Components.Num(), 17);
	TestEqual(TEXT("顶层动画定义数"), Catalog.GetCatalog().Animations.Num(), 5);
	TestEqual(TEXT("骨骼网格由顶层 Catalog 提供"),
		Catalog.GetCatalog().SkeletalMeshPath,
		FString(TEXT("/Game/Configurator/_ImportStaging/a5-dcc-v4-paint-seat-zup/"
			"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup."
			"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup")));
	TestEqual(TEXT("所有帧段共享顶层完整 AnimSequence"),
		Catalog.GetCatalog().SequencePath,
		FString(TEXT("/Game/Configurator/_ImportStaging/a5-dcc-v4-paint-seat-zup/"
			"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup_Anim."
			"automotive-configurator-audi-a5-dcc-v4-paint-seat-zup_Anim")));
	const AutomotiveCatalog::FAnimation* HoodAnimation =
		Catalog.FindAnimation(TEXT("hood"));
	TestNotNull(TEXT("可按 animationId 查询动画"), HoodAnimation);
	if (HoodAnimation != nullptr)
	{
		TestEqual(TEXT("机盖使用完整序列首个帧段"), HoodAnimation->StartFrame, 0);
		TestEqual(TEXT("机盖帧段末帧"), HoodAnimation->EndFrame, 30);
		TestEqual(TEXT("机盖关闭行为为反向播放"),
			HoodAnimation->CloseMode, FString(TEXT("reverse")));
	}
	const AutomotiveCatalog::FComponent* Chassis =
		Catalog.FindComponent(TEXT("chassis"));
	TestTrue(TEXT("chassis 节点联动 hood 动画"),
		Chassis != nullptr && Chassis->AnimationId.IsSet()
			&& Chassis->AnimationId.GetValue() == TEXT("hood"));
	AutomotiveCatalog::FCatalog InvalidSequenceCatalog = Catalog.GetCatalog();
	InvalidSequenceCatalog.SequencePath = TEXT("Invalid/FullSequence");
	AutomotiveCatalog::FCatalogIndex InvalidSequenceIndex;
	TestFalse(TEXT("拒绝非 /Game/ 顶层完整序列路径"),
		InvalidSequenceIndex.Initialize(InvalidSequenceCatalog, Error));
	AutomotiveCatalog::FCatalog IncompleteMeshPathCatalog = Catalog.GetCatalog();
	IncompleteMeshPathCatalog.SkeletalMeshPath = TEXT("/Game/Vehicle/SK_Car");
	AutomotiveCatalog::FCatalogIndex IncompleteMeshPathIndex;
	TestFalse(TEXT("拒绝缺少对象名的骨骼网格包路径"),
		IncompleteMeshPathIndex.Initialize(IncompleteMeshPathCatalog, Error));
	AutomotiveCatalog::FCatalog IncompleteSequencePathCatalog = Catalog.GetCatalog();
	IncompleteSequencePathCatalog.SequencePath = TEXT("/Game/Vehicle/A_FullVehicle");
	AutomotiveCatalog::FCatalogIndex IncompleteSequencePathIndex;
	TestFalse(TEXT("拒绝缺少对象名的动画序列包路径"),
		IncompleteSequencePathIndex.Initialize(IncompleteSequencePathCatalog, Error));
	AutomotiveCatalog::FCatalog InvalidFrameCatalog = Catalog.GetCatalog();
	InvalidFrameCatalog.Animations[0].StartFrame = -1;
	AutomotiveCatalog::FCatalogIndex InvalidFrameIndex;
	TestFalse(TEXT("拒绝负数 clip 首帧"),
		InvalidFrameIndex.Initialize(InvalidFrameCatalog, Error));
	TestEqual(
		TEXT("四阶段按契约顺序"),
		FString::JoinBy(
			Catalog.GetCatalog().Categories,
			TEXT(","),
			[](const AutomotiveCatalog::FCategory& Value) { return Value.CategoryId; }),
		FString(TEXT("exterior,interior,performance,personalization")));
	const AutomotiveCatalog::FSurface* BodySurface =
		Catalog.FindSurface(TEXT("exterior-body-cover"));
	TestNotNull(TEXT("可按 id 查询 surface 元数据"), BodySurface);
	if (BodySurface != nullptr)
	{
		TestEqual(TEXT("surface displayName"), BodySurface->DisplayName, FString(TEXT("车漆")));
	}
	const TArray<FName>* PaintSlots =
		Catalog.FindMaterialSlotIdsForSurface(TEXT("exterior-body-cover"));
	TestTrue(TEXT("车漆 surface 命中两个 SkeletalMesh slot"),
		PaintSlots != nullptr
			&& *PaintSlots == TArray<FName>({
				TEXT("sc01_exterior_body_cover"),
				TEXT("CS_Validation_Paint")}));
	TestNotNull(TEXT("原语义缺失项也有独占 SkeletalMesh slot"),
		Catalog.FindMaterialSlotIdsForSurface(TEXT("seat-backrest")));
	TestFalse(TEXT("完整槽映射不再保留 unsupported surface"),
		Catalog.IsSurfaceBindingExplicitlyUnsupported(TEXT("seat-backrest")));
	TestTrue(TEXT("40 个 surface 均命中完整 slot binding"),
		Algo::AllOf(
			Catalog.GetCatalog().SelectionOrder,
			[&Catalog](const FString& SurfaceId)
			{
				const TArray<FName>* Slots =
					Catalog.FindMaterialSlotIdsForSurface(SurfaceId);
				return Catalog.IsSurfaceBindingCovered(SurfaceId)
					&& Slots != nullptr
					&& Slots->Num() == (
						SurfaceId == TEXT("exterior-body-cover") ? 2 : 1);
			}));
	TSet<FName> UniqueBoundSlots;
	for (const FString& SurfaceId : Catalog.GetCatalog().SelectionOrder)
	{
		const TArray<FName>* Slots =
			Catalog.FindMaterialSlotIdsForSurface(SurfaceId);
		if (Slots != nullptr)
		{
			for (const FName SlotId : *Slots)
			{
				UniqueBoundSlots.Add(SlotId);
			}
		}
	}
	TestEqual(TEXT("40 个 surface 共使用 41 个唯一槽"), UniqueBoundSlots.Num(), 41);
	TMap<FString, TArray<FName>> TransactionTargets;
	TSet<FString> TransactionGaps;
	TestTrue(TEXT("选配 transaction 解析到各自 sc01 槽 binding"),
		Catalog.ResolveSurfaceBindingTransaction(
			TSet<FString>({TEXT("exterior-body-cover"), TEXT("seat-backrest")}),
			TransactionTargets,
			TransactionGaps,
			Error));
	TestTrue(TEXT("transaction 车漆命中两个车漆 slot"),
		TransactionTargets.FindRef(TEXT("exterior-body-cover"))
			== TArray<FName>({
				TEXT("sc01_exterior_body_cover"),
				TEXT("CS_Validation_Paint")}));
	TestTrue(TEXT("语义缺失项命中自己的 sc01 slot"),
		TransactionTargets.FindRef(TEXT("seat-backrest"))
			== TArray<FName>({TEXT("sc01_seat_backrest")}));
	TestTrue(TEXT("完整槽映射没有 capability 缺口"),
		TransactionGaps.IsEmpty());
	TestFalse(TEXT("transaction 拒绝未知 surface"),
		Catalog.ResolveSurfaceBindingTransaction(
			TSet<FString>({TEXT("unknown-surface")}),
			TransactionTargets,
			TransactionGaps,
			Error));
	TestEqual(TEXT("未知 transaction surface 错误码"),
		Error.Code, FString(TEXT("UNBOUND_TRANSACTION_SURFACE")));
	AutomotiveCatalog::FCatalog MultiSlotCatalog = Catalog.GetCatalog();
	MultiSlotCatalog.VehicleSurfaceBinding.Bindings[0].MaterialSlotIds.Add(
		TEXT("CS_Validation_Paint_Secondary"));
	AutomotiveCatalog::FCatalogIndex MultiSlotIndex;
	TestTrue(TEXT("一个 surface 可扩展绑定更多 SkeletalMesh slot"),
		MultiSlotIndex.Initialize(MultiSlotCatalog, Error));
	TestEqual(TEXT("扩展多槽映射保持完整"),
		MultiSlotIndex.FindMaterialSlotIdsForSurface(TEXT("exterior-body-cover"))->Num(), 3);
	AutomotiveCatalog::FCatalog CollidingSlotCatalog = MultiSlotCatalog;
	CollidingSlotCatalog.VehicleSurfaceBinding.Bindings[1].MaterialSlotIds[0] =
		CollidingSlotCatalog.VehicleSurfaceBinding.Bindings[0].MaterialSlotIds[0];
	AutomotiveCatalog::FCatalogIndex CollidingSlotIndex;
	TestFalse(TEXT("拒绝跨 surface 复用 slot，避免串色"),
		CollidingSlotIndex.Initialize(CollidingSlotCatalog, Error));
	TestEqual(TEXT("串色拒绝错误码"), Error.Code, FString(TEXT("SURFACE_SLOT_COLLISION")));
	AutomotiveCatalog::FCatalog MissingCapabilityCatalog = Catalog.GetCatalog();
	MissingCapabilityCatalog.VehicleSurfaceBinding.Bindings.Pop();
	AutomotiveCatalog::FCatalogIndex MissingCapabilityIndex;
	TestFalse(TEXT("拒绝未绑定且未显式声明的 surface 缺口"),
		MissingCapabilityIndex.Initialize(MissingCapabilityCatalog, Error));
	TestEqual(TEXT("缺口错误码"), Error.Code, FString(TEXT("INCOMPLETE_SURFACE_BINDING")));
	TestEqual(
		TEXT("车漆显式默认项来自 defaultSelections"),
		Catalog.FindDefaultOptionIdForSurface(TEXT("exterior-body-cover")) != nullptr
			? *Catalog.FindDefaultOptionIdForSurface(TEXT("exterior-body-cover"))
			: FString(),
		FString(TEXT("body-cover-red")));
	AutomotiveCatalog::FCatalog ReorderedCatalog = Catalog.GetCatalog();
	Algo::Reverse(ReorderedCatalog.Options);
	AutomotiveCatalog::FCatalogIndex ReorderedIndex;
	TestTrue(
		TEXT("多标配项重排后仍使用显式 defaultSelections"),
		ReorderedIndex.Initialize(ReorderedCatalog, Error));
	const FString* ReorderedDefault =
		ReorderedIndex.FindDefaultOptionIdForSurface(TEXT("exterior-body-cover"));
	TestEqual(
		TEXT("多标配项重排不改变默认车漆"),
		ReorderedDefault != nullptr ? *ReorderedDefault : FString(),
		FString(TEXT("body-cover-red")));
	AutomotiveCatalog::FCatalog MissingDefaultCatalog = Catalog.GetCatalog();
	MissingDefaultCatalog.DefaultSelections.Remove(TEXT("wheel-material"));
	AutomotiveCatalog::FCatalogIndex MissingDefaultIndex;
	TestFalse(
		TEXT("拒绝缺少必选 surface 显式默认项的 catalog"),
		MissingDefaultIndex.Initialize(MissingDefaultCatalog, Error));
	TestEqual(
		TEXT("车架默认银色"),
		Catalog.FindDefaultOptionIdForSurface(TEXT("engine-bay-cover")) != nullptr
			? *Catalog.FindDefaultOptionIdForSurface(TEXT("engine-bay-cover"))
			: FString(),
		FString(TEXT("engine-cover-silver")));
	const TArray<FString>* ExteriorComponents =
		Catalog.FindComponentIdsForCategory(TEXT("exterior"));
	TestTrue(
		TEXT("外饰组件按契约顺序"),
		ExteriorComponents != nullptr
			&& *ExteriorComponents == TArray<FString>({
				TEXT("car-paint"), TEXT("chassis"), TEXT("wheel"), TEXT("caliper")}));
	const TArray<FString>* WheelSurfaces =
		Catalog.FindSurfaceIdsForComponent(TEXT("wheel"));
	TestTrue(
		TEXT("轮毂项目按 selectionOrder 顺序"),
		WheelSurfaces != nullptr
			&& *WheelSurfaces == TArray<FString>({
				TEXT("wheel-material"), TEXT("wheel-style"), TEXT("wheel-color")}));
	const TArray<FString>* ExteriorCategories =
		Catalog.FindCategoryIdsForRegion(TEXT("exterior"));
	TestTrue(
		TEXT("region 到 category 索引"),
		ExteriorCategories != nullptr && ExteriorCategories->Contains(TEXT("exterior")));
	const AutomotiveCatalog::FMaterialVariant* Variant =
		Catalog.FindMaterialVariant(TEXT("ultrasuede-p6-uf7"));
	TestNotNull(TEXT("variant 展示元数据"), Variant);
	if (Variant != nullptr)
	{
		TestFalse(TEXT("variant displayName 非空"), Variant->DisplayName.IsEmpty());
		TestTrue(
			TEXT("variant 保留真实 thumbnailUrl"),
			Variant->ThumbnailUrl == TEXT("/sc01/thumbnails/ultrasuede-p6-uf7.webp"));
	}
	TSharedPtr<FJsonObject> CatalogObject;
	TestTrue(
		TEXT("读取目录以验证 option ui.sortColorHex"),
		AutomotiveCatalogAutomation::ReadJsonObject(
			AutomotiveCatalogAutomation::ContractFile(
				TEXT("fixtures/sc01.catalog.draft.v2.json")),
			CatalogObject));
	const TArray<TSharedPtr<FJsonValue>>* OptionValues = nullptr;
	if (CatalogObject.IsValid()
		&& CatalogObject->TryGetArrayField(TEXT("options"), OptionValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *OptionValues)
		{
			const TSharedPtr<FJsonObject> OptionObject = Value->AsObject();
			if (OptionObject.IsValid()
				&& OptionObject->GetStringField(TEXT("optionId"))
					== TEXT("body-cover-silver"))
			{
				const TSharedPtr<FJsonObject>* Ui = nullptr;
				if (!OptionObject->TryGetObjectField(TEXT("ui"), Ui))
				{
					OptionObject->SetObjectField(
						TEXT("ui"),
						MakeShared<FJsonObject>());
					OptionObject->TryGetObjectField(TEXT("ui"), Ui);
				}
				(*Ui)->SetStringField(TEXT("sortColorHex"), TEXT("#A1B2C3"));
				break;
			}
		}
		FString CatalogJson;
		FJsonSerializer::Serialize(
			CatalogObject.ToSharedRef(),
			TJsonWriterFactory<>::Create(&CatalogJson));
		AutomotiveCatalog::FCatalogIndex OptionUiCatalog;
		TestTrue(
			TEXT("解析带 option ui.sortColorHex 的目录"),
			OptionUiCatalog.LoadJson(CatalogJson, Error));
		const AutomotiveCatalog::FOption* Silver =
			OptionUiCatalog.FindOption(TEXT("body-cover-silver"));
		TestTrue(
			TEXT("FOption 保留 ui.sortColorHex"),
			Silver != nullptr
				&& Silver->DisplayColorHex.IsSet()
				&& Silver->DisplayColorHex.GetValue() == TEXT("#A1B2C3"));
	}

	TSharedPtr<FJsonObject> Golden;
	TestTrue(
		TEXT("读取共享黄金向量"),
		AutomotiveCatalogAutomation::ReadJsonObject(
			AutomotiveCatalogAutomation::ContractFile(TEXT("fixtures/sc01.identity-golden.v2.json")),
			Golden));
	if (!Golden.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Vectors = nullptr;
	if (!Golden->TryGetArrayField(TEXT("vectors"), Vectors))
	{
		AddError(TEXT("共享黄金向量缺少 vectors"));
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Vectors)
	{
		const TSharedPtr<FJsonObject> Vector = Value->AsObject();
		AutomotiveCatalog::FSelections Selections;
		if (!AutomotiveCatalogAutomation::ReadSelections(Vector, Selections))
		{
			AddError(TEXT("黄金向量 selections 非法"));
			continue;
		}
		AutomotiveCatalog::FConfiguration Configuration;
		const FString Name = Vector->GetStringField(TEXT("name"));
		TestTrue(
			*FString::Printf(TEXT("%s 可派生"), *Name),
			AutomotiveCatalog::DeriveConfiguration(
				Selections, AutomotiveCatalog::FCustomizations(), Catalog, Configuration, Error));
		TestEqual(
			*FString::Printf(TEXT("%s configurationId 字节一致"), *Name),
			Configuration.ConfigurationId,
			Vector->GetStringField(TEXT("configurationId")));
		TestEqual(
			*FString::Printf(TEXT("%s renderKey 字节一致"), *Name),
			Configuration.RenderKey,
			Vector->GetStringField(TEXT("renderKey")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveCatalogCustomizationAutomationTest,
	"ConfigurationSystem.Runtime.AutomotiveCatalog.Customizations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveCatalogCustomizationAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AutomotiveCatalog::FError Error;
	AutomotiveCatalog::FCatalogIndex Catalog;
	if (!Catalog.LoadJsonFile(
		AutomotiveCatalogAutomation::ContractFile(TEXT("fixtures/sc01.catalog.draft.v2.json")),
		Error))
	{
		AddError(Error.Code + TEXT(": ") + Error.Message);
		return false;
	}
	TSharedPtr<FJsonObject> Valid;
	if (!AutomotiveCatalogAutomation::ReadJsonObject(
		AutomotiveCatalogAutomation::ContractFile(TEXT("fixtures/sc01.configuration.valid.v2.json")),
		Valid))
	{
		AddError(TEXT("无法读取有效配置 fixture"));
		return false;
	}
	AutomotiveCatalog::FSelections Selections;
	AutomotiveCatalogAutomation::ReadSelections(Valid, Selections);
	Selections[TEXT("exterior-body-cover")] = TEXT("body-cover-custom");
	Selections[TEXT("steering-wheel-skin")] = TEXT("steering-skin-ultrasuede-custom");

	AutomotiveCatalog::FPaintCustomization Paint;
	Paint.ColorHex = TEXT("#336699");
	Paint.Metallic = 0.45;
	Paint.Roughness = 0.25;
	Paint.ClearCoat = 0.9;
	Paint.OrangePeel = 0.12;
	Paint.FlakeIntensity = 0.3;
	AutomotiveCatalog::FCustomizations Customizations;
	Customizations.Add(
		TEXT("exterior-body-cover"),
		AutomotiveCatalog::FCustomization::ForPaint(Paint));
	Customizations.Add(
		TEXT("steering-wheel-skin"),
		AutomotiveCatalog::FCustomization::ForMaterialVariant(TEXT("ultrasuede-p6-uf7")));

	AutomotiveCatalog::FConfiguration Configuration;
	TestTrue(
		TEXT("材料 variant 与完整车漆定制通过"),
		AutomotiveCatalog::DeriveConfiguration(
			Selections, Customizations, Catalog, Configuration, Error));
	TestEqual(
		TEXT("定制 configurationId 与 server automotive-catalog-v2.ts 一致"),
		Configuration.ConfigurationId,
		FString(TEXT("cfg-3d3bfa5b72851e7025742c48")));
	TestEqual(
		TEXT("定制 renderKey 与 server automotive-catalog-v2.ts 一致"),
		Configuration.RenderKey,
		FString(TEXT("sc01__sc01-draft-20261007__render-3d3bfa5b72851e7025742c48")));

	AutomotiveCatalog::FCustomizations Mismatch = Customizations;
	Mismatch[TEXT("steering-wheel-skin")] =
		AutomotiveCatalog::FCustomization::ForMaterialVariant(TEXT("alcantara-p2-1045"));
	TestFalse(
		TEXT("拒绝跨材料族 variant"),
		AutomotiveCatalog::ValidateCustomizations(Mismatch, Selections, Catalog, Error));
	TestEqual(
		TEXT("材料族错误码"),
		Error.Code,
		FString(TEXT("MATERIAL_VARIANT_FAMILY_MISMATCH")));

	AutomotiveCatalog::FCustomizations Unsupported;
	AutomotiveCatalog::FSelections UnsupportedSelections = Selections;
	UnsupportedSelections.Add(TEXT("door-upper"), TEXT("door-upper-microfiber-black"));
	Unsupported.Add(
		TEXT("door-upper"),
		AutomotiveCatalog::FCustomization::ForMaterialVariant(TEXT("microfiber-p16-np-3048")));
	TestFalse(
		TEXT("不支持 variant 色彩能力的同材料族 option 拒绝色卡"),
		AutomotiveCatalog::ValidateCustomizations(Unsupported, UnsupportedSelections, Catalog, Error));
	TestEqual(
		TEXT("材料色卡能力错误码"),
		Error.Code,
		FString(TEXT("MATERIAL_VARIANT_NOT_SUPPORTED")));

	AutomotiveCatalog::FCustomizations InvalidPaint = Customizations;
	InvalidPaint[TEXT("exterior-body-cover")].Paint.Metallic = 1.1;
	TestFalse(
		TEXT("拒绝越界车漆参数"),
		AutomotiveCatalog::ValidateCustomizations(InvalidPaint, Selections, Catalog, Error));
	TestEqual(
		TEXT("车漆错误码"),
		Error.Code,
		FString(TEXT("INVALID_PAINT_CUSTOMIZATION")));

	AutomotiveCatalog::FSelections LegacySelections = Selections;
	LegacySelections[TEXT("steering-wheel-skin")] =
		TEXT("steering-skin-leather-user");
	AutomotiveCatalog::FConfiguration Migrated;
	TestTrue(
		TEXT("旧 optionId 可迁移"),
		AutomotiveCatalog::DeriveConfiguration(
			LegacySelections,
			AutomotiveCatalog::FCustomizations(),
			Catalog,
			Migrated,
			Error));
	TestEqual(
		TEXT("迁移后保存规范 optionId"),
		Migrated.Selections.FindRef(TEXT("steering-wheel-skin")),
		FString(TEXT("steering-skin-leather")));

	AutomotiveCatalog::FSelections ChassisSelections = Selections;
	ChassisSelections[TEXT("engine-bay-cover")] =
		TEXT("engine-cover-ppg-custom");
	AutomotiveCatalog::FCustomizations ChassisCustomizations;
	ChassisCustomizations.Add(
		TEXT("engine-bay-cover"),
		AutomotiveCatalog::FCustomization::ForPaint(Paint));
	TestTrue(
		TEXT("自定义色按 option 能力而非固定 ID 放行"),
		AutomotiveCatalog::ValidateCustomizations(
			ChassisCustomizations,
			ChassisSelections,
			Catalog,
			Error));

	AutomotiveCatalog::FSelections OptionalOmitted = Selections;
	OptionalOmitted.Remove(TEXT("pedal"));
	TestTrue(
		TEXT("允许可选项目保持不选装"),
		AutomotiveCatalog::ValidateSelections(OptionalOmitted, Catalog, Error));

	AutomotiveCatalog::FSelections MissingSelection = Selections;
	MissingSelection.Remove(TEXT("seat-backrest"));
	TestFalse(
		TEXT("拒绝缺少必选项目的选择"),
		AutomotiveCatalog::ValidateSelections(MissingSelection, Catalog, Error));

	AutomotiveCatalog::FSelections CrossSurface = Selections;
	CrossSurface[TEXT("pedal")] = TEXT("body-cover-red");
	TestFalse(
		TEXT("拒绝跨 surface 选项"),
		AutomotiveCatalog::ValidateSelections(CrossSurface, Catalog, Error));
	return true;
}

#endif
