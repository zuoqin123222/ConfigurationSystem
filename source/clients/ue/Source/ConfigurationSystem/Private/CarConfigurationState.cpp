#include "CarConfigurationState.h"

namespace CarConfigurationState
{
	const FString PaintPart = TEXT("paint");
	const FString WheelPart = TEXT("wheel");
	const FString InteriorPart = TEXT("interior");
	const FString FramePart = TEXT("frame");
}

bool UCarConfigurationState::Initialize(
	const int64 InBasePriceMinor,
	const TArray<FCarConfigurationOption>& InOptions,
	const TArray<FCarConfigurationTemplate>& InTemplates,
	const FCarConfigurationSelection& InDefaults)
{
	if (InBasePriceMinor < 0)
	{
		return false;
	}

	TMap<FString, FCarConfigurationOption> CandidateOptions;
	for (const FCarConfigurationOption& Option : InOptions)
	{
		if (!IsValidPartId(Option.PartId)
			|| Option.OptionId.IsEmpty()
			|| Option.PriceDeltaMinor < 0
			|| CandidateOptions.Contains(Option.OptionId))
		{
			return false;
		}
		CandidateOptions.Add(Option.OptionId, Option);
	}

	if (!IsSelectionValid(InDefaults, CandidateOptions))
	{
		return false;
	}

	TMap<FString, FCarConfigurationTemplate> CandidateTemplates;
	for (const FCarConfigurationTemplate& Template : InTemplates)
	{
		if (Template.TemplateId.IsEmpty()
			|| CandidateTemplates.Contains(Template.TemplateId)
			|| !IsSelectionValid(Template.Selections, CandidateOptions))
		{
			return false;
		}
		CandidateTemplates.Add(Template.TemplateId, Template);
	}

	const bool bStateChanged = !bInitialized
		|| BasePriceMinor != InBasePriceMinor
		|| Selection != InDefaults;
	BasePriceMinor = InBasePriceMinor;
	OptionsById = MoveTemp(CandidateOptions);
	TemplatesById = MoveTemp(CandidateTemplates);
	Selection = InDefaults;
	bInitialized = true;

	if (bStateChanged)
	{
		OnChanged.Broadcast();
	}
	return true;
}

bool UCarConfigurationState::SelectOption(
	const FString& PartId,
	const FString& OptionId)
{
	if (!bInitialized || !IsValidPartId(PartId))
	{
		return false;
	}

	const FCarConfigurationOption* Option = OptionsById.Find(OptionId);
	if (Option == nullptr || Option->PartId != PartId)
	{
		return false;
	}

	FString* CurrentOptionId = GetSelectionForPart(Selection, PartId);
	if (CurrentOptionId == nullptr)
	{
		return false;
	}
	if (*CurrentOptionId == OptionId)
	{
		return true;
	}

	*CurrentOptionId = OptionId;
	OnChanged.Broadcast();
	return true;
}

bool UCarConfigurationState::ApplyTemplate(const FString& TemplateId)
{
	if (!bInitialized)
	{
		return false;
	}

	const FCarConfigurationTemplate* Template = TemplatesById.Find(TemplateId);
	if (Template == nullptr || !IsSelectionValid(Template->Selections, OptionsById))
	{
		return false;
	}
	if (Selection == Template->Selections)
	{
		return true;
	}

	// 模板作为一次原子修改提交，只广播一次变更。
	Selection = Template->Selections;
	OnChanged.Broadcast();
	return true;
}

bool UCarConfigurationState::ApplySelection(
	const FCarConfigurationSelection& InSelection)
{
	if (!CanApplySelection(InSelection))
	{
		return false;
	}
	if (Selection == InSelection)
	{
		return true;
	}

	Selection = InSelection;
	OnChanged.Broadcast();
	return true;
}

bool UCarConfigurationState::CanApplySelection(
	const FCarConfigurationSelection& InSelection) const
{
	return bInitialized && IsSelectionValid(InSelection, OptionsById);
}

FString UCarConfigurationState::GetCanonicalKey() const
{
	if (!bInitialized)
	{
		return FString();
	}

	return FString::Printf(
		TEXT("%s__%s__%s__%s"),
		*Selection.Paint,
		*Selection.Wheel,
		*Selection.Interior,
		*Selection.Frame);
}

int64 UCarConfigurationState::GetTotalPrice() const
{
	int64 Total = BasePriceMinor;
	const FString* OptionIds[] = {
		&Selection.Paint,
		&Selection.Wheel,
		&Selection.Interior,
		&Selection.Frame
	};
	for (const FString* OptionId : OptionIds)
	{
		if (const FCarConfigurationOption* Option = OptionsById.Find(*OptionId))
		{
			Total += Option->PriceDeltaMinor;
		}
	}
	return Total;
}

bool UCarConfigurationState::IsValidPartId(const FString& PartId)
{
	using namespace CarConfigurationState;
	return PartId == PaintPart
		|| PartId == WheelPart
		|| PartId == InteriorPart
		|| PartId == FramePart;
}

const FString* UCarConfigurationState::GetSelectionForPart(
	const FCarConfigurationSelection& Value,
	const FString& PartId)
{
	using namespace CarConfigurationState;
	if (PartId == PaintPart)
	{
		return &Value.Paint;
	}
	if (PartId == WheelPart)
	{
		return &Value.Wheel;
	}
	if (PartId == InteriorPart)
	{
		return &Value.Interior;
	}
	if (PartId == FramePart)
	{
		return &Value.Frame;
	}
	return nullptr;
}

FString* UCarConfigurationState::GetSelectionForPart(
	FCarConfigurationSelection& Value,
	const FString& PartId)
{
	return const_cast<FString*>(GetSelectionForPart(
		static_cast<const FCarConfigurationSelection&>(Value),
		PartId));
}

bool UCarConfigurationState::IsSelectionValid(
	const FCarConfigurationSelection& Value,
	const TMap<FString, FCarConfigurationOption>& CandidateOptions) const
{
	using namespace CarConfigurationState;
	const FString PartIds[] = {PaintPart, WheelPart, InteriorPart, FramePart};
	for (const FString& PartId : PartIds)
	{
		const FString* OptionId = GetSelectionForPart(Value, PartId);
		const FCarConfigurationOption* Option =
			OptionId != nullptr ? CandidateOptions.Find(*OptionId) : nullptr;
		if (Option == nullptr || Option->PartId != PartId)
		{
			return false;
		}
	}
	return true;
}
