#include "CarConfiguratorSubsystem.h"

#include "Engine/AssetManager.h"
#include "ConfiguratorVehicleActor.h"
#include "Sc01V2CatalogData.h"
#include "Sc01V2ConfigurationState.h"

namespace CarConfiguratorCatalog
{
	FCarConfigurationOption Option(
		const TCHAR* PartId,
		const TCHAR* OptionId,
		const int64 PriceDeltaMinor)
	{
		FCarConfigurationOption Value;
		Value.PartId = PartId;
		Value.OptionId = OptionId;
		Value.PriceDeltaMinor = PriceDeltaMinor;
		return Value;
	}

	FCarConfigurationSelection Selection(
		const TCHAR* Paint,
		const TCHAR* Wheel,
		const TCHAR* Interior,
		const TCHAR* Frame)
	{
		FCarConfigurationSelection Value;
		Value.Paint = Paint;
		Value.Wheel = Wheel;
		Value.Interior = Interior;
		Value.Frame = Frame;
		return Value;
	}

	FCarConfigurationTemplate Template(
		const TCHAR* TemplateId,
		const FCarConfigurationSelection& Selection)
	{
		FCarConfigurationTemplate Value;
		Value.TemplateId = TemplateId;
		Value.Selections = Selection;
		return Value;
	}
}

void UCarConfiguratorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	int64 BasePriceMinor = 0;
	TArray<FCarConfigurationOption> Options;
	TArray<FCarConfigurationTemplate> Templates;
	FCarConfigurationSelection Defaults;
	BuildMvpCatalog(BasePriceMinor, Options, Templates, Defaults);

	State = NewObject<UCarConfigurationState>(this);
	State->OnChanged.AddDynamic(this, &UCarConfiguratorSubsystem::HandleStateChanged);
	ensureAlwaysMsgf(
		State->Initialize(BasePriceMinor, Options, Templates, Defaults),
		TEXT("内置 MVP catalog 必须能够初始化 UCarConfigurationState。"));

	const FPrimaryAssetId Sc01CatalogId(
		USc01V2CatalogData::PrimaryAssetType,
		USc01V2CatalogData::DefaultAssetName);
	const FSoftObjectPath Sc01CatalogPath =
		UAssetManager::Get().GetPrimaryAssetPath(Sc01CatalogId);
	Sc01V2Catalog = Cast<USc01V2CatalogData>(Sc01CatalogPath.TryLoad());
	if (IsValid(Sc01V2Catalog))
	{
		Sc01V2State = NewObject<USc01V2ConfigurationState>(this);
		ensureAlwaysMsgf(
			Sc01V2State->Initialize(Sc01V2Catalog),
			TEXT("DA_SC01Catalog 必须能够初始化独立 SC01 v2 状态。"));
	}

	const FText OptionNames[] = {
		NSLOCTEXT("Configurator", "PaintRed", "竞速红"),
		NSLOCTEXT("Configurator", "PaintSilver", "星辉银"),
		NSLOCTEXT("Configurator", "WheelSport", "运动轮毂"),
		NSLOCTEXT("Configurator", "WheelForged", "锻造轮毂"),
		NSLOCTEXT("Configurator", "InteriorDark", "曜石黑"),
		NSLOCTEXT("Configurator", "InteriorIvory", "象牙白"),
		NSLOCTEXT("Configurator", "FrameBlack", "哑光黑"),
		NSLOCTEXT("Configurator", "FrameRed", "性能红")
	};
	DisplayOptions.Reset(Options.Num());
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		FConfiguratorDisplayOption& Display = DisplayOptions.AddDefaulted_GetRef();
		Display.PartId = Options[Index].PartId;
		Display.OptionId = Options[Index].OptionId;
		Display.DisplayName = OptionNames[Index];
		Display.PriceDeltaMinor = Options[Index].PriceDeltaMinor;
	}

	DisplayTemplates.Reset(2);
	FConfiguratorDisplayTemplate& Sport = DisplayTemplates.AddDefaulted_GetRef();
	Sport.TemplateId = TEXT("sport");
	Sport.DisplayName = NSLOCTEXT("Configurator", "SportTemplate", "运动");
	FConfiguratorDisplayTemplate& Luxury = DisplayTemplates.AddDefaulted_GetRef();
	Luxury.TemplateId = TEXT("luxury");
	Luxury.DisplayName = NSLOCTEXT("Configurator", "LuxuryTemplate", "豪华");
}

void UCarConfiguratorSubsystem::Deinitialize()
{
	if (State != nullptr)
	{
		State->OnChanged.RemoveDynamic(this, &UCarConfiguratorSubsystem::HandleStateChanged);
	}
	RegisteredVehicle = nullptr;
	Sc01V2State = nullptr;
	Sc01V2Catalog = nullptr;
	State = nullptr;
	DisplayOptions.Reset();
	DisplayTemplates.Reset();
	Super::Deinitialize();
}

bool UCarConfiguratorSubsystem::SelectOption(
	const FString& PartId,
	const FString& OptionId)
{
	return State != nullptr && State->SelectOption(PartId, OptionId);
}

bool UCarConfiguratorSubsystem::ApplyTemplate(const FString& TemplateId)
{
	return State != nullptr && State->ApplyTemplate(TemplateId);
}

bool UCarConfiguratorSubsystem::ApplySelection(
	const FCarConfigurationSelection& Selection)
{
	return IsValid(State) && State->ApplySelection(Selection);
}

bool UCarConfiguratorSubsystem::CanApplySelection(
	const FCarConfigurationSelection& Selection) const
{
	return IsValid(State) && State->CanApplySelection(Selection);
}

FString UCarConfiguratorSubsystem::GetCanonicalKey() const
{
	return State != nullptr ? State->GetCanonicalKey() : FString();
}

int64 UCarConfiguratorSubsystem::GetTotalPriceMinor() const
{
	return State != nullptr ? State->GetTotalPrice() : 0;
}

FCarConfigurationSelection UCarConfiguratorSubsystem::GetSelection() const
{
	return State != nullptr ? State->GetSelection() : FCarConfigurationSelection();
}

void UCarConfiguratorSubsystem::RegisterVehicle(
	AConfiguratorVehicleActor* Vehicle)
{
	RegisteredVehicle = Vehicle;
	if (IsValid(RegisteredVehicle) && IsValid(State))
	{
		RegisteredVehicle->ApplyConfiguration(State->GetSelection());
	}
}

void UCarConfiguratorSubsystem::UnregisterVehicle(
	const AConfiguratorVehicleActor* Vehicle)
{
	if (RegisteredVehicle == Vehicle)
	{
		RegisteredVehicle = nullptr;
	}
}

void UCarConfiguratorSubsystem::HandleStateChanged()
{
	if (IsValid(RegisteredVehicle) && IsValid(State))
	{
		RegisteredVehicle->ApplyConfiguration(State->GetSelection());
	}
	OnConfigurationChanged.Broadcast();
}

void UCarConfiguratorSubsystem::BuildMvpCatalog(
	int64& OutBasePriceMinor,
	TArray<FCarConfigurationOption>& OutOptions,
	TArray<FCarConfigurationTemplate>& OutTemplates,
	FCarConfigurationSelection& OutDefaults)
{
	using namespace CarConfiguratorCatalog;
	OutBasePriceMinor = 30000000;
	OutOptions = {
		Option(TEXT("paint"), TEXT("paint-red"), 0),
		Option(TEXT("paint"), TEXT("paint-silver"), 880000),
		Option(TEXT("wheel"), TEXT("wheel-sport"), 0),
		Option(TEXT("wheel"), TEXT("wheel-forged"), 1200000),
		Option(TEXT("interior"), TEXT("interior-dark"), 0),
		Option(TEXT("interior"), TEXT("interior-ivory"), 680000),
		Option(TEXT("frame"), TEXT("frame-black"), 0),
		Option(TEXT("frame"), TEXT("frame-red"), 360000)
	};
	OutDefaults = Selection(
		TEXT("paint-red"),
		TEXT("wheel-sport"),
		TEXT("interior-dark"),
		TEXT("frame-black"));
	OutTemplates = {
		Template(TEXT("sport"), Selection(
			TEXT("paint-red"),
			TEXT("wheel-sport"),
			TEXT("interior-dark"),
			TEXT("frame-red"))),
		Template(TEXT("luxury"), Selection(
			TEXT("paint-silver"),
			TEXT("wheel-forged"),
			TEXT("interior-ivory"),
			TEXT("frame-black")))
	};
}
