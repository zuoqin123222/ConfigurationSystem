#if WITH_DEV_AUTOMATION_TESTS

#include "Sc01V2Domain.h"
#include "ConfiguratorPanel.h"

#include "Algo/Reverse.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace Sc01V2Automation
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
		Sc01V2::FSelections& OutSelections)
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
	FSc01V2DataButtonAutomationTest,
	"ConfigurationSystem.Runtime.SC01V2.DataButtonBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSc01V2DataButtonAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UConfiguratorDataButton* Button =
		NewObject<UConfiguratorDataButton>(GetTransientPackage());
	FString Action;
	FString PrimaryId;
	FString SecondaryId;
	Button->OnDataClicked.AddLambda(
		[&Action, &PrimaryId, &SecondaryId](
			const FString& InAction,
			const FString& InPrimaryId,
			const FString& InSecondaryId)
		{
			Action = InAction;
			PrimaryId = InPrimaryId;
			SecondaryId = InSecondaryId;
		});
	Button->InitializeBinding(
		TEXT("option"),
		TEXT("exterior-body-cover"),
		TEXT("body-cover-custom"));
	Button->OnClicked.Broadcast();
	TestEqual(TEXT("通用按钮 action payload"), Action, FString(TEXT("option")));
	TestEqual(
		TEXT("通用按钮 primary payload"),
		PrimaryId,
		FString(TEXT("exterior-body-cover")));
	TestEqual(
		TEXT("通用按钮 secondary payload"),
		SecondaryId,
		FString(TEXT("body-cover-custom")));
	Button->InitializeBinding(
		TEXT("variant"),
		TEXT("steering-skin-leather-user"),
		TEXT("leather-p10-1217"));
	Button->OnClicked.Broadcast();
	TestEqual(TEXT("色卡按钮保留显式关联 option"), PrimaryId,
		FString(TEXT("steering-skin-leather-user")));
	TestEqual(TEXT("色卡按钮保留 variant"), SecondaryId,
		FString(TEXT("leather-p10-1217")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSc01V2GoldenVectorsAutomationTest,
	"ConfigurationSystem.Runtime.SC01V2.GoldenVectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSc01V2GoldenVectorsAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	Sc01V2::FError Error;
	Sc01V2::FCatalogIndex Catalog;
	TestTrue(
		TEXT("加载并校验 SC01 v2 catalog"),
		Catalog.LoadJsonFile(
			Sc01V2Automation::ContractFile(TEXT("fixtures/sc01.catalog.draft.v2.json")),
			Error));
	if (!Catalog.IsValid())
	{
		AddError(Error.Code + TEXT(": ") + Error.Message);
		return false;
	}
	TestEqual(TEXT("必选表面数"), Catalog.GetCatalog().SelectionOrder.Num(), 38);
	TestEqual(TEXT("选项索引覆盖 catalog"), Catalog.GetCatalog().Options.Num(), 136);
	TestEqual(TEXT("色卡索引覆盖 catalog"), Catalog.GetCatalog().MaterialVariants.Num(), 352);
	TestEqual(TEXT("车辆 displayName"), Catalog.GetCatalog().VehicleDisplayName, FString(TEXT("SC01")));
	TestEqual(TEXT("基础价读取为 22980000 分"), Catalog.GetCatalog().BasePriceMinor, int64(22980000));
	TestEqual(TEXT("region 数"), Catalog.GetCatalog().Regions.Num(), 4);
	TestEqual(TEXT("四阶段 category 数"), Catalog.GetCatalog().Categories.Num(), 4);
	TestEqual(TEXT("component 数"), Catalog.GetCatalog().Components.Num(), 14);
	TestEqual(
		TEXT("四阶段按契约顺序"),
		FString::JoinBy(
			Catalog.GetCatalog().Categories,
			TEXT(","),
			[](const Sc01V2::FCategory& Value) { return Value.CategoryId; }),
		FString(TEXT("exterior,interior,performance,personalization")));
	const Sc01V2::FSurface* BodySurface =
		Catalog.FindSurface(TEXT("exterior-body-cover"));
	TestNotNull(TEXT("可按 id 查询 surface 元数据"), BodySurface);
	if (BodySurface != nullptr)
	{
		TestEqual(TEXT("surface displayName"), BodySurface->DisplayName, FString(TEXT("车漆")));
	}
	TestEqual(
		TEXT("车漆显式默认项来自 defaultSelections"),
		Catalog.FindDefaultOptionIdForSurface(TEXT("exterior-body-cover")) != nullptr
			? *Catalog.FindDefaultOptionIdForSurface(TEXT("exterior-body-cover"))
			: FString(),
		FString(TEXT("body-cover-red")));
	Sc01V2::FCatalog ReorderedCatalog = Catalog.GetCatalog();
	Algo::Reverse(ReorderedCatalog.Options);
	Sc01V2::FCatalogIndex ReorderedIndex;
	TestTrue(
		TEXT("多标配项重排后仍使用显式 defaultSelections"),
		ReorderedIndex.Initialize(ReorderedCatalog, Error));
	const FString* ReorderedDefault =
		ReorderedIndex.FindDefaultOptionIdForSurface(TEXT("exterior-body-cover"));
	TestEqual(
		TEXT("多标配项重排不改变默认车漆"),
		ReorderedDefault != nullptr ? *ReorderedDefault : FString(),
		FString(TEXT("body-cover-red")));
	Sc01V2::FCatalog MissingDefaultCatalog = Catalog.GetCatalog();
	MissingDefaultCatalog.DefaultSelections.Remove(TEXT("wheel-material"));
	Sc01V2::FCatalogIndex MissingDefaultIndex;
	TestFalse(
		TEXT("拒绝缺少必选 surface 显式默认项的 catalog"),
		MissingDefaultIndex.Initialize(MissingDefaultCatalog, Error));
	TestNull(
		TEXT("可选机舱盖板无隐式默认项"),
		Catalog.FindDefaultOptionIdForSurface(TEXT("engine-bay-cover")));
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
	const Sc01V2::FMaterialVariant* Variant =
		Catalog.FindMaterialVariant(TEXT("ultrasuede-p6-uf7"));
	TestNotNull(TEXT("variant 展示元数据"), Variant);
	if (Variant != nullptr)
	{
		TestFalse(TEXT("variant displayName 非空"), Variant->DisplayName.IsEmpty());
		TestTrue(
			TEXT("variant 保留真实 thumbnailUrl"),
			Variant->ThumbnailUrl == TEXT("/sc01/thumbnails/ultrasuede-p6-uf7.webp"));
	}

	TSharedPtr<FJsonObject> Golden;
	TestTrue(
		TEXT("读取共享黄金向量"),
		Sc01V2Automation::ReadJsonObject(
			Sc01V2Automation::ContractFile(TEXT("fixtures/sc01.identity-golden.v2.json")),
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
		Sc01V2::FSelections Selections;
		if (!Sc01V2Automation::ReadSelections(Vector, Selections))
		{
			AddError(TEXT("黄金向量 selections 非法"));
			continue;
		}
		Sc01V2::FConfiguration Configuration;
		const FString Name = Vector->GetStringField(TEXT("name"));
		TestTrue(
			*FString::Printf(TEXT("%s 可派生"), *Name),
			Sc01V2::DeriveConfiguration(
				Selections, Sc01V2::FCustomizations(), Catalog, Configuration, Error));
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
	FSc01V2CustomizationAutomationTest,
	"ConfigurationSystem.Runtime.SC01V2.Customizations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSc01V2CustomizationAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	Sc01V2::FError Error;
	Sc01V2::FCatalogIndex Catalog;
	if (!Catalog.LoadJsonFile(
		Sc01V2Automation::ContractFile(TEXT("fixtures/sc01.catalog.draft.v2.json")),
		Error))
	{
		AddError(Error.Code + TEXT(": ") + Error.Message);
		return false;
	}
	TSharedPtr<FJsonObject> Valid;
	if (!Sc01V2Automation::ReadJsonObject(
		Sc01V2Automation::ContractFile(TEXT("fixtures/sc01.configuration.valid.v2.json")),
		Valid))
	{
		AddError(TEXT("无法读取有效配置 fixture"));
		return false;
	}
	Sc01V2::FSelections Selections;
	Sc01V2Automation::ReadSelections(Valid, Selections);
	Selections[TEXT("exterior-body-cover")] = Sc01V2::CustomPaintOptionId;
	Selections[TEXT("steering-wheel-skin")] = TEXT("steering-skin-ultrasuede-custom");

	Sc01V2::FPaintCustomization Paint;
	Paint.ColorHex = TEXT("#336699");
	Paint.Metallic = 0.45;
	Paint.Roughness = 0.25;
	Paint.ClearCoat = 0.9;
	Paint.OrangePeel = 0.12;
	Paint.FlakeIntensity = 0.3;
	Sc01V2::FCustomizations Customizations;
	Customizations.Add(
		TEXT("exterior-body-cover"),
		Sc01V2::FCustomization::ForPaint(Paint));
	Customizations.Add(
		TEXT("steering-wheel-skin"),
		Sc01V2::FCustomization::ForMaterialVariant(TEXT("ultrasuede-p6-uf7")));

	Sc01V2::FConfiguration Configuration;
	TestTrue(
		TEXT("材料 variant 与完整车漆定制通过"),
		Sc01V2::DeriveConfiguration(
			Selections, Customizations, Catalog, Configuration, Error));
	TestEqual(
		TEXT("定制 configurationId 与 server sc01-v2.ts 一致"),
		Configuration.ConfigurationId,
		FString(TEXT("cfg-3c1f9dd4444802a20100a1d2")));
	TestEqual(
		TEXT("定制 renderKey 与 server sc01-v2.ts 一致"),
		Configuration.RenderKey,
		FString(TEXT("sc01__sc01-draft-20260121__render-3c1f9dd4444802a20100a1d2")));

	Sc01V2::FCustomizations Mismatch = Customizations;
	Mismatch[TEXT("steering-wheel-skin")] =
		Sc01V2::FCustomization::ForMaterialVariant(TEXT("alcantara-p2-1045"));
	TestFalse(
		TEXT("拒绝跨材料族 variant"),
		Sc01V2::ValidateCustomizations(Mismatch, Selections, Catalog, Error));
	TestEqual(
		TEXT("材料族错误码"),
		Error.Code,
		FString(TEXT("MATERIAL_VARIANT_FAMILY_MISMATCH")));

	Sc01V2::FCustomizations Unsupported;
	Unsupported.Add(
		TEXT("seat-backrest"),
		Sc01V2::FCustomization::ForMaterialVariant(TEXT("ultrasuede-p6-uf7")));
	TestFalse(
		TEXT("不支持 variant 色彩能力的同材料族 option 拒绝色卡"),
		Sc01V2::ValidateCustomizations(Unsupported, Selections, Catalog, Error));
	TestEqual(
		TEXT("材料色卡能力错误码"),
		Error.Code,
		FString(TEXT("MATERIAL_VARIANT_NOT_SUPPORTED")));

	Sc01V2::FCustomizations InvalidPaint = Customizations;
	InvalidPaint[TEXT("exterior-body-cover")].Paint.Metallic = 1.1;
	TestFalse(
		TEXT("拒绝越界车漆参数"),
		Sc01V2::ValidateCustomizations(InvalidPaint, Selections, Catalog, Error));
	TestEqual(
		TEXT("车漆错误码"),
		Error.Code,
		FString(TEXT("INVALID_PAINT_CUSTOMIZATION")));

	Sc01V2::FSelections OptionalOmitted = Selections;
	OptionalOmitted.Remove(TEXT("pedal"));
	TestTrue(
		TEXT("允许可选项目保持不选装"),
		Sc01V2::ValidateSelections(OptionalOmitted, Catalog, Error));

	Sc01V2::FSelections MissingSelection = Selections;
	MissingSelection.Remove(TEXT("seat-backrest"));
	TestFalse(
		TEXT("拒绝缺少必选项目的选择"),
		Sc01V2::ValidateSelections(MissingSelection, Catalog, Error));

	Sc01V2::FSelections CrossSurface = Selections;
	CrossSurface[TEXT("pedal")] = TEXT("body-cover-red");
	TestFalse(
		TEXT("拒绝跨 surface 选项"),
		Sc01V2::ValidateSelections(CrossSurface, Catalog, Error));
	return true;
}

#endif
