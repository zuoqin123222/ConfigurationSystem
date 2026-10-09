#include "AutomotiveMaterialBinder.h"

#include "Components/MeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Crc.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "AutomotiveMaterialLibrary.h"
#include "AutomotiveConfigurationState.h"

const FName UAutomotiveMaterialBinder::PaintProxySlotTag(
	TEXT("Configurator.Slot.paint_body"));
const FName UAutomotiveMaterialBinder::InteriorProxySlotTag(
	TEXT("Configurator.Slot.automotive_interior_material_proxy"));
const FString UAutomotiveMaterialBinder::PaintSurfaceId(TEXT("exterior-body-cover"));
const FString UAutomotiveMaterialBinder::InteriorProxySurfaceId(TEXT("door-middle"));
const FString UAutomotiveMaterialBinder::WheelMaterialSurfaceId(TEXT("wheel-material"));
const FString UAutomotiveMaterialBinder::WheelColorSurfaceId(TEXT("wheel-color"));

FName UAutomotiveMaterialBinder::MakeProxyTargetTag(const FName SlotId)
{
	return FName(*FString::Printf(
		TEXT("Configurator.ProxySurfaceTarget.%s"),
		*SlotId.ToString()));
}

FName UAutomotiveMaterialBinder::MakeProxyMaterialSlotTag(
	const FName MaterialSlotId)
{
	return FName(*FString::Printf(
		TEXT("Configurator.ProxyMaterialSlot.%s"),
		*MaterialSlotId.ToString()));
}

namespace
{
	const FString ProxyMaterialSlotTagPrefix(
		TEXT("Configurator.ProxyMaterialSlot."));

	int32 ResolveProxyMaterialIndex(
		UMeshComponent* Proxy,
		const FName SemanticSlotId,
		FString& OutError)
	{
		const int32 SemanticIndex = Proxy->GetMaterialIndex(SemanticSlotId);
		if (SemanticIndex != INDEX_NONE)
		{
			return SemanticIndex;
		}

		FName ExplicitMaterialSlot;
		for (const FName Tag : Proxy->ComponentTags)
		{
			const FString TagValue = Tag.ToString();
			if (!TagValue.StartsWith(ProxyMaterialSlotTagPrefix))
			{
				continue;
			}
			const FName Candidate(
				*TagValue.Mid(ProxyMaterialSlotTagPrefix.Len()));
			if (!ExplicitMaterialSlot.IsNone()
				&& ExplicitMaterialSlot != Candidate)
			{
				OutError = FString::Printf(
					TEXT("代理组件 %s 声明了多个真实材质槽。"),
					*Proxy->GetName());
				return INDEX_NONE;
			}
			ExplicitMaterialSlot = Candidate;
		}
		if (!ExplicitMaterialSlot.IsNone())
		{
			const int32 ExplicitIndex =
				Proxy->GetMaterialIndex(ExplicitMaterialSlot);
			if (ExplicitIndex == INDEX_NONE)
			{
				OutError = FString::Printf(
					TEXT("代理组件 %s 不包含声明的真实材质槽 %s。"),
					*Proxy->GetName(),
					*ExplicitMaterialSlot.ToString());
			}
			return ExplicitIndex;
		}
		if (Proxy->GetNumMaterials() == 1)
		{
			return 0;
		}
		OutError = FString::Printf(
			TEXT("代理组件 %s 包含 %d 个材质槽，但没有为 %s 声明唯一真实槽。"),
			*Proxy->GetName(),
			Proxy->GetNumMaterials(),
			*SemanticSlotId.ToString());
		return INDEX_NONE;
	}

	TOptional<FLinearColor> ResolveCatalogColor(
		const FString& ColorCode)
	{
		if (ColorCode.Equals(TEXT("red"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#A61D24")));
		}
		if (ColorCode.Equals(TEXT("silver"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#BFC3C7")));
		}
		if (ColorCode.Equals(TEXT("bright-silver"), ESearchCase::IgnoreCase))
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#D4D7D9")));
		}
		const FString Hex = ColorCode.StartsWith(TEXT("#"))
			? ColorCode.Mid(1)
			: ColorCode;
		bool bIsHex = Hex.Len() == 6 || Hex.Len() == 8;
		for (const TCHAR Character : Hex)
		{
			bIsHex = bIsHex && FChar::IsHexDigit(Character);
		}
		if (bIsHex)
		{
			return FLinearColor::FromSRGBColor(FColor::FromHex(ColorCode));
		}
		return TOptional<FLinearColor>();
	}

	TOptional<FLinearColor> ResolveFixedOptionColor(
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
		if (Option.DisplayColorHex.IsSet())
		{
			return ResolveCatalogColor(Option.DisplayColorHex.GetValue());
		}
		return TOptional<FLinearColor>();
	}

	FLinearColor ResolveNeutralProxyColor(const FString& OptionId)
	{
		// 无固定色的结构/样式代理只在中性灰阶内产生稳定区分，避免把占位效果
		// 误呈现为彩色设计；实际质感仍来自所选材料族母材质。
		const uint32 Hash = FCrc::StrCrc32(*OptionId);
		const uint8 Shade = static_cast<uint8>(88u + Hash % 81u);
		return FLinearColor::FromSRGBColor(FColor(Shade, Shade, Shade));
	}

	UTexture2D* UpdateDynamicColorTexture(
		UTexture2D* Existing,
		const FLinearColor& Color)
	{
		UTexture2D* Texture = Existing;
		if (!IsValid(Texture))
		{
			Texture = UTexture2D::CreateTransient(
				1,
				1,
				PF_B8G8R8A8,
				TEXT("SC01_RuntimeColor"));
			if (!IsValid(Texture))
			{
				return nullptr;
			}
			Texture->SRGB = true;
			Texture->NeverStream = true;
		}
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixel = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		*Pixel = Color.ToFColorSRGB();
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}
}

FString FAutomotiveMaterialTransactionResult::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), bSuccess);
	Root->SetStringField(TEXT("code"), Code);
	Root->SetStringField(TEXT("message"), Message);
	Root->SetStringField(TEXT("configurationId"), ConfigurationId);
	TArray<TSharedPtr<FJsonValue>> AppliedSurfaces;
	for (const FString& SurfaceId : AppliedSurfaceIds)
	{
		AppliedSurfaces.Add(MakeShared<FJsonValueString>(SurfaceId));
	}
	Root->SetArrayField(TEXT("appliedSurfaceIds"), MoveTemp(AppliedSurfaces));
	TArray<TSharedPtr<FJsonValue>> UnsupportedSurfaces;
	for (const FString& SurfaceId : UnsupportedSurfaceIds)
	{
		UnsupportedSurfaces.Add(MakeShared<FJsonValueString>(SurfaceId));
	}
	Root->SetArrayField(TEXT("unsupportedSurfaceIds"), MoveTemp(UnsupportedSurfaces));
	TArray<TSharedPtr<FJsonValue>> AppliedSlots;
	for (const FName SlotId : AppliedSlotIds)
	{
		AppliedSlots.Add(MakeShared<FJsonValueString>(SlotId.ToString()));
	}
	Root->SetArrayField(TEXT("appliedSlotIds"), MoveTemp(AppliedSlots));
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	return Json;
}

UMeshComponent* UAutomotiveMaterialBinder::FindUniqueTaggedMesh(
	AActor* Vehicle,
	const FName SlotTag,
	FString& OutError)
{
	TInlineComponentArray<UMeshComponent*> Meshes(Vehicle);
	UMeshComponent* Match = nullptr;
	for (UMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->ComponentHasTag(SlotTag))
		{
			continue;
		}
		if (Match != nullptr)
		{
			OutError = FString::Printf(
				TEXT("代理槽 %s 必须且只能绑定一个 MeshComponent。"),
				*SlotTag.ToString());
			return nullptr;
		}
		Match = Mesh;
	}
	if (Match == nullptr)
	{
		OutError = FString::Printf(
			TEXT("未找到代理槽 %s。"),
			*SlotTag.ToString());
	}
	return Match;
}

bool UAutomotiveMaterialBinder::BuildBoundSlots(AActor* Vehicle)
{
	BoundSlots.Reset();
	PaintComponent = nullptr;
	InteriorComponent = nullptr;
	TInlineComponentArray<UMeshComponent*> Meshes(Vehicle);
	const AutomotiveCatalog::FCatalog& Catalog =
		State->GetCatalogIndex().GetCatalog();
	for (const AutomotiveCatalog::FSurfaceBinding& Binding :
		Catalog.VehicleSurfaceBinding.Bindings)
	{
		for (const FName SlotId : Binding.MaterialSlotIds)
		{
			UMeshComponent* Match = nullptr;
			int32 MatchIndex = INDEX_NONE;
			FString ProxyError;
			UMeshComponent* Proxy = FindUniqueTaggedMesh(
				Vehicle,
				MakeProxyTargetTag(SlotId),
				ProxyError);
			FString ProxyMaterialError;
			if (IsValid(Proxy) && Proxy->IsVisible() && !Proxy->bHiddenInGame)
			{
				MatchIndex = ResolveProxyMaterialIndex(
					Proxy,
					SlotId,
					ProxyMaterialError);
				if (MatchIndex != INDEX_NONE)
				{
					Match = Proxy;
				}
			}
			for (UMeshComponent* Mesh : Meshes)
			{
				if (Match != nullptr)
				{
					break;
				}
				if (!IsValid(Mesh) || !Mesh->IsVisible() || Mesh->bHiddenInGame)
				{
					continue;
				}
				const int32 MaterialIndex = Mesh->GetMaterialIndex(SlotId);
				if (MaterialIndex != INDEX_NONE)
				{
					if (Match != nullptr)
					{
						SetFailure(
							TEXT("DUPLICATE_RUNTIME_MATERIAL_SLOT"),
							FString::Printf(
								TEXT("命名槽 %s 在多个可见 MeshComponent 上重复。"),
								*SlotId.ToString()));
						return false;
					}
					Match = Mesh;
					MatchIndex = MaterialIndex;
				}
			}

			// A5 独立静态分件通过唯一 ComponentTag 暴露代理目标；正式车辆仍可
			// 直接使用真实命名材质槽。两种路径都禁止一个目标被多个 surface 复用。
			if (Match == nullptr || MatchIndex == INDEX_NONE)
			{
				SetFailure(
					ProxyMaterialError.IsEmpty()
						? TEXT("RUNTIME_MATERIAL_SLOT_MISSING")
						: TEXT("RUNTIME_PROXY_MATERIAL_SLOT_AMBIGUOUS"),
					ProxyMaterialError.IsEmpty()
						? FString::Printf(
							TEXT("surfaceId=%s 的命名槽 %s 在当前车辆上不存在。"),
							*Binding.SurfaceId,
							*SlotId.ToString())
						: ProxyMaterialError);
				return false;
			}

			FAutomotiveBoundMaterialSlot& Bound = BoundSlots.AddDefaulted_GetRef();
			Bound.SurfaceId = Binding.SurfaceId;
			Bound.SlotId = SlotId;
			Bound.Component = Match;
			Bound.MaterialIndex = MatchIndex;
			Bound.OriginalMaterial = Match->GetMaterial(MatchIndex);
			if (Binding.SurfaceId == PaintSurfaceId && PaintComponent == nullptr)
			{
				PaintComponent = Match;
			}
			if (Binding.SurfaceId == InteriorProxySurfaceId && InteriorComponent == nullptr)
			{
				InteriorComponent = Match;
			}
		}
	}
	return !BoundSlots.IsEmpty();
}

bool UAutomotiveMaterialBinder::Bind(
	UAutomotiveConfigurationState* InState,
	UAutomotiveMaterialLibrary* InLibrary,
	AActor* InVehicle)
{
	Unbind();
	if (!IsValid(InState) || !InState->IsInitialized()
		|| !IsValid(InLibrary) || !IsValid(InVehicle))
	{
		LastError = TEXT("状态、材质库或车辆无效。");
		return false;
	}

	State = InState;
	Library = InLibrary;
	if (!BuildBoundSlots(InVehicle))
	{
		State = nullptr;
		Library = nullptr;
		return false;
	}
	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("AutomotiveMaterialBinder bound %d material slots"),
		BoundSlots.Num());
	State->OnChangedNative.AddUObject(this, &UAutomotiveMaterialBinder::HandleStateChanged);
	if (!ApplyCurrentConfiguration())
	{
		const FString BindError = LastError;
		Unbind();
		LastError = BindError;
		return false;
	}
	return true;
}

void UAutomotiveMaterialBinder::Unbind()
{
	if (IsValid(State))
	{
		State->OnChangedNative.RemoveAll(this);
	}
	for (FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		if (IsValid(Bound.Component) && Bound.MaterialIndex != INDEX_NONE)
		{
			Bound.Component->SetMaterial(Bound.MaterialIndex, Bound.OriginalMaterial);
		}
	}
	State = nullptr;
	Library = nullptr;
	PaintComponent = nullptr;
	InteriorComponent = nullptr;
	PaintMaterialInstance = nullptr;
	InteriorMaterialInstance = nullptr;
	BoundSlots.Reset();
	AppliedInteriorFamilyId.Reset();
	LastError.Reset();
	LastTransactionResult = FAutomotiveMaterialTransactionResult();
	bApplyingTransaction = false;
}

void UAutomotiveMaterialBinder::BeginDestroy()
{
	Unbind();
	Super::BeginDestroy();
}

void UAutomotiveMaterialBinder::HandleStateChanged()
{
	if (!bApplyingTransaction)
	{
		ApplyCurrentConfiguration();
	}
}

bool UAutomotiveMaterialBinder::ApplyCurrentConfiguration()
{
	if (!IsValid(State) || !IsValid(Library) || BoundSlots.IsEmpty())
	{
		LastError = TEXT("Binder 尚未完成有效绑定。");
		return false;
	}

	LastError.Reset();
	const TMap<FString, FString> Selections = State->GetSelections();
	const TMap<FString, FAutomotiveCustomization> Customizations =
		State->GetCustomizations();
	for (const AutomotiveCatalog::FSurfaceBinding& Binding :
		State->GetCatalogIndex().GetCatalog().VehicleSurfaceBinding.Bindings)
	{
		if (!ApplySurface(Binding.SurfaceId, Selections, Customizations))
		{
			return false;
		}
	}
	return true;
}

void UAutomotiveMaterialBinder::SetFailure(
	const FString& Code,
	const FString& Message)
{
	LastError = Message;
	LastTransactionResult = FAutomotiveMaterialTransactionResult();
	LastTransactionResult.Code = Code;
	LastTransactionResult.Message = Message;
	LastTransactionResult.ConfigurationId =
		IsValid(State) ? State->GetConfigurationId() : FString();
}

int32 UAutomotiveMaterialBinder::GetBoundSlotCount(
	const FString& SurfaceId) const
{
	int32 Count = 0;
	for (const FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		Count += Bound.SurfaceId == SurfaceId ? 1 : 0;
	}
	return Count;
}

TArray<FAutomotiveBoundMaterialSlot> UAutomotiveMaterialBinder::GetBoundSlots(
	const FString& SurfaceId) const
{
	TArray<FAutomotiveBoundMaterialSlot> Result;
	for (const FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		if (Bound.SurfaceId == SurfaceId)
		{
			Result.Add(Bound);
		}
	}
	return Result;
}

bool UAutomotiveMaterialBinder::GetSingleBoundSlot(
	const FString& SurfaceId,
	UMeshComponent*& OutComponent,
	FName& OutSlotId,
	int32& OutMaterialIndex) const
{
	OutComponent = nullptr;
	OutSlotId = NAME_None;
	OutMaterialIndex = INDEX_NONE;
	for (const FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		if (Bound.SurfaceId != SurfaceId)
		{
			continue;
		}
		if (OutComponent != nullptr)
		{
			OutComponent = nullptr;
			OutSlotId = NAME_None;
			OutMaterialIndex = INDEX_NONE;
			return false;
		}
		OutComponent = Bound.Component;
		OutSlotId = Bound.SlotId;
		OutMaterialIndex = Bound.MaterialIndex;
	}
	return IsValid(OutComponent)
		&& !OutSlotId.IsNone()
		&& OutMaterialIndex != INDEX_NONE;
}

UMaterialInterface* UAutomotiveMaterialBinder::GetAppliedMaterialForSurface(
	const FString& SurfaceId) const
{
	const FString& AppliedSurfaceId = SurfaceId == WheelColorSurfaceId
		? WheelMaterialSurfaceId
		: SurfaceId;
	for (const FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		if (Bound.SurfaceId == AppliedSurfaceId
			&& IsValid(Bound.Component)
			&& Bound.MaterialIndex != INDEX_NONE)
		{
			return Bound.Component->GetMaterial(Bound.MaterialIndex);
		}
	}
	return nullptr;
}

FString UAutomotiveMaterialBinder::GetLastTransactionResultJson() const
{
	return LastTransactionResult.ToJson();
}

bool UAutomotiveMaterialBinder::ResolveWheelColor(
	const TMap<FString, FString>& Selections,
	FLinearColor& OutColor,
	FString& OutErrorCode,
	FString& OutErrorMessage) const
{
	OutColor = FLinearColor::White;
	OutErrorCode.Reset();
	OutErrorMessage.Reset();
	const FString* OptionId = Selections.Find(WheelColorSurfaceId);
	const AutomotiveCatalog::FOption* Option = OptionId != nullptr
		? State->GetCatalogIndex().FindOption(*OptionId)
		: nullptr;
	if (Option == nullptr)
	{
		OutErrorCode = TEXT("WHEEL_COLOR_UNRESOLVED");
		OutErrorMessage = TEXT("wheel-color 缺少有效 option。");
		return false;
	}
	const TOptional<FLinearColor> Color = ResolveFixedOptionColor(*Option);
	if (!Color.IsSet())
	{
		OutErrorCode = TEXT("WHEEL_COLOR_UNRESOLVED");
		OutErrorMessage = FString::Printf(
			TEXT("wheel-color optionId=%s 缺少可解析固定色。"),
			**OptionId);
		return false;
	}
	OutColor = Color.GetValue();
	return true;
}

bool UAutomotiveMaterialBinder::ApplyWheelColorOverlay(
	const TMap<FString, FString>& Selections,
	TArray<FName>* OutAppliedSlots)
{
	FLinearColor Color;
	FString ErrorCode;
	FString ErrorMessage;
	if (!ResolveWheelColor(Selections, Color, ErrorCode, ErrorMessage))
	{
		SetFailure(ErrorCode, ErrorMessage);
		return false;
	}

	bool bFoundTarget = false;
	for (FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		if (Bound.SurfaceId != WheelMaterialSurfaceId)
		{
			continue;
		}
		bFoundTarget = true;
		UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(
			IsValid(Bound.Component) && Bound.MaterialIndex != INDEX_NONE
				? Bound.Component->GetMaterial(Bound.MaterialIndex)
				: nullptr);
		if (!IsValid(Dynamic))
		{
			SetFailure(
				TEXT("WHEEL_MATERIAL_MID_UNAVAILABLE"),
				TEXT("wheel-color 只能叠加到 wheel-material 当前 MID。"));
			return false;
		}
		Dynamic->SetVectorParameterValue(TEXT("Metallic Color A"), Color);
		Dynamic->SetVectorParameterValue(TEXT("Metallic Color B"), Color);
		if (OutAppliedSlots != nullptr)
		{
			OutAppliedSlots->Add(Bound.SlotId);
		}
	}
	if (!bFoundTarget)
	{
		SetFailure(
			TEXT("WHEEL_MATERIAL_TARGET_MISSING"),
			TEXT("wheel-color 未找到 wheel-material 运行时目标。"));
		return false;
	}
	return true;
}

bool UAutomotiveMaterialBinder::ResolveSurfaceMaterial(
	const FString& SurfaceId,
	const TMap<FString, FString>& Selections,
	const TMap<FString, FAutomotiveCustomization>& Customizations,
	UMaterialInterface*& OutMaterial,
	bool& bOutUseDynamic,
	FLinearColor& OutColor,
	FAutomotivePaintCustomization& OutPaint,
	bool& bOutHasPaintParameters,
	FString& OutFamilyId,
	FString& OutErrorCode,
	FString& OutErrorMessage) const
{
	OutMaterial = nullptr;
	bOutUseDynamic = false;
	OutColor = FLinearColor::White;
	OutPaint = FAutomotivePaintCustomization();
	bOutHasPaintParameters = false;
	OutFamilyId.Reset();
	OutErrorCode.Reset();
	OutErrorMessage.Reset();

	const FString* OptionId = Selections.Find(SurfaceId);
	if (OptionId == nullptr)
	{
		return true;
	}
	const AutomotiveCatalog::FOption* Option =
		State->GetCatalogIndex().FindOption(*OptionId);
	if (Option == nullptr)
	{
		OutErrorCode = TEXT("MATERIAL_FAMILY_UNRESOLVED");
		OutErrorMessage = FString::Printf(
			TEXT("surfaceId=%s 的 optionId=%s 不存在。"),
			*SurfaceId,
			**OptionId);
		return false;
	}
	// 代理车仍需让 wheel style、铭牌等非材质语义项目可见切换。它们使用
	// 项目自建 paint parent 和稳定 optionId 色值，只表达“发生了变化”，
	// 不把 A5 代理分件或颜色伪称为正式 SC01 效果。
	OutFamilyId = Option->MaterialFamilyId.Get(TEXT("paint"));

	const FAutomotiveCustomization* Customization =
		Customizations.Find(SurfaceId);
	if (Customization != nullptr
		&& Customization->Kind == EAutomotiveCustomizationKind::MaterialVariant)
	{
		const AutomotiveCatalog::FMaterialVariant* Variant =
			State->GetCatalogIndex().FindMaterialVariant(
				Customization->MaterialVariantId);
		OutMaterial = Library->LoadVariantMaterial(
			Customization->MaterialVariantId);
		if (Variant == nullptr || !IsValid(OutMaterial))
		{
			OutErrorCode = TEXT("MATERIAL_VARIANT_ASSET_MISSING");
			OutErrorMessage = FString::Printf(
				TEXT("surfaceId=%s 的 variantId=%s 未命中已物化材质实例。"),
				*SurfaceId,
				*Customization->MaterialVariantId);
			return false;
		}
		OutFamilyId = Variant->MaterialFamilyId;
		return true;
	}

	OutMaterial = Library->LoadInteriorMaterial(OutFamilyId);
	if (!IsValid(OutMaterial))
	{
		OutErrorCode = TEXT("MATERIAL_FAMILY_ASSET_MISSING");
		OutErrorMessage = FString::Printf(
			TEXT("surfaceId=%s 的 materialFamilyId=%s 未命中材质库。"),
			*SurfaceId,
			*OutFamilyId);
		return false;
	}

	if (Customization != nullptr
		&& Customization->Kind == EAutomotiveCustomizationKind::Paint)
	{
		bOutUseDynamic = true;
		bOutHasPaintParameters = true;
		OutPaint = Customization->Paint;
		OutColor = FLinearColor::FromSRGBColor(
			FColor::FromHex(Customization->Paint.ColorHex));
		return true;
	}
	const TOptional<FLinearColor> FixedColor = ResolveFixedOptionColor(*Option);
	if (FixedColor.IsSet())
	{
		OutColor = FixedColor.GetValue();
		bOutUseDynamic = true;
		if (OutFamilyId == TEXT("paint"))
		{
			bOutHasPaintParameters = true;
			OutPaint.ColorHex = Option->ColorCode.Get(
				Option->DisplayColorHex.Get(TEXT("#808080")));
			OutPaint.Metallic = Option->ColorCode.Get(FString()).Equals(
				TEXT("silver"), ESearchCase::IgnoreCase) ? 0.8 : 0.35;
			OutPaint.Roughness = 0.22;
			OutPaint.ClearCoat = 0.85;
			OutPaint.OrangePeel = 0.12;
			OutPaint.FlakeIntensity = 0.25;
		}
	}
	else
	{
		bOutUseDynamic = true;
		OutColor = ResolveNeutralProxyColor(Option->OptionId);
	}
	return true;
}

bool UAutomotiveMaterialBinder::ApplySurface(
	const FString& SurfaceId,
	const TMap<FString, FString>& Selections,
	const TMap<FString, FAutomotiveCustomization>& Customizations,
	TArray<FName>* OutAppliedSlots)
{
	if (SurfaceId == WheelColorSurfaceId)
	{
		return ApplyWheelColorOverlay(Selections, OutAppliedSlots);
	}

	UMaterialInterface* Material = nullptr;
	bool bUseDynamic = false;
	FLinearColor Color;
	FAutomotivePaintCustomization Paint;
	bool bHasPaintParameters = false;
	FString FamilyId;
	FString ErrorCode;
	FString ErrorMessage;
	if (!ResolveSurfaceMaterial(
		SurfaceId,
		Selections,
		Customizations,
		Material,
		bUseDynamic,
		Color,
		Paint,
		bHasPaintParameters,
		FamilyId,
		ErrorCode,
		ErrorMessage))
	{
		SetFailure(ErrorCode, ErrorMessage);
		return false;
	}

	bool bFoundTarget = false;
	for (FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		if (Bound.SurfaceId != SurfaceId)
		{
			continue;
		}
		bFoundTarget = true;
		if (!IsValid(Bound.Component) || Bound.MaterialIndex == INDEX_NONE)
		{
			SetFailure(
				TEXT("RUNTIME_MATERIAL_SLOT_INVALID"),
				FString::Printf(TEXT("surfaceId=%s 的运行时槽已失效。"), *SurfaceId));
			return false;
		}
		UMaterialInterface* AppliedMaterial = Material;
		if (!IsValid(Material))
		{
			AppliedMaterial = Bound.OriginalMaterial;
		}
		else if (bUseDynamic)
		{
			if (!IsValid(Bound.DynamicInstance)
				|| Bound.DynamicParent != Material)
			{
				Bound.DynamicInstance =
					UMaterialInstanceDynamic::Create(Material, this);
				Bound.DynamicParent = Material;
			}
			if (!IsValid(Bound.DynamicInstance))
			{
				SetFailure(
					TEXT("DYNAMIC_MATERIAL_CREATE_FAILED"),
					FString::Printf(
						TEXT("surfaceId=%s 的槽 %s 无法创建 MID。"),
						*SurfaceId,
						*Bound.SlotId.ToString()));
				return false;
			}
			Bound.DynamicInstance->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Bound.DynamicInstance->SetVectorParameterValue(TEXT("Color"), Color);
			// SubstrateMaterials 的车漆母材质以 Tint 作为主体可见颜色；
			// BaseColor 只用于兼容 AutomotiveMats 和其他材料族。
			Bound.DynamicInstance->SetVectorParameterValue(TEXT("Tint"), Color);
			if (SurfaceId == PaintSurfaceId)
			{
				Bound.DynamicInstance->SetVectorParameterValue(
					TEXT("Primary Glints Color"),
					Color);
			}
			if (FamilyId == TEXT("aluminum-alloy")
				|| FamilyId == TEXT("magnesium-alloy")
				|| FamilyId == TEXT("metal"))
			{
				Bound.DynamicInstance->SetVectorParameterValue(
					TEXT("Metallic Color A"),
					Color);
				Bound.DynamicInstance->SetVectorParameterValue(
					TEXT("Metallic Color B"),
					Color);
			}
			Bound.DynamicColorTexture = UpdateDynamicColorTexture(
				Bound.DynamicColorTexture,
				Color);
			if (IsValid(Bound.DynamicColorTexture))
			{
				static const FName ColorTextureParameters[] = {
					TEXT("Diffuse Color Map"),
					TEXT("Color Map"),
					TEXT("Base Color Map")
				};
				for (const FName ParameterName : ColorTextureParameters)
				{
					Bound.DynamicInstance->SetTextureParameterValue(
						ParameterName,
						Bound.DynamicColorTexture);
				}
			}
			if (bHasPaintParameters)
			{
				Bound.DynamicInstance->SetScalarParameterValue(
					TEXT("Metallic"), Paint.Metallic);
				Bound.DynamicInstance->SetScalarParameterValue(
					TEXT("Roughness"), Paint.Roughness);
				Bound.DynamicInstance->SetScalarParameterValue(
					TEXT("ClearCoat"), Paint.ClearCoat);
				Bound.DynamicInstance->SetScalarParameterValue(
					TEXT("OrangePeel"), Paint.OrangePeel);
				Bound.DynamicInstance->SetScalarParameterValue(
					TEXT("FlakeIntensity"), Paint.FlakeIntensity);
			}
			AppliedMaterial = Bound.DynamicInstance;
		}
		Bound.Component->SetMaterial(Bound.MaterialIndex, AppliedMaterial);
		if (OutAppliedSlots != nullptr)
		{
			OutAppliedSlots->Add(Bound.SlotId);
		}
		if (SurfaceId == PaintSurfaceId)
		{
			PaintMaterialInstance = Cast<UMaterialInstanceDynamic>(AppliedMaterial);
		}
		if (SurfaceId == InteriorProxySurfaceId)
		{
			InteriorMaterialInstance = Cast<UMaterialInstanceDynamic>(AppliedMaterial);
			AppliedInteriorFamilyId = FamilyId;
		}
	}
	if (!bFoundTarget)
	{
		SetFailure(
			TEXT("RUNTIME_SURFACE_TARGET_MISSING"),
			FString::Printf(TEXT("surfaceId=%s 没有已绑定运行时槽。"), *SurfaceId));
		return false;
	}
	return true;
}

FAutomotiveMaterialTransactionResult UAutomotiveMaterialBinder::ApplyTransaction(
	const TMap<FString, FString>& InSelections,
	const TMap<FString, FAutomotiveCustomization>& InCustomizations)
{
	LastTransactionResult = FAutomotiveMaterialTransactionResult();
	if (!IsValid(State) || !IsValid(Library) || BoundSlots.IsEmpty())
	{
		SetFailure(TEXT("BINDER_UNAVAILABLE"), TEXT("材质 Binder 尚未完成有效绑定。"));
		return LastTransactionResult;
	}
	AutomotiveCatalog::FError ValidationError;
	if (!State->CanApplyTransaction(
		InSelections,
		InCustomizations,
		&ValidationError))
	{
		SetFailure(
			ValidationError.Code.IsEmpty()
				? TEXT("INVALID_CONFIGURATION_TRANSACTION")
				: ValidationError.Code,
			ValidationError.Message.IsEmpty()
				? TEXT("配置事务未通过目录与定制参数校验。")
				: ValidationError.Message);
		return LastTransactionResult;
	}

	const TMap<FString, FString> PreviousSelections = State->GetSelections();
	const TMap<FString, FAutomotiveCustomization> PreviousCustomizations =
		State->GetCustomizations();
	TSet<FString> ChangedSet;
	TArray<FString> ChangedSurfaceIds;
	for (const FString& SurfaceId :
		State->GetCatalogIndex().GetCatalog().SelectionOrder)
	{
		const FString* PreviousOption = PreviousSelections.Find(SurfaceId);
		const FString* NextOption = InSelections.Find(SurfaceId);
		const bool bSelectionChanged =
			(PreviousOption == nullptr) != (NextOption == nullptr)
			|| (PreviousOption != nullptr && NextOption != nullptr
				&& *PreviousOption != *NextOption);
		const FAutomotiveCustomization* PreviousCustomization =
			PreviousCustomizations.Find(SurfaceId);
		const FAutomotiveCustomization* NextCustomization =
			InCustomizations.Find(SurfaceId);
		const bool bCustomizationChanged =
			(PreviousCustomization == nullptr) != (NextCustomization == nullptr)
			|| (PreviousCustomization != nullptr && NextCustomization != nullptr
				&& !(*PreviousCustomization == *NextCustomization));
		if (bSelectionChanged || bCustomizationChanged)
		{
			ChangedSet.Add(SurfaceId);
			ChangedSurfaceIds.Add(SurfaceId);
		}
	}

	TMap<FString, TArray<FName>> BindingTargets;
	TSet<FString> UnsupportedSet;
	AutomotiveCatalog::FError BindingError;
	if (!State->GetCatalogIndex().ResolveSurfaceBindingTransaction(
		ChangedSet,
		BindingTargets,
		UnsupportedSet,
		BindingError))
	{
		SetFailure(BindingError.Code, BindingError.Message);
		return LastTransactionResult;
	}

	TArray<TArray<TObjectPtr<UMaterialInterface>>> PreviousAppliedMaterials;
	PreviousAppliedMaterials.Reserve(BoundSlots.Num());
	for (const FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
	{
		TArray<TObjectPtr<UMaterialInterface>>& Materials =
			PreviousAppliedMaterials.AddDefaulted_GetRef();
		if (!IsValid(Bound.Component) || Bound.MaterialIndex == INDEX_NONE)
		{
			continue;
		}
		Materials.Add(Bound.Component->GetMaterial(Bound.MaterialIndex));
	}

	// 在提交状态前加载并验证本事务所需的所有材质，保证缺资产时零修改。
	for (const FString& SurfaceId : ChangedSurfaceIds)
	{
		if (!BindingTargets.Contains(SurfaceId))
		{
			continue;
		}
		const FString& EffectiveSurfaceId = SurfaceId == WheelColorSurfaceId
			? WheelMaterialSurfaceId
			: SurfaceId;
		UMaterialInterface* Material = nullptr;
		bool bUseDynamic = false;
		FLinearColor Color;
		FAutomotivePaintCustomization Paint;
		bool bHasPaintParameters = false;
		FString FamilyId;
		FString ErrorCode;
		FString ErrorMessage;
		const bool bResolved = SurfaceId == WheelColorSurfaceId
			? ResolveWheelColor(
				InSelections,
				Color,
				ErrorCode,
				ErrorMessage)
			: ResolveSurfaceMaterial(
				SurfaceId,
				InSelections,
				InCustomizations,
				Material,
				bUseDynamic,
				Color,
				Paint,
				bHasPaintParameters,
				FamilyId,
				ErrorCode,
				ErrorMessage);
		if (!bResolved)
		{
			SetFailure(ErrorCode, ErrorMessage);
			return LastTransactionResult;
		}
		int32 PreparedSlotCount = 0;
		for (FAutomotiveBoundMaterialSlot& Bound : BoundSlots)
		{
			if (Bound.SurfaceId != EffectiveSurfaceId)
			{
				continue;
			}
			++PreparedSlotCount;
			if (!IsValid(Bound.Component) || Bound.MaterialIndex == INDEX_NONE)
			{
				SetFailure(
					TEXT("RUNTIME_MATERIAL_SLOT_INVALID"),
					FString::Printf(
						TEXT("surfaceId=%s 的运行时槽已失效。"),
						*SurfaceId));
				return LastTransactionResult;
			}
			if (bUseDynamic
				&& (!IsValid(Bound.DynamicInstance)
					|| Bound.DynamicParent != Material))
			{
				Bound.DynamicInstance =
					UMaterialInstanceDynamic::Create(Material, this);
				Bound.DynamicParent = Material;
				if (!IsValid(Bound.DynamicInstance))
				{
					SetFailure(
						TEXT("DYNAMIC_MATERIAL_CREATE_FAILED"),
						FString::Printf(
							TEXT("surfaceId=%s 的槽 %s 无法创建 MID。"),
							*SurfaceId,
							*Bound.SlotId.ToString()));
					return LastTransactionResult;
				}
			}
		}
		if (PreparedSlotCount != BindingTargets.FindChecked(SurfaceId).Num())
		{
			SetFailure(
				TEXT("RUNTIME_SURFACE_TARGET_MISMATCH"),
				FString::Printf(
					TEXT("surfaceId=%s 的运行时槽数量与 binding 不一致。"),
					*SurfaceId));
			return LastTransactionResult;
		}
	}

	bApplyingTransaction = true;
	const bool bCommitted =
		State->ApplyTransaction(InSelections, InCustomizations);
	bApplyingTransaction = false;
	if (!bCommitted)
	{
		SetFailure(State->GetLastErrorCode(), TEXT("配置状态原子提交失败。"));
		return LastTransactionResult;
	}

	LastError.Reset();
	LastTransactionResult.bSuccess = true;
	LastTransactionResult.ConfigurationId = State->GetConfigurationId();
	for (const FString& SurfaceId : ChangedSurfaceIds)
	{
		if (BindingTargets.Contains(SurfaceId))
		{
			if (!ApplySurface(
				SurfaceId,
				InSelections,
				InCustomizations,
				&LastTransactionResult.AppliedSlotIds))
			{
				const FString ApplyError = LastError;
				bApplyingTransaction = true;
				const bool bRolledBack = State->ApplyTransaction(
					PreviousSelections,
					PreviousCustomizations);
				bApplyingTransaction = false;
				for (int32 Index = 0;
					Index < BoundSlots.Num() && Index < PreviousAppliedMaterials.Num();
					++Index)
				{
					FAutomotiveBoundMaterialSlot& Bound = BoundSlots[Index];
					if (IsValid(Bound.Component) && Bound.MaterialIndex != INDEX_NONE)
					{
						for (int32 MaterialOffset = 0;
							MaterialOffset < PreviousAppliedMaterials[Index].Num();
							++MaterialOffset)
						{
							Bound.Component->SetMaterial(
								Bound.MaterialIndex + MaterialOffset,
								PreviousAppliedMaterials[Index][MaterialOffset]);
						}
					}
				}
				SetFailure(
					bRolledBack
						? TEXT("MATERIAL_APPLY_ROLLED_BACK")
						: TEXT("MATERIAL_APPLY_ROLLBACK_FAILED"),
					ApplyError);
				return LastTransactionResult;
			}
			LastTransactionResult.AppliedSurfaceIds.Add(SurfaceId);
			if (SurfaceId == WheelMaterialSurfaceId
				&& !ChangedSet.Contains(WheelColorSurfaceId)
				&& !ApplyWheelColorOverlay(InSelections))
			{
				const FString ApplyError = LastError;
				bApplyingTransaction = true;
				const bool bRolledBack = State->ApplyTransaction(
					PreviousSelections,
					PreviousCustomizations);
				bApplyingTransaction = false;
				for (int32 Index = 0;
					Index < BoundSlots.Num() && Index < PreviousAppliedMaterials.Num();
					++Index)
				{
					FAutomotiveBoundMaterialSlot& Bound = BoundSlots[Index];
					if (!IsValid(Bound.Component) || Bound.MaterialIndex == INDEX_NONE)
					{
						continue;
					}
					for (int32 MaterialOffset = 0;
						MaterialOffset < PreviousAppliedMaterials[Index].Num();
						++MaterialOffset)
					{
						Bound.Component->SetMaterial(
							Bound.MaterialIndex + MaterialOffset,
							PreviousAppliedMaterials[Index][MaterialOffset]);
					}
				}
				SetFailure(
					bRolledBack
						? TEXT("MATERIAL_APPLY_ROLLED_BACK")
						: TEXT("MATERIAL_APPLY_ROLLBACK_FAILED"),
					ApplyError);
				return LastTransactionResult;
			}
		}
		if (UnsupportedSet.Contains(SurfaceId))
		{
			LastTransactionResult.UnsupportedSurfaceIds.Add(SurfaceId);
		}
	}
	if (LastTransactionResult.UnsupportedSurfaceIds.IsEmpty())
	{
		LastTransactionResult.Code = ChangedSurfaceIds.IsEmpty()
			? TEXT("NO_CHANGE")
			: TEXT("APPLIED");
		LastTransactionResult.Message = ChangedSurfaceIds.IsEmpty()
			? TEXT("配置未变化。")
			: TEXT("配置与可映射材质槽已原子应用。");
	}
	else
	{
		LastTransactionResult.Code = TEXT("APPLIED_WITH_UNSUPPORTED_SURFACES");
		LastTransactionResult.Message = FString::Printf(
			TEXT("配置已应用；%d 个 surfaceId 为当前 proxy capability 明确缺口。"),
			LastTransactionResult.UnsupportedSurfaceIds.Num());
	}
	return LastTransactionResult;
}
