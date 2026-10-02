#include "Sc01V2ConfigurationState.h"

#include "Sc01V2CatalogData.h"

namespace
{
	bool CustomizationMapsEqual(
		const TMap<FString, FSc01V2Customization>& Left,
		const TMap<FString, FSc01V2Customization>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (const TPair<FString, FSc01V2Customization>& Pair : Left)
		{
			const FSc01V2Customization* Other = Right.Find(Pair.Key);
			if (Other == nullptr || !(Pair.Value == *Other))
			{
				return false;
			}
		}
		return true;
	}
}

bool FSc01V2PaintCustomization::operator==(
	const FSc01V2PaintCustomization& Other) const
{
	return ColorHex.Equals(Other.ColorHex, ESearchCase::IgnoreCase)
		&& Metallic == Other.Metallic
		&& Roughness == Other.Roughness
		&& ClearCoat == Other.ClearCoat
		&& OrangePeel == Other.OrangePeel
		&& FlakeIntensity == Other.FlakeIntensity;
}

bool FSc01V2Customization::operator==(const FSc01V2Customization& Other) const
{
	return Kind == Other.Kind
		&& MaterialVariantId == Other.MaterialVariantId
		&& Paint == Other.Paint;
}

bool USc01V2ConfigurationState::Initialize(USc01V2CatalogData* InCatalogAsset)
{
	if (!IsValid(InCatalogAsset))
	{
		LastErrorCode = TEXT("INVALID_CATALOG_ASSET");
		return false;
	}

	Sc01V2::FCatalogIndex CandidateCatalog;
	Sc01V2::FError Error;
	if (!InCatalogAsset->BuildCatalogIndex(CandidateCatalog, Error))
	{
		LastErrorCode = Error.Code;
		return false;
	}

	TMap<FString, FString> Defaults;
	for (const FString& SurfaceId : CandidateCatalog.GetCatalog().SelectionOrder)
	{
		const TArray<FString>* OptionIds =
			CandidateCatalog.FindOptionIdsForSurface(SurfaceId);
		if (OptionIds == nullptr || OptionIds->IsEmpty())
		{
			LastErrorCode = TEXT("SURFACE_WITHOUT_OPTIONS");
			return false;
		}
		Defaults.Add(SurfaceId, (*OptionIds)[0]);
	}

	Sc01V2::FConfiguration Derived;
	if (!Sc01V2::DeriveConfiguration(
		Defaults,
		Sc01V2::FCustomizations(),
		CandidateCatalog,
		Derived,
		Error))
	{
		LastErrorCode = Error.Code;
		return false;
	}

	const bool bChanged = !bInitialized
		|| Selections.OrderIndependentCompareEqual(Defaults) == false
		|| !Customizations.IsEmpty()
		|| ConfigurationId != Derived.ConfigurationId
		|| RenderKey != Derived.RenderKey;
	CatalogAsset = InCatalogAsset;
	CatalogIndex = MoveTemp(CandidateCatalog);
	Selections = MoveTemp(Defaults);
	Customizations.Reset();
	ConfigurationId = MoveTemp(Derived.ConfigurationId);
	RenderKey = MoveTemp(Derived.RenderKey);
	LastErrorCode.Reset();
	bInitialized = true;
	if (bChanged)
	{
		OnChanged.Broadcast();
		OnChangedNative.Broadcast();
	}
	return true;
}

bool USc01V2ConfigurationState::ApplyTransaction(
	const TMap<FString, FString>& InSelections,
	const TMap<FString, FSc01V2Customization>& InCustomizations)
{
	return TryCommit(InSelections, InCustomizations, true);
}

bool USc01V2ConfigurationState::CanApplyTransaction(
	const TMap<FString, FString>& InSelections,
	const TMap<FString, FSc01V2Customization>& InCustomizations) const
{
	if (!bInitialized)
	{
		return false;
	}
	Sc01V2::FConfiguration Ignored;
	Sc01V2::FError Error;
	return Sc01V2::DeriveConfiguration(
		InSelections,
		ToDomainCustomizations(InCustomizations),
		CatalogIndex,
		Ignored,
		Error);
}

bool USc01V2ConfigurationState::SelectOption(
	const FString& SurfaceId,
	const FString& OptionId)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FString> Candidate = Selections;
	TMap<FString, FSc01V2Customization> CandidateCustomizations = Customizations;
	Candidate.Add(SurfaceId, OptionId);
	if (Selections.FindRef(SurfaceId) != OptionId)
	{
		// option 决定可用的材料族/自定义类型。切换时在同一事务中移除旧定制，
		// 避免旧 variant 或 custom paint 令新的合法 option 无法被选中。
		CandidateCustomizations.Remove(SurfaceId);
	}
	return TryCommit(Candidate, CandidateCustomizations, true);
}

bool USc01V2ConfigurationState::SetMaterialVariant(
	const FString& SurfaceId,
	const FString& MaterialVariantId)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FSc01V2Customization> Candidate = Customizations;
	FSc01V2Customization Value;
	Value.Kind = ESc01V2CustomizationKind::MaterialVariant;
	Value.MaterialVariantId = MaterialVariantId;
	Candidate.Add(SurfaceId, MoveTemp(Value));
	return TryCommit(Selections, Candidate, true);
}

bool USc01V2ConfigurationState::SetPaintCustomization(
	const FString& SurfaceId,
	const FSc01V2PaintCustomization& Paint)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FSc01V2Customization> Candidate = Customizations;
	FSc01V2Customization Value;
	Value.Kind = ESc01V2CustomizationKind::Paint;
	Value.Paint = Paint;
	Candidate.Add(SurfaceId, MoveTemp(Value));
	return TryCommit(Selections, Candidate, true);
}

bool USc01V2ConfigurationState::ClearCustomization(const FString& SurfaceId)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FSc01V2Customization> Candidate = Customizations;
	Candidate.Remove(SurfaceId);
	return TryCommit(Selections, Candidate, true);
}

bool USc01V2ConfigurationState::TryCommit(
	const TMap<FString, FString>& CandidateSelections,
	const TMap<FString, FSc01V2Customization>& CandidateCustomizations,
	const bool bBroadcast)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}

	TMap<FString, FSc01V2Customization> NormalizedCustomizations =
		CandidateCustomizations;
	for (TPair<FString, FSc01V2Customization>& Pair : NormalizedCustomizations)
	{
		if (Pair.Value.Kind == ESc01V2CustomizationKind::Paint)
		{
			Pair.Value.Paint.ColorHex = Pair.Value.Paint.ColorHex.ToUpper();
		}
	}

	Sc01V2::FConfiguration Derived;
	Sc01V2::FError Error;
	if (!Sc01V2::DeriveConfiguration(
		CandidateSelections,
		ToDomainCustomizations(NormalizedCustomizations),
		CatalogIndex,
		Derived,
		Error))
	{
		LastErrorCode = Error.Code;
		return false;
	}

	LastErrorCode.Reset();
	const bool bChanged =
		!Selections.OrderIndependentCompareEqual(CandidateSelections)
		|| !CustomizationMapsEqual(Customizations, NormalizedCustomizations);
	if (!bChanged)
	{
		return true;
	}

	Selections = CandidateSelections;
	Customizations = MoveTemp(NormalizedCustomizations);
	ConfigurationId = MoveTemp(Derived.ConfigurationId);
	RenderKey = MoveTemp(Derived.RenderKey);
	if (bBroadcast)
	{
		OnChanged.Broadcast();
		OnChangedNative.Broadcast();
	}
	return true;
}

Sc01V2::FCustomizations USc01V2ConfigurationState::ToDomainCustomizations(
	const TMap<FString, FSc01V2Customization>& Values)
{
	Sc01V2::FCustomizations Result;
	for (const TPair<FString, FSc01V2Customization>& Pair : Values)
	{
		if (Pair.Value.Kind == ESc01V2CustomizationKind::MaterialVariant)
		{
			Result.Add(
				Pair.Key,
				Sc01V2::FCustomization::ForMaterialVariant(
					Pair.Value.MaterialVariantId));
			continue;
		}

		Sc01V2::FPaintCustomization Paint;
		Paint.ColorHex = Pair.Value.Paint.ColorHex;
		Paint.Metallic = Pair.Value.Paint.Metallic;
		Paint.Roughness = Pair.Value.Paint.Roughness;
		Paint.ClearCoat = Pair.Value.Paint.ClearCoat;
		Paint.OrangePeel = Pair.Value.Paint.OrangePeel;
		Paint.FlakeIntensity = Pair.Value.Paint.FlakeIntensity;
		Result.Add(Pair.Key, Sc01V2::FCustomization::ForPaint(Paint));
	}
	return Result;
}
