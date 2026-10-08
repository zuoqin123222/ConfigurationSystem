#if WITH_DEV_AUTOMATION_TESTS

#include "AutomotiveMaterialBinder.h"

#include "Algo/AllOf.h"
#include "ConfiguratorVehicleActor.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "AutomotiveMaterialLibrary.h"
#include "AutomotiveCatalogData.h"
#include "AutomotiveConfigurationState.h"

namespace AutomotiveMaterialBinderAutomation
{
	struct FCoverageSpec
	{
		TArray<FString> BakeOptionIds;
		TMap<FString, FString> VariantOptionByFamily;
		int32 ExcludedColorPickerOptionCount = 0;
	};

	bool IsDisabled(const TSharedPtr<FJsonObject>& Option)
	{
		const TSharedPtr<FJsonObject>* Availability = nullptr;
		FString Status;
		return Option->TryGetObjectField(TEXT("availability"), Availability)
			&& Availability != nullptr
			&& (*Availability)->TryGetStringField(TEXT("status"), Status)
			&& Status == TEXT("disabled");
	}

	FString GetUiControl(const TSharedPtr<FJsonObject>& Option)
	{
		const TSharedPtr<FJsonObject>* Ui = nullptr;
		FString Control;
		if (Option->TryGetObjectField(TEXT("ui"), Ui) && Ui != nullptr)
		{
			(*Ui)->TryGetStringField(TEXT("control"), Control);
		}
		return Control;
	}

	bool IsPricedVariantOption(const TSharedPtr<FJsonObject>& Option)
	{
		const TSharedPtr<FJsonObject>* Parameters = nullptr;
		const TSharedPtr<FJsonObject>* Color = nullptr;
		FString Mode;
		if (!Option->TryGetObjectField(TEXT("parameters"), Parameters)
			|| Parameters == nullptr
			|| !(*Parameters)->TryGetObjectField(TEXT("color"), Color)
			|| Color == nullptr
			|| !(*Color)->TryGetStringField(TEXT("mode"), Mode)
			|| Mode != TEXT("variant"))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Pricing = nullptr;
		bool bIsStandard = true;
		if (!Option->TryGetObjectField(TEXT("pricing"), Pricing)
			|| Pricing == nullptr
			|| !(*Pricing)->TryGetBoolField(TEXT("isStandard"), bIsStandard)
			|| bIsStandard)
		{
			return false;
		}
		const TSharedPtr<FJsonValue>* UnitPrice =
			(*Pricing)->Values.Find(TEXT("unitPriceMinor"));
		return UnitPrice != nullptr
			&& UnitPrice->IsValid()
			&& (*UnitPrice)->Type == EJson::Number;
	}

	bool ParseCoverageSpec(
		const FString& CatalogJson,
		FCoverageSpec& OutSpec)
	{
		OutSpec = FCoverageSpec();
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(
				TJsonReaderFactory<>::Create(CatalogJson),
				Root)
			|| !Root.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
		if (!Root->TryGetArrayField(TEXT("options"), Options)
			|| Options == nullptr)
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Options)
		{
			const TSharedPtr<FJsonObject> Option = Value->AsObject();
			bool bRenderRelevant = false;
			if (!Option.IsValid()
				|| !Option->TryGetBoolField(
					TEXT("renderRelevant"),
					bRenderRelevant)
				|| !bRenderRelevant
				|| IsDisabled(Option))
			{
				continue;
			}
			const FString Control = GetUiControl(Option);
			if (Control == TEXT("color-picker"))
			{
				++OutSpec.ExcludedColorPickerOptionCount;
				continue;
			}
			FString OptionId;
			FString FamilyId;
			if (!Option->TryGetStringField(TEXT("optionId"), OptionId))
			{
				return false;
			}
			OutSpec.BakeOptionIds.Add(OptionId);
			if (IsPricedVariantOption(Option)
				&& Option->TryGetStringField(
					TEXT("materialFamilyId"),
					FamilyId)
				&& !OutSpec.VariantOptionByFamily.Contains(FamilyId))
			{
				// 与 generateV2Coverage 的 Array.find 语义一致：每个材料族使用
				// Catalog 中第一个满足 coverage 规则的付费 variant option。
				OutSpec.VariantOptionByFamily.Add(FamilyId, OptionId);
			}
		}
		return true;
	}

	TOptional<FLinearColor> ResolveCatalogColor(const FString& Value)
	{
		if (Value.Equals(TEXT("red"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#A61D24")));
		}
		if (Value.Equals(TEXT("silver"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#BFC3C7")));
		}
		const FString Hex = Value.StartsWith(TEXT("#")) ? Value.Mid(1) : Value;
		bool bValid = Hex.Len() == 6 || Hex.Len() == 8;
		for (const TCHAR Character : Hex)
		{
			bValid = bValid && FChar::IsHexDigit(Character);
		}
		return bValid
			? TOptional<FLinearColor>(
				FLinearColor::FromSRGBColor(FColor::FromHex(Value)))
			: TOptional<FLinearColor>();
	}

	TOptional<FLinearColor> ResolveExpectedOptionColor(
		const AutomotiveCatalog::FOption& Option)
	{
		if (Option.ColorCode.IsSet())
		{
			const TOptional<FLinearColor> Color =
				ResolveCatalogColor(Option.ColorCode.GetValue());
			if (Color.IsSet())
			{
				return Color;
			}
		}
		return Option.DisplayColorHex.IsSet()
			? ResolveCatalogColor(Option.DisplayColorHex.GetValue())
			: TOptional<FLinearColor>();
	}

	FAutomotiveCustomization MakeNeutralPaintCustomization()
	{
		FAutomotiveCustomization Result;
		Result.Kind = EAutomotiveCustomizationKind::Paint;
		Result.Paint.ColorHex = TEXT("#808080");
		Result.Paint.Metallic = 0.35;
		Result.Paint.Roughness = 0.22;
		Result.Paint.ClearCoat = 0.85;
		Result.Paint.OrangePeel = 0.12;
		Result.Paint.FlakeIntensity = 0.25;
		return Result;
	}

	bool PrimeDifferentOption(
		UAutomotiveMaterialBinder* Binder,
		UAutomotiveConfigurationState* State,
		const AutomotiveCatalog::FOption& Target)
	{
		if (State->GetSelections().FindRef(Target.SurfaceId) != Target.OptionId)
		{
			return true;
		}
		const TArray<FString>* SurfaceOptions =
			State->GetCatalogIndex().FindOptionIdsForSurface(Target.SurfaceId);
		if (SurfaceOptions == nullptr)
		{
			return false;
		}
		for (const FString& CandidateId : *SurfaceOptions)
		{
			if (CandidateId == Target.OptionId)
			{
				continue;
			}
			const AutomotiveCatalog::FOption* Candidate =
				State->GetCatalogIndex().FindOption(CandidateId);
			if (Candidate == nullptr)
			{
				continue;
			}
			TMap<FString, FString> Selections = State->GetSelections();
			TMap<FString, FAutomotiveCustomization> Customizations =
				State->GetCustomizations();
			Selections.Add(Target.SurfaceId, CandidateId);
			Customizations.Remove(Target.SurfaceId);
			if (Candidate->SupportsCustomColor())
			{
				Customizations.Add(
					Target.SurfaceId,
					MakeNeutralPaintCustomization());
			}
			if (Binder->ApplyTransaction(Selections, Customizations).bSuccess)
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveMaterialBinderAutomationTest,
	"ConfigurationSystem.Runtime.AutomotiveMaterials.Binder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAutomotiveMaterialBinderExhaustiveCoverageAutomationTest,
	"ConfigurationSystem.Runtime.AutomotiveMaterials.ExhaustiveCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAutomotiveMaterialBinderAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UAutomotiveCatalogData* Catalog = LoadObject<UAutomotiveCatalogData>(
		nullptr,
		TEXT("/Game/SC01/DA_SC01Catalog.DA_SC01Catalog"));
	UAutomotiveMaterialLibrary* Library = LoadObject<UAutomotiveMaterialLibrary>(
		nullptr,
		TEXT("/Game/SC01/Materials/DA_SC01MaterialLibrary.DA_SC01MaterialLibrary"));
	TestNotNull(TEXT("加载 v2 catalog"), Catalog);
	TestNotNull(TEXT("加载 AutomotiveMaterialLibrary"), Library);
	if (Catalog == nullptr || Library == nullptr)
	{
		return false;
	}
	UAutomotiveCatalogData* TestCatalog =
		DuplicateObject<UAutomotiveCatalogData>(Catalog, GetTransientPackage());
	TSharedPtr<FJsonObject> CatalogObject;
	TestTrue(
		TEXT("解析测试目录 JSON"),
		FJsonSerializer::Deserialize(
			TJsonReaderFactory<>::Create(TestCatalog->CatalogJson),
			CatalogObject));
	const TArray<TSharedPtr<FJsonValue>>* OptionValues = nullptr;
	if (CatalogObject.IsValid()
		&& CatalogObject->TryGetArrayField(TEXT("options"), OptionValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *OptionValues)
		{
			const TSharedPtr<FJsonObject> OptionObject = Value->AsObject();
			if (!OptionObject.IsValid())
			{
				continue;
			}
			const FString OptionId = OptionObject->GetStringField(TEXT("optionId"));
			if (OptionId != TEXT("body-cover-red")
				&& OptionId != TEXT("wheel-magnesium-alloy"))
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Ui = nullptr;
			OptionObject->TryGetObjectField(TEXT("ui"), Ui);
			(*Ui)->SetStringField(
				TEXT("sortColorHex"),
				OptionId == TEXT("body-cover-red")
					? TEXT("#00FF00")
					: TEXT("#336699"));
		}
		const TSharedRef<TJsonWriter<>> Writer =
			TJsonWriterFactory<>::Create(&TestCatalog->CatalogJson);
		FJsonSerializer::Serialize(CatalogObject.ToSharedRef(), Writer);
	}
	Catalog = TestCatalog;

	UAutomotiveConfigurationState* State =
		NewObject<UAutomotiveConfigurationState>(GetTransientPackage());
	TestTrue(TEXT("初始化 v2 状态"), State->Initialize(Catalog));
	AConfiguratorVehicleActor* Vehicle =
		NewObject<AConfiguratorVehicleActor>(GetTransientPackage());
	TestTrue(
		TEXT("Bind 前配置带 40 个 sc01 槽的骨骼车"),
		Vehicle->ConfigureAnimationFromCatalog(
			State->GetCatalogIndex().GetCatalog()));
	UAutomotiveMaterialBinder* Binder =
		NewObject<UAutomotiveMaterialBinder>(GetTransientPackage());
	const bool bBound = Binder->Bind(State, Library, Vehicle);
	TestTrue(TEXT("绑定骨骼车明确车漆/内饰槽"), bBound);
	if (!bBound)
	{
		AddError(FString::Printf(
			TEXT("Binder 绑定失败：%s"),
			*Binder->GetLastError()));
		return false;
	}
	TestNotNull(TEXT("找到骨骼车车漆槽"), Binder->GetPaintComponent());
	TestNotNull(TEXT("找到骨骼车内饰槽"), Binder->GetInteriorComponent());
	TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents(Vehicle);
	int32 VisibleSkeletalComponentCount = 0;
	for (const USkeletalMeshComponent* Component : SkeletalComponents)
	{
		if (IsValid(Component)
			&& Component->IsVisible()
			&& !Component->bHiddenInGame)
		{
			++VisibleSkeletalComponentCount;
		}
	}
	TestEqual(
		TEXT("车辆仅有一个可见 SkeletalMeshComponent"),
		VisibleSkeletalComponentCount,
		1);
	TSet<FName> UniqueSlots;
	TSet<int32> UniqueMaterialIndices;
	USkeletalMeshComponent* BoundSkeletalComponent = nullptr;
	for (const AutomotiveCatalog::FSurfaceBinding& Binding :
		State->GetCatalogIndex().GetCatalog().VehicleSurfaceBinding.Bindings)
	{
		const int32 ExpectedSlotCount = Binding.MaterialSlotIds.Num();
		const TArray<FAutomotiveBoundMaterialSlot> BoundSlots =
			Binder->GetBoundSlots(Binding.SurfaceId);
		TestEqual(
			*FString::Printf(TEXT("%s Binder 命中预期槽数"), *Binding.SurfaceId),
			Binder->GetBoundSlotCount(Binding.SurfaceId),
			ExpectedSlotCount);
		TestEqual(
			*FString::Printf(TEXT("%s 只读查询返回预期槽数"), *Binding.SurfaceId),
			BoundSlots.Num(),
			ExpectedSlotCount);
		bool bCatalogSlotOrderMatches =
			BoundSlots.Num() == Binding.MaterialSlotIds.Num();
		for (int32 SlotIndex = 0;
			bCatalogSlotOrderMatches && SlotIndex < BoundSlots.Num();
			++SlotIndex)
		{
			bCatalogSlotOrderMatches =
				BoundSlots[SlotIndex].SlotId == Binding.MaterialSlotIds[SlotIndex];
		}
		TestTrue(
			*FString::Printf(TEXT("%s 查询结果保持 catalog 槽顺序"), *Binding.SurfaceId),
			bCatalogSlotOrderMatches);
		for (const FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
		{
			TestTrue(
				*FString::Printf(
					TEXT("%s/%s Binder 命中可见骨骼组件"),
					*Binding.SurfaceId,
					*Bound.SlotId.ToString()),
				IsValid(Bound.Component)
					&& Cast<USkeletalMeshComponent>(Bound.Component) != nullptr
					&& Bound.Component->IsVisible()
					&& !Bound.Component->bHiddenInGame);
			USkeletalMeshComponent* CurrentSkeletalComponent =
				Cast<USkeletalMeshComponent>(Bound.Component);
			if (BoundSkeletalComponent == nullptr)
			{
				BoundSkeletalComponent = CurrentSkeletalComponent;
			}
			TestTrue(
				*FString::Printf(TEXT("%s 复用同一个可见骨骼组件"), *Binding.SurfaceId),
				CurrentSkeletalComponent == BoundSkeletalComponent);
			TestFalse(
				*FString::Printf(TEXT("%s 槽名不被复用"), *Bound.SlotId.ToString()),
				UniqueSlots.Contains(Bound.SlotId));
			UniqueSlots.Add(Bound.SlotId);
			TestFalse(
				*FString::Printf(TEXT("%s 材质索引不被复用"), *Bound.SlotId.ToString()),
				UniqueMaterialIndices.Contains(Bound.MaterialIndex));
			UniqueMaterialIndices.Add(Bound.MaterialIndex);
		}
	}
	TestNotNull(TEXT("40 个 surface 绑定单一 SkeletalMeshComponent"), BoundSkeletalComponent);
	TestEqual(TEXT("骨骼车包含 40 个唯一选配槽"), UniqueSlots.Num(), 40);
	TestEqual(TEXT("骨骼车包含 40 个唯一选配材质索引"), UniqueMaterialIndices.Num(), 40);
	const AutomotiveCatalog::FOption* RedOption =
		State->GetCatalogIndex().FindOption(TEXT("body-cover-red"));
	TestTrue(
		TEXT("Binder 测试目录包含冲突的 ColorCode 与 DisplayColorHex"),
		RedOption != nullptr
			&& RedOption->ColorCode.IsSet()
			&& RedOption->DisplayColorHex.IsSet());

	struct FInteriorCase
	{
		const TCHAR* OptionId;
		const TCHAR* FamilyId;
		UMaterialInterface* Expected;
	};
	const FInteriorCase InteriorCases[] = {
		{TEXT("door-middle-ultrasuede-black"), TEXT("ultrasuede"), Library->LoadInteriorMaterial(TEXT("ultrasuede"))},
		{TEXT("door-middle-alcantara"), TEXT("alcantara"), Library->LoadInteriorMaterial(TEXT("alcantara"))},
		{TEXT("door-middle-leather"), TEXT("leather"), Library->LoadInteriorMaterial(TEXT("leather"))},
		{TEXT("door-middle-microfiber"), TEXT("microfiber"), Library->LoadInteriorMaterial(TEXT("microfiber"))},
		{TEXT("door-middle-woven-wool"), TEXT("woven-wool"), Library->LoadInteriorMaterial(TEXT("woven-wool"))}
	};
	for (const FInteriorCase& Case : InteriorCases)
	{
		TestTrue(
			*FString::Printf(TEXT("选择 %s"), Case.OptionId),
			State->SelectOption(UAutomotiveMaterialBinder::InteriorProxySurfaceId, Case.OptionId));
		TestEqual(
			*FString::Printf(TEXT("代理槽切换到 %s"), Case.FamilyId),
			Binder->GetAppliedInteriorFamilyId(),
			FString(Case.FamilyId));
		TestTrue(
			TEXT("骨骼车槽使用对应材料族的可见实例"),
			IsValid(Binder->GetInteriorComponent())
				&& IsValid(Case.Expected)
				&& IsValid(Binder->GetAppliedMaterialForSurface(
					UAutomotiveMaterialBinder::InteriorProxySurfaceId)));
	}
	TMap<FString, FString> VariantSelections = State->GetSelections();
	VariantSelections.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		TEXT("door-middle-leather"));
	TMap<FString, FAutomotiveCustomization> VariantCustomizations =
		State->GetCustomizations();
	FAutomotiveCustomization VariantCustomization;
	VariantCustomization.Kind = EAutomotiveCustomizationKind::MaterialVariant;
	VariantCustomization.MaterialVariantId = TEXT("leather-p10-1217");
	VariantCustomizations.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		VariantCustomization);
	const FAutomotiveMaterialTransactionResult VariantResult =
		Binder->ApplyTransaction(VariantSelections, VariantCustomizations);
	TestTrue(TEXT("variantId 事务成功"), VariantResult.bSuccess);
	TestTrue(
		TEXT("variantId 直接命中阶段3物化 MI"),
		Binder->GetAppliedMaterialForSurface(
			UAutomotiveMaterialBinder::InteriorProxySurfaceId)
			== Library->LoadVariantMaterial(TEXT("leather-p10-1217")));
	VariantCustomizations.Remove(UAutomotiveMaterialBinder::InteriorProxySurfaceId);
	TestTrue(
		TEXT("清除 variant 恢复 option 的材料族默认材质"),
		Binder->ApplyTransaction(
			VariantSelections,
			VariantCustomizations).bSuccess);
	TestTrue(
		TEXT("清除 variant 后槽恢复 leather family 的可见实例"),
		Binder->GetAppliedInteriorFamilyId() == TEXT("leather")
			&& IsValid(Binder->GetAppliedMaterialForSurface(
				UAutomotiveMaterialBinder::InteriorProxySurfaceId)));
	TMap<FString, FString> InvalidSelections = State->GetSelections();
	InvalidSelections.Add(TEXT("exterior-body-cover"), TEXT("unknown-option"));
	const FAutomotiveMaterialTransactionResult InvalidResult =
		Binder->ApplyTransaction(InvalidSelections, State->GetCustomizations());
	TestFalse(TEXT("非法 option 事务被拒绝"), InvalidResult.bSuccess);
	TestEqual(
		TEXT("Binder 保留目录返回的具体事务错误码"),
		InvalidResult.Code,
		FString(TEXT("INVALID_OPTION")));
	TestTrue(
		TEXT("Binder 保留目录返回的具体事务错误消息"),
		InvalidResult.Message.Contains(TEXT("exterior-body-cover"))
			&& InvalidResult.Message.Contains(TEXT("optionId")));
	const FString BeforeRejectedConfigurationId = State->GetConfigurationId();
	UMaterialInterface* BeforeRejectedMaterial =
		Binder->GetAppliedMaterialForSurface(
			UAutomotiveMaterialBinder::InteriorProxySurfaceId);
	const TSoftObjectPtr<UMaterialInterface> SavedVariant =
		Library->Variants.FindRef(TEXT("leather-p10-1217"));
	Library->Variants.Remove(TEXT("leather-p10-1217"));
	TMap<FString, FAutomotiveCustomization> MissingAssetCustomizations =
		VariantCustomizations;
	MissingAssetCustomizations.Add(
		UAutomotiveMaterialBinder::InteriorProxySurfaceId,
		VariantCustomization);
	const FAutomotiveMaterialTransactionResult MissingAssetResult =
		Binder->ApplyTransaction(VariantSelections, MissingAssetCustomizations);
	TestFalse(TEXT("缺失物化 MI 时拒绝整个事务"), MissingAssetResult.bSuccess);
	TestEqual(
		TEXT("缺失物化 MI 返回明确错误码"),
		MissingAssetResult.Code,
		FString(TEXT("MATERIAL_VARIANT_ASSET_MISSING")));
	TestEqual(
		TEXT("失败事务不修改配置身份"),
		State->GetConfigurationId(),
		BeforeRejectedConfigurationId);
	TestTrue(
		TEXT("失败事务不修改已显示材质"),
		Binder->GetAppliedMaterialForSurface(
			UAutomotiveMaterialBinder::InteriorProxySurfaceId)
			== BeforeRejectedMaterial);
	Library->Variants.Add(TEXT("leather-p10-1217"), SavedVariant);

	const TArray<FString>* ProxyOptions =
		State->GetCatalogIndex().FindOptionIdsForSurface(TEXT("wheel-style"));
	TestTrue(
		TEXT("测试目录包含代理可视化 surface"),
		ProxyOptions != nullptr && ProxyOptions->Num() > 1);
	if (ProxyOptions != nullptr && ProxyOptions->Num() > 1)
	{
		TMap<FString, FString> ProxySelections = State->GetSelections();
		const FString Current = ProxySelections.FindRef(TEXT("wheel-style"));
		const FString* Replacement = ProxyOptions->FindByPredicate(
			[&Current](const FString& Value) { return Value != Current; });
		TestNotNull(TEXT("找到不同的代理 option"), Replacement);
		UMaterialInstanceDynamic* ProxyMaterial =
			Cast<UMaterialInstanceDynamic>(
				Binder->GetAppliedMaterialForSurface(TEXT("wheel-style")));
		const FLinearColor BeforeProxyColor = IsValid(ProxyMaterial)
			? ProxyMaterial->K2_GetVectorParameterValue(TEXT("BaseColor"))
			: FLinearColor::Transparent;
		ProxySelections.Add(TEXT("wheel-style"), *Replacement);
		const FAutomotiveMaterialTransactionResult ProxyResult =
			Binder->ApplyTransaction(
				ProxySelections,
				State->GetCustomizations());
		TestTrue(TEXT("代理 surface 原子提交成功"), ProxyResult.bSuccess);
		TestEqual(
			TEXT("代理 surface 返回完整应用回执"),
			ProxyResult.Code,
			FString(TEXT("APPLIED")));
		TestTrue(
			TEXT("回执明确列出已切换 surfaceId"),
			ProxyResult.AppliedSurfaceIds.Contains(TEXT("wheel-style")));
		TestTrue(
			TEXT("代理选项切换更新可见运行时材质"),
			IsValid(Cast<UMaterialInstanceDynamic>(
				Binder->GetAppliedMaterialForSurface(TEXT("wheel-style"))))
				&& !CastChecked<UMaterialInstanceDynamic>(
					Binder->GetAppliedMaterialForSurface(TEXT("wheel-style")))
					->K2_GetVectorParameterValue(TEXT("BaseColor"))
					.Equals(BeforeProxyColor));
	}

	UMaterialInstanceDynamic* PaintInstance = Binder->GetPaintMaterialInstance();
	TestNotNull(TEXT("默认车漆创建动态实例"), PaintInstance);
	if (PaintInstance != nullptr)
	{
		const FLinearColor Red =
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#A61D24")));
		TestTrue(
			TEXT("固定色优先 ColorCode 而非 ui.sortColorHex"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(
				Red,
				0.001f));
		TestTrue(
			TEXT("固定色同步写入 Substrate 车漆可见 Tint"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("Tint")).Equals(
				Red,
				0.001f));
	}
	TestTrue(
		TEXT("选择标准银色"),
		State->SelectOption(
			UAutomotiveMaterialBinder::PaintSurfaceId,
			TEXT("body-cover-silver")));
	if (PaintInstance != nullptr)
	{
		const FLinearColor Silver =
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#BFC3C7")));
		TestTrue(
			TEXT("标准银色写入车漆 BaseColor"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(
				Silver,
				0.001f));
		TestTrue(
			TEXT("标准银色写入 Substrate 车漆可见 Tint"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("Tint")).Equals(
				Silver,
				0.001f));
	}

	TMap<FString, FString> DisplayColorSelections = State->GetSelections();
	DisplayColorSelections.Add(
		TEXT("wheel-material"),
		TEXT("wheel-magnesium-alloy"));
	TestTrue(
		TEXT("无 ColorCode 的固定色事务成功"),
		Binder->ApplyTransaction(
			DisplayColorSelections,
			State->GetCustomizations()).bSuccess);
	UMaterialInstanceDynamic* DisplayColorMaterial =
		Cast<UMaterialInstanceDynamic>(
			Binder->GetAppliedMaterialForSurface(TEXT("wheel-material")));
	TestTrue(
		TEXT("固定色在 ColorCode 缺失时使用 ui.sortColorHex"),
		IsValid(DisplayColorMaterial)
			&& DisplayColorMaterial->K2_GetVectorParameterValue(TEXT("BaseColor"))
				.Equals(
					FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#336699"))),
					0.001f));
	TestTrue(
		TEXT("金属材料同步写入真实 Metallic Color A"),
		IsValid(DisplayColorMaterial)
			&& DisplayColorMaterial->K2_GetVectorParameterValue(
				TEXT("Metallic Color A")).Equals(
					FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#336699"))),
					0.001f));
	TestTrue(
		TEXT("DisplayColorHex 调色仍保留镁合金母材质"),
		IsValid(DisplayColorMaterial)
			&& DisplayColorMaterial->IsChildOf(
				Library->LoadInteriorMaterial(TEXT("magnesium-alloy"))));

	TMap<FString, FString> NeutralSelections = State->GetSelections();
	NeutralSelections.Add(TEXT("wheel-style"), TEXT("wheel-style-magnesium-1"));
	TestTrue(
		TEXT("无色值结构/样式代理事务成功"),
		Binder->ApplyTransaction(
			NeutralSelections,
			State->GetCustomizations()).bSuccess);
	UMaterialInstanceDynamic* NeutralMaterial =
		Cast<UMaterialInstanceDynamic>(
			Binder->GetAppliedMaterialForSurface(TEXT("wheel-style")));
	const FLinearColor NeutralColor = IsValid(NeutralMaterial)
		? NeutralMaterial->K2_GetVectorParameterValue(TEXT("BaseColor"))
		: FLinearColor::Transparent;
	TestTrue(
		TEXT("无色值结构/样式代理使用中性灰阶"),
		IsValid(NeutralMaterial)
			&& FMath::IsNearlyEqual(NeutralColor.R, NeutralColor.G)
			&& FMath::IsNearlyEqual(NeutralColor.G, NeutralColor.B));
	TestTrue(
		TEXT("中性灰代理仍保留材料族母材质质感"),
		IsValid(NeutralMaterial)
			&& NeutralMaterial->IsChildOf(
				Library->LoadInteriorMaterial(TEXT("magnesium-alloy"))));

	const AutomotiveCatalog::FMaterialVariant* DisplayColorVariant =
		State->GetCatalogIndex().FindMaterialVariant(TEXT("alcantara-p2-2911"));
	TestTrue(
		TEXT("色卡 ui.sortColorHex 进入 UE 目录模型"),
		DisplayColorVariant != nullptr
			&& DisplayColorVariant->DisplayColorHex.IsSet()
			&& DisplayColorVariant->DisplayColorHex.GetValue() == TEXT("#DFDBBE"));

	FAutomotivePaintCustomization Paint;
	Paint.ColorHex = TEXT("#336699");
	Paint.Metallic = 0.45;
	Paint.Roughness = 0.25;
	Paint.ClearCoat = 0.9;
	Paint.OrangePeel = 0.12;
	Paint.FlakeIntensity = 0.3;
	TMap<FString, FString> TransactionSelections = State->GetSelections();
	TransactionSelections.Add(
		UAutomotiveMaterialBinder::PaintSurfaceId,
		TEXT("body-cover-custom"));
	TMap<FString, FAutomotiveCustomization> TransactionCustomizations =
		State->GetCustomizations();
	FAutomotiveCustomization PaintCustomization;
	PaintCustomization.Kind = EAutomotiveCustomizationKind::Paint;
	PaintCustomization.Paint = Paint;
	TransactionCustomizations.Add(
		UAutomotiveMaterialBinder::PaintSurfaceId,
		PaintCustomization);
	const FAutomotiveMaterialTransactionResult PaintResult =
		Binder->ApplyTransaction(TransactionSelections, TransactionCustomizations);
	TestTrue(TEXT("Binder 原子提交 v2 selections/customizations"), PaintResult.bSuccess);
	TestTrue(
		TEXT("车漆事务回执包含 surface"),
		PaintResult.AppliedSurfaceIds
			== TArray<FString>({TEXT("exterior-body-cover")}));
	TestTrue(
		TEXT("车漆事务回执只包含主体车漆槽"),
		PaintResult.AppliedSlotIds
			== TArray<FName>({
				TEXT("sc01_exterior_body_cover")}));

	PaintInstance = Binder->GetPaintMaterialInstance();
	TestNotNull(TEXT("车身代理使用动态车漆实例"), PaintInstance);
	if (PaintInstance != nullptr)
	{
		const FLinearColor Expected =
			FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#336699")));
		TestTrue(
			TEXT("BaseColor 由 v2 自定义颜色驱动"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("BaseColor")).Equals(
				Expected,
				0.001f));
		TestTrue(
			TEXT("Substrate Tint 由 v2 自定义颜色驱动"),
			PaintInstance->K2_GetVectorParameterValue(TEXT("Tint")).Equals(
				Expected,
				0.001f));
		TestTrue(
			TEXT("车漆亮片颜色同步自定义颜色"),
			PaintInstance->K2_GetVectorParameterValue(
				TEXT("Primary Glints Color")).Equals(
					Expected,
					0.001f));
		TestTrue(TEXT("Metallic 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("Metallic")),
				0.45f));
		TestTrue(TEXT("Roughness 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("Roughness")),
				0.25f));
		TestTrue(TEXT("ClearCoat 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("ClearCoat")),
				0.9f));
		TestTrue(TEXT("OrangePeel 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("OrangePeel")),
				0.12f));
		TestTrue(TEXT("FlakeIntensity 由 v2 参数驱动"),
			FMath::IsNearlyEqual(
				PaintInstance->K2_GetScalarParameterValue(TEXT("FlakeIntensity")),
				0.3f));
	}
	FAutomotivePaintCustomization RecoloredPaint = Paint;
	RecoloredPaint.ColorHex = TEXT("#112233");
	TransactionCustomizations.FindChecked(
		UAutomotiveMaterialBinder::PaintSurfaceId).Paint = RecoloredPaint;
	TestTrue(
		TEXT("同一车漆 parent 二次调色事务成功"),
		Binder->ApplyTransaction(
			TransactionSelections,
			TransactionCustomizations).bSuccess);
	TestTrue(
		TEXT("连续自定义车漆调色复用同一个 MID"),
		Binder->GetPaintMaterialInstance() == PaintInstance);

	for (const FString& SurfaceId :
		State->GetCatalogIndex().GetCatalog().SelectionOrder)
	{
		const TArray<FString>* Options =
			State->GetCatalogIndex().FindOptionIdsForSurface(SurfaceId);
		if (Options == nullptr || Options->IsEmpty())
		{
			continue;
		}
		TMap<FString, FString> NextSelections = State->GetSelections();
		const FString CurrentOption = NextSelections.FindRef(SurfaceId);
		const FString* NextOption = Options->FindByPredicate(
			[&CurrentOption](const FString& Value)
			{
				return Value != CurrentOption;
			});
		if (NextOption == nullptr)
		{
			continue;
		}
		UMaterialInterface* Before =
			Binder->GetAppliedMaterialForSurface(SurfaceId);
		UMaterialInstanceDynamic* BeforeDynamic =
			Cast<UMaterialInstanceDynamic>(Before);
		const FLinearColor BeforeColor = IsValid(BeforeDynamic)
			? BeforeDynamic->K2_GetVectorParameterValue(TEXT("BaseColor"))
			: FLinearColor::Transparent;
		NextSelections.Add(SurfaceId, *NextOption);
		TMap<FString, FAutomotiveCustomization> NextCustomizations =
			State->GetCustomizations();
		NextCustomizations.Remove(SurfaceId);
		const FAutomotiveMaterialTransactionResult Result =
			Binder->ApplyTransaction(NextSelections, NextCustomizations);
		TestTrue(
			*FString::Printf(TEXT("%s 逐项事务成功"), *SurfaceId),
			Result.bSuccess);
		TestTrue(
			*FString::Printf(TEXT("%s 回执包含唯一 surface"), *SurfaceId),
			Result.AppliedSurfaceIds == TArray<FString>({SurfaceId}));
		const TArray<FName>* ExpectedSlots =
			State->GetCatalogIndex().FindMaterialSlotIdsForSurface(SurfaceId);
		TestTrue(
			*FString::Printf(TEXT("%s 回执包含全部 catalog 槽"), *SurfaceId),
			ExpectedSlots != nullptr
				&& Result.AppliedSlotIds == *ExpectedSlots);
		TestTrue(
			*FString::Printf(TEXT("%s unsupported 为空"), *SurfaceId),
			Result.UnsupportedSurfaceIds.IsEmpty());
		UMaterialInterface* After =
			Binder->GetAppliedMaterialForSurface(SurfaceId);
		UMaterialInstanceDynamic* AfterDynamic =
			Cast<UMaterialInstanceDynamic>(After);
		TestTrue(
			*FString::Printf(TEXT("%s 切换产生可见材质差异"), *SurfaceId),
			IsValid(After)
				&& (After != Before
					|| (IsValid(AfterDynamic)
						&& !AfterDynamic->K2_GetVectorParameterValue(
							TEXT("BaseColor")).Equals(BeforeColor))));
		const TArray<FAutomotiveBoundMaterialSlot> SurfaceBoundSlots =
			Binder->GetBoundSlots(SurfaceId);
		const FLinearColor AfterColor = IsValid(AfterDynamic)
			? AfterDynamic->K2_GetVectorParameterValue(TEXT("BaseColor"))
			: FLinearColor::Transparent;
		TestTrue(
			*FString::Printf(TEXT("%s 切换后全部绑定槽均更新"), *SurfaceId),
			ExpectedSlots != nullptr
				&& SurfaceBoundSlots.Num() == ExpectedSlots->Num()
				&& Algo::AllOf(
					SurfaceBoundSlots,
					[BoundSkeletalComponent, After, AfterDynamic, AfterColor](
						const FAutomotiveBoundMaterialSlot& Bound)
					{
						if (!IsValid(Bound.Component)
							|| Bound.Component != BoundSkeletalComponent)
						{
							return false;
						}
						UMaterialInterface* BoundMaterial =
							Bound.Component->GetMaterial(Bound.MaterialIndex);
						UMaterialInstanceDynamic* BoundDynamic =
							Cast<UMaterialInstanceDynamic>(BoundMaterial);
						return BoundMaterial == After
							|| (IsValid(AfterDynamic)
								&& IsValid(BoundDynamic)
								&& BoundDynamic->K2_GetVectorParameterValue(
									TEXT("BaseColor")).Equals(AfterColor));
					}));
	}
	return true;
}

bool FAutomotiveMaterialBinderExhaustiveCoverageAutomationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UAutomotiveCatalogData* Catalog = LoadObject<UAutomotiveCatalogData>(
		nullptr,
		TEXT("/Game/SC01/DA_SC01Catalog.DA_SC01Catalog"));
	UAutomotiveMaterialLibrary* Library = LoadObject<UAutomotiveMaterialLibrary>(
		nullptr,
		TEXT("/Game/SC01/Materials/DA_SC01MaterialLibrary.DA_SC01MaterialLibrary"));
	TestNotNull(TEXT("穷举测试加载当前 Catalog"), Catalog);
	TestNotNull(TEXT("穷举测试加载当前材质库"), Library);
	if (Catalog == nullptr || Library == nullptr)
	{
		return false;
	}

	AutomotiveMaterialBinderAutomation::FCoverageSpec Coverage;
	TestTrue(
		TEXT("按 generateV2Coverage 同源规则解析 Runtime 覆盖集"),
		AutomotiveMaterialBinderAutomation::ParseCoverageSpec(
			Catalog->CatalogJson,
			Coverage));
	TestEqual(TEXT("可烘焙 option 数"), Coverage.BakeOptionIds.Num(), 168);
	TestEqual(
		TEXT("coverage 排除 color-picker 数"),
		Coverage.ExcludedColorPickerOptionCount,
		5);
	if (Coverage.BakeOptionIds.Num() != 168)
	{
		return false;
	}

	UAutomotiveConfigurationState* State =
		NewObject<UAutomotiveConfigurationState>(GetTransientPackage());
	AConfiguratorVehicleActor* Vehicle =
		NewObject<AConfiguratorVehicleActor>(GetTransientPackage());
	TestTrue(TEXT("穷举测试初始化 Runtime 状态"), State->Initialize(Catalog));
	TestTrue(
		TEXT("穷举测试 Bind 前配置骨骼车"),
		Vehicle->ConfigureAnimationFromCatalog(
			State->GetCatalogIndex().GetCatalog()));
	UAutomotiveMaterialBinder* Binder =
		NewObject<UAutomotiveMaterialBinder>(GetTransientPackage());
	TestTrue(TEXT("穷举测试绑定真实 AutomotiveMaterialBinder"), Binder->Bind(State, Library, Vehicle));
	if (!State->IsInitialized() || Binder->GetBoundSlotCount(TEXT("exterior-body-cover")) == 0)
	{
		AddError(Binder->GetLastError());
		return false;
	}

	TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents(Vehicle);
	int32 VisibleSkeletalComponentCount = 0;
	for (const USkeletalMeshComponent* Component : SkeletalComponents)
	{
		if (IsValid(Component)
			&& Component->IsVisible()
			&& !Component->bHiddenInGame)
		{
			++VisibleSkeletalComponentCount;
		}
	}
	TestEqual(
		TEXT("穷举车辆仅有一个可见 SkeletalMeshComponent"),
		VisibleSkeletalComponentCount,
		1);
	TSet<FName> UniqueSlots;
	TSet<int32> UniqueMaterialIndices;
	USkeletalMeshComponent* BoundSkeletalComponent = nullptr;
	for (const FString& SurfaceId :
		State->GetCatalogIndex().GetCatalog().SelectionOrder)
	{
		const TArray<FName>* ExpectedSlots =
			State->GetCatalogIndex().FindMaterialSlotIdsForSurface(SurfaceId);
		const int32 ExpectedSlotCount =
			ExpectedSlots != nullptr ? ExpectedSlots->Num() : 0;
		const TArray<FAutomotiveBoundMaterialSlot> BoundSlots =
			Binder->GetBoundSlots(SurfaceId);
		TestEqual(
			*FString::Printf(TEXT("%s 槽计数符合 binding"), *SurfaceId),
			Binder->GetBoundSlotCount(SurfaceId),
			ExpectedSlotCount);
		for (const FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
		{
			TestTrue(
				*FString::Printf(TEXT("%s 目标骨骼组件可见"), *Bound.SlotId.ToString()),
				IsValid(Bound.Component)
					&& Cast<USkeletalMeshComponent>(Bound.Component) != nullptr
					&& Bound.Component->IsVisible()
					&& !Bound.Component->bHiddenInGame);
			USkeletalMeshComponent* CurrentSkeletalComponent =
				Cast<USkeletalMeshComponent>(Bound.Component);
			if (BoundSkeletalComponent == nullptr)
			{
				BoundSkeletalComponent = CurrentSkeletalComponent;
			}
			TestTrue(
				*FString::Printf(TEXT("%s 复用同一个骨骼组件"), *SurfaceId),
				CurrentSkeletalComponent == BoundSkeletalComponent);
			TestFalse(
				*FString::Printf(TEXT("%s 槽名唯一"), *Bound.SlotId.ToString()),
				UniqueSlots.Contains(Bound.SlotId));
			UniqueSlots.Add(Bound.SlotId);
			TestFalse(
				*FString::Printf(TEXT("%s 材质索引唯一"), *Bound.SlotId.ToString()),
				UniqueMaterialIndices.Contains(Bound.MaterialIndex));
			UniqueMaterialIndices.Add(Bound.MaterialIndex);
		}
	}
	TestNotNull(TEXT("40 个 surface 使用单一可见 SkeletalMeshComponent"), BoundSkeletalComponent);
	TestEqual(TEXT("40 个 surface 使用 40 个唯一槽名"), UniqueSlots.Num(), 40);
	TestEqual(TEXT("40 个 surface 使用 40 个唯一材质索引"), UniqueMaterialIndices.Num(), 40);

	int32 AppliedOptionCount = 0;
	int32 FixedColorOptionCount = 0;
	int32 NeutralProxyOptionCount = 0;
	for (const FString& OptionId : Coverage.BakeOptionIds)
	{
		const AutomotiveCatalog::FOption* Option =
			State->GetCatalogIndex().FindOption(OptionId);
		TestNotNull(*FString::Printf(TEXT("%s 存在于 Runtime Catalog"), *OptionId), Option);
		if (Option == nullptr)
		{
			continue;
		}
		TestTrue(
			*FString::Printf(TEXT("%s 可建立不同前态"), *OptionId),
			AutomotiveMaterialBinderAutomation::PrimeDifferentOption(
				Binder,
				State,
				*Option));

		TMap<FString, FString> Selections = State->GetSelections();
		TMap<FString, FAutomotiveCustomization> Customizations =
			State->GetCustomizations();
		Selections.Add(Option->SurfaceId, OptionId);
		Customizations.Remove(Option->SurfaceId);
		const FAutomotiveMaterialTransactionResult Result =
			Binder->ApplyTransaction(Selections, Customizations);
		TestTrue(*FString::Printf(TEXT("%s 事务成功"), *OptionId), Result.bSuccess);
		TestEqual(
			*FString::Printf(TEXT("%s 返回 APPLIED"), *OptionId),
			Result.Code,
			FString(TEXT("APPLIED")));
		TestTrue(
			*FString::Printf(TEXT("%s unsupported 为空"), *OptionId),
			Result.UnsupportedSurfaceIds.IsEmpty());
		TestTrue(
			*FString::Printf(TEXT("%s 回执只包含目标 surface"), *OptionId),
			Result.AppliedSurfaceIds
				== TArray<FString>({Option->SurfaceId}));
		const TArray<FName>* ExpectedReceiptSlots =
			State->GetCatalogIndex().FindMaterialSlotIdsForSurface(
				Option->SurfaceId);
		TestTrue(
			*FString::Printf(TEXT("%s 回执包含全部目标槽"), *OptionId),
			ExpectedReceiptSlots != nullptr
				&& Result.AppliedSlotIds == *ExpectedReceiptSlots);

		const TArray<FAutomotiveBoundMaterialSlot> BoundSlots =
			Binder->GetBoundSlots(Option->SurfaceId);
		const int32 ExpectedSlotCount =
			ExpectedReceiptSlots != nullptr ? ExpectedReceiptSlots->Num() : 0;
		TestTrue(
			*FString::Printf(TEXT("%s 应用后槽数符合 binding"), *OptionId),
			BoundSlots.Num() == ExpectedSlotCount);
		UMaterialInterface* Applied =
			Binder->GetAppliedMaterialForSurface(Option->SurfaceId);
		TestTrue(
			*FString::Printf(TEXT("%s 全部目标槽均可见且已应用材质"), *OptionId),
			IsValid(Applied)
				&& Algo::AllOf(
					BoundSlots,
					[](const FAutomotiveBoundMaterialSlot& Bound)
					{
						return IsValid(Bound.Component)
							&& Bound.Component->IsVisible()
							&& !Bound.Component->bHiddenInGame
							&& IsValid(Bound.Component->GetMaterial(
								Bound.MaterialIndex));
					}));
		UMaterialInstanceDynamic* Dynamic =
			Cast<UMaterialInstanceDynamic>(Applied);
		const FString FamilyId = Option->MaterialFamilyId.Get(TEXT("paint"));
		UMaterialInterface* ExpectedParent =
			Library->LoadInteriorMaterial(FamilyId);
		TestTrue(
			*FString::Printf(TEXT("%s 全部目标槽使用正确材料族 MID"), *OptionId),
			IsValid(ExpectedParent)
				&& Algo::AllOf(
					BoundSlots,
					[ExpectedParent](const FAutomotiveBoundMaterialSlot& Bound)
					{
						UMaterialInstanceDynamic* BoundDynamic =
							Cast<UMaterialInstanceDynamic>(
								Bound.Component->GetMaterial(Bound.MaterialIndex));
						return IsValid(BoundDynamic)
							&& BoundDynamic->IsChildOf(ExpectedParent);
					}));

		const TOptional<FLinearColor> ExpectedColor =
			AutomotiveMaterialBinderAutomation::ResolveExpectedOptionColor(*Option);
		const FLinearColor ActualColor = IsValid(Dynamic)
			? Dynamic->K2_GetVectorParameterValue(TEXT("BaseColor"))
			: FLinearColor::Transparent;
		if (ExpectedColor.IsSet())
		{
			++FixedColorOptionCount;
			TestTrue(
				*FString::Printf(
					TEXT("%s 固定色遵循 ColorCode/DisplayColorHex"),
					*OptionId),
				IsValid(Dynamic)
					&& ActualColor.Equals(ExpectedColor.GetValue(), 0.001f)
					&& Algo::AllOf(
						BoundSlots,
						[&ExpectedColor](const FAutomotiveBoundMaterialSlot& Bound)
						{
							UMaterialInstanceDynamic* BoundDynamic =
								Cast<UMaterialInstanceDynamic>(
									Bound.Component->GetMaterial(Bound.MaterialIndex));
							return IsValid(BoundDynamic)
								&& BoundDynamic->K2_GetVectorParameterValue(
									TEXT("BaseColor")).Equals(
										ExpectedColor.GetValue(),
										0.001f);
						}));
		}
		else
		{
			++NeutralProxyOptionCount;
			TestTrue(
				*FString::Printf(TEXT("%s 无色代理为灰阶"), *OptionId),
				IsValid(Dynamic)
					&& FMath::IsNearlyEqual(ActualColor.R, ActualColor.G)
					&& FMath::IsNearlyEqual(ActualColor.G, ActualColor.B)
					&& Algo::AllOf(
						BoundSlots,
						[](const FAutomotiveBoundMaterialSlot& Bound)
						{
							UMaterialInstanceDynamic* BoundDynamic =
								Cast<UMaterialInstanceDynamic>(
									Bound.Component->GetMaterial(Bound.MaterialIndex));
							if (!IsValid(BoundDynamic))
							{
								return false;
							}
							const FLinearColor Color =
								BoundDynamic->K2_GetVectorParameterValue(
									TEXT("BaseColor"));
							return FMath::IsNearlyEqual(Color.R, Color.G)
								&& FMath::IsNearlyEqual(Color.G, Color.B);
						}));
		}
		if (Result.bSuccess
			&& Result.Code == TEXT("APPLIED")
			&& Result.UnsupportedSurfaceIds.IsEmpty())
		{
			++AppliedOptionCount;
		}
	}

	int32 AppliedVariantCount = 0;
	const AutomotiveCatalog::FCatalog& RuntimeCatalog =
		State->GetCatalogIndex().GetCatalog();
	TestEqual(TEXT("Runtime Catalog material variant 数"), RuntimeCatalog.MaterialVariants.Num(), 352);
	TestEqual(TEXT("材质库物化 variant 数"), Library->Variants.Num(), 352);
	for (const AutomotiveCatalog::FMaterialVariant& Variant :
		RuntimeCatalog.MaterialVariants)
	{
		const FString* OptionId =
			Coverage.VariantOptionByFamily.Find(Variant.MaterialFamilyId);
		TestNotNull(
			*FString::Printf(
				TEXT("%s 材料族存在 coverage option"),
				*Variant.VariantId),
			OptionId);
		if (OptionId == nullptr)
		{
			continue;
		}
		const AutomotiveCatalog::FOption* Option =
			State->GetCatalogIndex().FindOption(*OptionId);
		if (Option == nullptr)
		{
			AddError(Variant.VariantId + TEXT(" 的 coverage option 不存在"));
			continue;
		}
		TMap<FString, FString> Selections = State->GetSelections();
		TMap<FString, FAutomotiveCustomization> Customizations =
			State->GetCustomizations();
		Selections.Add(Option->SurfaceId, *OptionId);
		FAutomotiveCustomization Customization;
		Customization.Kind = EAutomotiveCustomizationKind::MaterialVariant;
		Customization.MaterialVariantId = Variant.VariantId;
		Customizations.Add(Option->SurfaceId, Customization);
		const FAutomotiveMaterialTransactionResult Result =
			Binder->ApplyTransaction(Selections, Customizations);
		TestTrue(
			*FString::Printf(TEXT("%s variant 事务成功"), *Variant.VariantId),
			Result.bSuccess);
		TestEqual(
			*FString::Printf(TEXT("%s variant 返回 APPLIED"), *Variant.VariantId),
			Result.Code,
			FString(TEXT("APPLIED")));
		TestTrue(
			*FString::Printf(TEXT("%s variant unsupported 为空"), *Variant.VariantId),
			Result.UnsupportedSurfaceIds.IsEmpty());
		TestTrue(
			*FString::Printf(TEXT("%s variant 回执只包含目标 surface"), *Variant.VariantId),
			Result.AppliedSurfaceIds
				== TArray<FString>({Option->SurfaceId}));
		const TArray<FName>* ExpectedReceiptSlots =
			State->GetCatalogIndex().FindMaterialSlotIdsForSurface(
				Option->SurfaceId);
		TestTrue(
			*FString::Printf(TEXT("%s variant 回执包含全部目标槽"), *Variant.VariantId),
			ExpectedReceiptSlots != nullptr
				&& Result.AppliedSlotIds == *ExpectedReceiptSlots);

		const TArray<FAutomotiveBoundMaterialSlot> BoundSlots =
			Binder->GetBoundSlots(Option->SurfaceId);
		const int32 ExpectedSlotCount =
			ExpectedReceiptSlots != nullptr ? ExpectedReceiptSlots->Num() : 0;
		UMaterialInterface* Expected =
			Library->LoadVariantMaterial(Variant.VariantId);
		UMaterialInterface* Applied =
			Binder->GetAppliedMaterialForSurface(Option->SurfaceId);
		UMaterialInstance* ExpectedInstance = Cast<UMaterialInstance>(Expected);
		TestTrue(
			*FString::Printf(TEXT("%s variant 全部目标可见"), *Variant.VariantId),
			BoundSlots.Num() == ExpectedSlotCount
				&& Algo::AllOf(
					BoundSlots,
					[](const FAutomotiveBoundMaterialSlot& Bound)
					{
						return IsValid(Bound.Component)
							&& Bound.Component->IsVisible()
							&& !Bound.Component->bHiddenInGame;
					}));
		TestTrue(
			*FString::Printf(TEXT("%s 精确应用已物化 MI"), *Variant.VariantId),
			IsValid(Expected)
				&& Applied == Expected
				&& Algo::AllOf(
					BoundSlots,
					[Expected](const FAutomotiveBoundMaterialSlot& Bound)
					{
						return IsValid(Bound.Component)
							&& Bound.Component->GetMaterial(Bound.MaterialIndex)
								== Expected;
					})
				&& IsValid(ExpectedInstance)
				&& ExpectedInstance->IsChildOf(
					Library->LoadInteriorMaterial(Variant.MaterialFamilyId)));
		TestTrue(
			*FString::Printf(TEXT("%s 保留 Catalog 固定色"), *Variant.VariantId),
			Variant.ColorCode.IsSet() || Variant.DisplayColorHex.IsSet());
		if (Result.bSuccess
			&& Result.Code == TEXT("APPLIED")
			&& Result.UnsupportedSurfaceIds.IsEmpty()
			&& Applied == Expected)
		{
			++AppliedVariantCount;
		}
	}

	TestEqual(TEXT("168 个可烘焙 option 全部通过 Binder"), AppliedOptionCount, 168);
	TestEqual(TEXT("可解析固定色 option 统计"), FixedColorOptionCount, 30);
	TestEqual(TEXT("无可解析色值的灰阶代理 option 统计"), NeutralProxyOptionCount, 138);
	TestEqual(TEXT("352 个 material variant 全部通过 Binder"), AppliedVariantCount, 352);
	AddInfo(FString::Printf(
		TEXT("Runtime 穷举统计：option=%d（固定色=%d，灰阶代理=%d），"
			"materialVariant=%d，surface=%d，unsupported=0"),
		AppliedOptionCount,
		FixedColorOptionCount,
		NeutralProxyOptionCount,
		AppliedVariantCount,
		UniqueSlots.Num()));
	return true;
}

#endif
