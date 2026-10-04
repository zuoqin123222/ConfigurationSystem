#include "AutomotiveConfigurationState.h"

#include "AutomotiveCatalogData.h"

namespace
{
	bool CustomizationMapsEqual(
		const TMap<FString, FAutomotiveCustomization>& Left,
		const TMap<FString, FAutomotiveCustomization>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (const TPair<FString, FAutomotiveCustomization>& Pair : Left)
		{
			const FAutomotiveCustomization* Other = Right.Find(Pair.Key);
			if (Other == nullptr || !(Pair.Value == *Other))
			{
				return false;
			}
		}
		return true;
	}
}

bool FAutomotivePaintCustomization::operator==(
	const FAutomotivePaintCustomization& Other) const
{
	return ColorHex.Equals(Other.ColorHex, ESearchCase::IgnoreCase)
		&& Metallic == Other.Metallic
		&& Roughness == Other.Roughness
		&& ClearCoat == Other.ClearCoat
		&& OrangePeel == Other.OrangePeel
		&& FlakeIntensity == Other.FlakeIntensity;
}

bool FAutomotiveCustomization::operator==(const FAutomotiveCustomization& Other) const
{
	return Kind == Other.Kind
		&& MaterialVariantId == Other.MaterialVariantId
		&& Paint == Other.Paint;
}

bool UAutomotiveConfigurationState::Initialize(UAutomotiveCatalogData* InCatalogAsset)
{
	if (!IsValid(InCatalogAsset))
	{
		LastErrorCode = TEXT("INVALID_CATALOG_ASSET");
		return false;
	}

	AutomotiveCatalog::FCatalogIndex CandidateCatalog;
	AutomotiveCatalog::FError Error;
	if (!InCatalogAsset->BuildCatalogIndex(CandidateCatalog, Error))
	{
		LastErrorCode = Error.Code;
		return false;
	}

	TMap<FString, FString> Defaults = CandidateCatalog.GetCatalog().DefaultSelections;
	for (const FString& SurfaceId : CandidateCatalog.GetCatalog().SelectionOrder)
	{
		const AutomotiveCatalog::FSurface* Surface = CandidateCatalog.FindSurface(SurfaceId);
		const FString* DefaultOptionId =
			CandidateCatalog.FindDefaultOptionIdForSurface(SurfaceId);
		if (Surface == nullptr || (Surface->bRequired && DefaultOptionId == nullptr))
		{
			LastErrorCode = TEXT("REQUIRED_SURFACE_WITHOUT_DEFAULT");
			return false;
		}
	}

	AutomotiveCatalog::FConfiguration Derived;
	if (!AutomotiveCatalog::DeriveConfiguration(
		Defaults,
		AutomotiveCatalog::FCustomizations(),
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

bool UAutomotiveConfigurationState::ApplyTransaction(
	const TMap<FString, FString>& InSelections,
	const TMap<FString, FAutomotiveCustomization>& InCustomizations)
{
	return TryCommit(InSelections, InCustomizations, true);
}

bool UAutomotiveConfigurationState::CanApplyTransaction(
	const TMap<FString, FString>& InSelections,
	const TMap<FString, FAutomotiveCustomization>& InCustomizations) const
{
	if (!bInitialized)
	{
		return false;
	}
	AutomotiveCatalog::FConfiguration Ignored;
	AutomotiveCatalog::FError Error;
	return AutomotiveCatalog::DeriveConfiguration(
		InSelections,
		ToDomainCustomizations(InCustomizations),
		CatalogIndex,
		Ignored,
		Error);
}

bool UAutomotiveConfigurationState::SelectOption(
	const FString& SurfaceId,
	const FString& OptionId)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FString> Candidate = Selections;
	TMap<FString, FAutomotiveCustomization> CandidateCustomizations = Customizations;
	Candidate.Add(SurfaceId, OptionId);
	if (Selections.FindRef(SurfaceId) != OptionId)
	{
		// option 决定可用的材料族/自定义类型。切换时在同一事务中移除旧定制，
		// 避免旧 variant 或 custom paint 令新的合法 option 无法被选中。
		CandidateCustomizations.Remove(SurfaceId);
	}
	return TryCommit(Candidate, CandidateCustomizations, true);
}

bool UAutomotiveConfigurationState::ClearOptionalSelection(const FString& SurfaceId)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	const AutomotiveCatalog::FSurface* Surface = CatalogIndex.FindSurface(SurfaceId);
	if (Surface == nullptr || Surface->bRequired)
	{
		LastErrorCode = TEXT("REQUIRED_SELECTION");
		return false;
	}
	TMap<FString, FString> Candidate = Selections;
	TMap<FString, FAutomotiveCustomization> CandidateCustomizations = Customizations;
	Candidate.Remove(SurfaceId);
	CandidateCustomizations.Remove(SurfaceId);
	return TryCommit(Candidate, CandidateCustomizations, true);
}

bool UAutomotiveConfigurationState::SetMaterialVariant(
	const FString& SurfaceId,
	const FString& MaterialVariantId)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FAutomotiveCustomization> Candidate = Customizations;
	FAutomotiveCustomization Value;
	Value.Kind = EAutomotiveCustomizationKind::MaterialVariant;
	Value.MaterialVariantId = MaterialVariantId;
	Candidate.Add(SurfaceId, MoveTemp(Value));
	return TryCommit(Selections, Candidate, true);
}

bool UAutomotiveConfigurationState::SetPaintCustomization(
	const FString& SurfaceId,
	const FAutomotivePaintCustomization& Paint)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FAutomotiveCustomization> Candidate = Customizations;
	FAutomotiveCustomization Value;
	Value.Kind = EAutomotiveCustomizationKind::Paint;
	Value.Paint = Paint;
	Candidate.Add(SurfaceId, MoveTemp(Value));
	return TryCommit(Selections, Candidate, true);
}

bool UAutomotiveConfigurationState::ClearCustomization(const FString& SurfaceId)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}
	TMap<FString, FAutomotiveCustomization> Candidate = Customizations;
	Candidate.Remove(SurfaceId);
	return TryCommit(Selections, Candidate, true);
}

bool UAutomotiveConfigurationState::TryCommit(
	const TMap<FString, FString>& CandidateSelections,
	const TMap<FString, FAutomotiveCustomization>& CandidateCustomizations,
	const bool bBroadcast)
{
	if (!bInitialized)
	{
		LastErrorCode = TEXT("STATE_NOT_INITIALIZED");
		return false;
	}

	TMap<FString, FAutomotiveCustomization> NormalizedCustomizations =
		CandidateCustomizations;
	for (TPair<FString, FAutomotiveCustomization>& Pair : NormalizedCustomizations)
	{
		if (Pair.Value.Kind == EAutomotiveCustomizationKind::Paint)
		{
			Pair.Value.Paint.ColorHex = Pair.Value.Paint.ColorHex.ToUpper();
		}
	}

	AutomotiveCatalog::FConfiguration Derived;
	AutomotiveCatalog::FError Error;
	if (!AutomotiveCatalog::DeriveConfiguration(
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
		!Selections.OrderIndependentCompareEqual(Derived.Selections)
		|| !CustomizationMapsEqual(Customizations, NormalizedCustomizations);
	if (!bChanged)
	{
		return true;
	}

	Selections = MoveTemp(Derived.Selections);
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

AutomotiveCatalog::FCustomizations UAutomotiveConfigurationState::ToDomainCustomizations(
	const TMap<FString, FAutomotiveCustomization>& Values)
{
	AutomotiveCatalog::FCustomizations Result;
	for (const TPair<FString, FAutomotiveCustomization>& Pair : Values)
	{
		if (Pair.Value.Kind == EAutomotiveCustomizationKind::MaterialVariant)
		{
			Result.Add(
				Pair.Key,
				AutomotiveCatalog::FCustomization::ForMaterialVariant(
					Pair.Value.MaterialVariantId));
			continue;
		}

		AutomotiveCatalog::FPaintCustomization Paint;
		Paint.ColorHex = Pair.Value.Paint.ColorHex;
		Paint.Metallic = Pair.Value.Paint.Metallic;
		Paint.Roughness = Pair.Value.Paint.Roughness;
		Paint.ClearCoat = Pair.Value.Paint.ClearCoat;
		Paint.OrangePeel = Pair.Value.Paint.OrangePeel;
		Paint.FlakeIntensity = Pair.Value.Paint.FlakeIntensity;
		Result.Add(Pair.Key, AutomotiveCatalog::FCustomization::ForPaint(Paint));
	}
	return Result;
}
