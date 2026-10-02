#include "ConfigurationStateProbe.h"

#include "CarConfigurationState.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogConfigurationStateProbe, Log, All);

namespace ConfigurationStateProbe
{
	constexpr int64 BasePriceMinor = 30000000;

	FCarConfigurationOption MakeOption(
		const TCHAR* PartId,
		const TCHAR* OptionId,
		const int64 PriceDeltaMinor)
	{
		FCarConfigurationOption Result;
		Result.PartId = PartId;
		Result.OptionId = OptionId;
		Result.PriceDeltaMinor = PriceDeltaMinor;
		return Result;
	}

	FCarConfigurationSelection MakeSelection(
		const TCHAR* Paint,
		const TCHAR* Wheel,
		const TCHAR* Interior,
		const TCHAR* Frame)
	{
		FCarConfigurationSelection Result;
		Result.Paint = Paint;
		Result.Wheel = Wheel;
		Result.Interior = Interior;
		Result.Frame = Frame;
		return Result;
	}

	FCarConfigurationTemplate MakeTemplate(
		const TCHAR* TemplateId,
		const FCarConfigurationSelection& Selections)
	{
		FCarConfigurationTemplate Result;
		Result.TemplateId = TemplateId;
		Result.Selections = Selections;
		return Result;
	}
}

void UConfigurationStateProbe::Start()
{
	OutputPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("ConfigurationStateProbe"),
		TEXT("ConfigurationStateProbe.json"));
	FParse::Value(FCommandLine::Get(), TEXT("ConfigurationStateProbeOutput="), OutputPath);
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	// 探针对象由模块持有裸指针，因此显式 Root，直到报告写出或模块关闭。
	AddToRoot();
	EngineInitCompleteHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(
		this,
		&UConfigurationStateProbe::OnEngineLoopInitComplete);
}

void UConfigurationStateProbe::Shutdown()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	State = nullptr;
	if (IsRooted())
	{
		RemoveFromRoot();
	}
}

void UConfigurationStateProbe::OnEngineLoopInitComplete()
{
	if (EngineInitCompleteHandle.IsValid())
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Remove(EngineInitCompleteHandle);
		EngineInitCompleteHandle.Reset();
	}

	// 推迟到下一次 Tick，确保 Editor 与 Game 两种启动路径都已进入稳定主循环。
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UConfigurationStateProbe::Tick));
}

bool UConfigurationStateProbe::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	TickerHandle.Reset();
	Run();
	return false;
}

void UConfigurationStateProbe::Run()
{
	using namespace ConfigurationStateProbe;

	const TArray<FCarConfigurationOption> Options = {
		MakeOption(TEXT("paint"), TEXT("paint-red"), 0),
		MakeOption(TEXT("paint"), TEXT("paint-silver"), 880000),
		MakeOption(TEXT("wheel"), TEXT("wheel-sport"), 0),
		MakeOption(TEXT("wheel"), TEXT("wheel-forged"), 1200000),
		MakeOption(TEXT("interior"), TEXT("interior-dark"), 0),
		MakeOption(TEXT("interior"), TEXT("interior-ivory"), 680000),
		MakeOption(TEXT("frame"), TEXT("frame-black"), 0),
		MakeOption(TEXT("frame"), TEXT("frame-red"), 360000)
	};
	const FCarConfigurationSelection Defaults = MakeSelection(
		TEXT("paint-red"),
		TEXT("wheel-sport"),
		TEXT("interior-dark"),
		TEXT("frame-black"));
	const FCarConfigurationSelection Sport = MakeSelection(
		TEXT("paint-red"),
		TEXT("wheel-sport"),
		TEXT("interior-dark"),
		TEXT("frame-red"));
	const FCarConfigurationSelection Luxury = MakeSelection(
		TEXT("paint-silver"),
		TEXT("wheel-forged"),
		TEXT("interior-ivory"),
		TEXT("frame-black"));
	const TArray<FCarConfigurationTemplate> Templates = {
		MakeTemplate(TEXT("sport"), Sport),
		MakeTemplate(TEXT("luxury"), Luxury)
	};

	State = NewObject<UCarConfigurationState>(this);
	State->OnChanged.AddDynamic(this, &UConfigurationStateProbe::HandleStateChanged);
	bInitializationPassed = State->Initialize(BasePriceMinor, Options, Templates, Defaults);
	ExpectedEventCount = bInitializationPassed ? 1 : 0;

	TSet<FString> UniqueKeys;
	bPricesPassed = bInitializationPassed;
	bool bSelectionsPassed = bInitializationPassed;
	FCarConfigurationSelection Previous = State->GetSelection();
	for (int32 Mask = 0; Mask < 16 && bInitializationPassed; ++Mask)
	{
		const FCarConfigurationSelection Desired = MakeSelection(
			(Mask & 1) != 0 ? TEXT("paint-silver") : TEXT("paint-red"),
			(Mask & 2) != 0 ? TEXT("wheel-forged") : TEXT("wheel-sport"),
			(Mask & 4) != 0 ? TEXT("interior-ivory") : TEXT("interior-dark"),
			(Mask & 8) != 0 ? TEXT("frame-red") : TEXT("frame-black"));
		const struct
		{
			const TCHAR* PartId;
			const FString* DesiredOptionId;
		} Changes[] = {
			{TEXT("paint"), &Desired.Paint},
			{TEXT("wheel"), &Desired.Wheel},
			{TEXT("interior"), &Desired.Interior},
			{TEXT("frame"), &Desired.Frame}
		};

		for (const auto& Change : Changes)
		{
			const FString* Before = nullptr;
			if (FString(Change.PartId) == TEXT("paint")) Before = &Previous.Paint;
			else if (FString(Change.PartId) == TEXT("wheel")) Before = &Previous.Wheel;
			else if (FString(Change.PartId) == TEXT("interior")) Before = &Previous.Interior;
			else Before = &Previous.Frame;

			const bool bWillChange = *Before != *Change.DesiredOptionId;
			bSelectionsPassed &=
				State->SelectOption(Change.PartId, *Change.DesiredOptionId);
			if (bWillChange)
			{
				++ExpectedEventCount;
			}
			Previous = State->GetSelection();
		}

		const FString ExpectedKey = FString::Printf(
			TEXT("%s__%s__%s__%s"),
			*Desired.Paint,
			*Desired.Wheel,
			*Desired.Interior,
			*Desired.Frame);
		const FString ActualKey = State->GetCanonicalKey();
		const int64 ExpectedPrice = BasePriceMinor
			+ (((Mask & 1) != 0) ? 880000 : 0)
			+ (((Mask & 2) != 0) ? 1200000 : 0)
			+ (((Mask & 4) != 0) ? 680000 : 0)
			+ (((Mask & 8) != 0) ? 360000 : 0);
		const int64 ActualPrice = State->GetTotalPrice();

		CanonicalKeys.Add(ActualKey);
		ActualPrices.Add(ActualPrice);
		ExpectedPrices.Add(ExpectedPrice);
		UniqueKeys.Add(ActualKey);
		bSelectionsPassed &= ActualKey == ExpectedKey;
		bPricesPassed &= ActualPrice == ExpectedPrice;
	}
	UniqueKeyCount = UniqueKeys.Num();
	bSixteenUniqueKeysPassed =
		bSelectionsPassed && CanonicalKeys.Num() == 16 && UniqueKeyCount == 16;

	// 模板应原子切换；每次实际变化只产生一个事件，重复应用保持幂等。
	const int32 BeforeSportEvents = EventCount;
	const bool bSportApplied = State->ApplyTemplate(TEXT("sport"));
	++ExpectedEventCount;
	const bool bSportCorrect = State->GetSelection() == Sport
		&& State->GetCanonicalKey()
			== TEXT("paint-red__wheel-sport__interior-dark__frame-red")
		&& State->GetTotalPrice() == 30360000
		&& EventCount == BeforeSportEvents + 1;

	const int32 BeforeLuxuryEvents = EventCount;
	const bool bLuxuryApplied = State->ApplyTemplate(TEXT("luxury"));
	++ExpectedEventCount;
	const bool bLuxuryCorrect = State->GetSelection() == Luxury
		&& State->GetCanonicalKey()
			== TEXT("paint-silver__wheel-forged__interior-ivory__frame-black")
		&& State->GetTotalPrice() == 32760000
		&& EventCount == BeforeLuxuryEvents + 1;

	const int32 BeforeIdempotentEvents = EventCount;
	const bool bIdempotentApplied = State->ApplyTemplate(TEXT("luxury"));
	bTemplatesPassed = bSportApplied
		&& bSportCorrect
		&& bLuxuryApplied
		&& bLuxuryCorrect
		&& bIdempotentApplied
		&& EventCount == BeforeIdempotentEvents;

	// 三类非法输入都必须同时保持选择、价格、规范键和事件计数不变。
	const FCarConfigurationSelection BeforeInvalidSelection = State->GetSelection();
	const FString BeforeInvalidKey = State->GetCanonicalKey();
	const int64 BeforeInvalidPrice = State->GetTotalPrice();
	const int32 BeforeInvalidEvents = EventCount;
	const bool bInvalidPartRejected =
		!State->SelectOption(TEXT("sunroof"), TEXT("paint-red"));
	const bool bCrossPartRejected =
		!State->SelectOption(TEXT("paint"), TEXT("wheel-forged"));
	const bool bUnknownTemplateRejected =
		!State->ApplyTemplate(TEXT("unknown-template"));
	bInvalidInputsPassed = bInvalidPartRejected
		&& bCrossPartRejected
		&& bUnknownTemplateRejected
		&& State->GetSelection() == BeforeInvalidSelection
		&& State->GetCanonicalKey() == BeforeInvalidKey
		&& State->GetTotalPrice() == BeforeInvalidPrice
		&& EventCount == BeforeInvalidEvents;

	bEventCountPassed = EventCount == ExpectedEventCount;
	const bool bSuccess = bInitializationPassed
		&& bSixteenUniqueKeysPassed
		&& bPricesPassed
		&& bTemplatesPassed
		&& bInvalidInputsPassed
		&& bEventCountPassed;
	WriteReportAndExit(
		bSuccess,
		bSuccess ? FString() : TEXT("一个或多个配置状态检查失败。"));
}

void UConfigurationStateProbe::HandleStateChanged()
{
	++EventCount;
}

void UConfigurationStateProbe::WriteReportAndExit(
	const bool bSuccess,
	const FString& FailureReason)
{
	TArray<TSharedPtr<FJsonValue>> CombinationValues;
	for (int32 Index = 0; Index < CanonicalKeys.Num(); ++Index)
	{
		TSharedRef<FJsonObject> Combination = MakeShared<FJsonObject>();
		Combination->SetNumberField(TEXT("mask"), Index);
		Combination->SetStringField(TEXT("canonicalKey"), CanonicalKeys[Index]);
		Combination->SetNumberField(TEXT("totalPriceMinor"), ActualPrices[Index]);
		Combination->SetNumberField(TEXT("expectedPriceMinor"), ExpectedPrices[Index]);
		Combination->SetBoolField(
			TEXT("pricePassed"),
			ActualPrices[Index] == ExpectedPrices[Index]);
		CombinationValues.Add(MakeShared<FJsonValueObject>(Combination));
	}

	TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();
	Checks->SetBoolField(TEXT("initialization"), bInitializationPassed);
	Checks->SetBoolField(TEXT("sixteenUniqueCanonicalKeys"), bSixteenUniqueKeysPassed);
	Checks->SetBoolField(TEXT("allPrices"), bPricesPassed);
	Checks->SetBoolField(TEXT("templates"), bTemplatesPassed);
	Checks->SetBoolField(TEXT("invalidInputsLeaveStateUnchanged"), bInvalidInputsPassed);
	Checks->SetBoolField(TEXT("eventCount"), bEventCountPassed);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("probe"), TEXT("ConfigurationStateProbe"));
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetNumberField(TEXT("optionCount"), 8);
	Root->SetNumberField(TEXT("templateCount"), 2);
	Root->SetStringField(TEXT("canonicalOrder"), TEXT("paint__wheel__interior__frame"));
	Root->SetArrayField(TEXT("combinations"), CombinationValues);
	Root->SetNumberField(TEXT("uniqueKeyCount"), UniqueKeyCount);
	Root->SetNumberField(TEXT("eventCount"), EventCount);
	Root->SetNumberField(TEXT("expectedEventCount"), ExpectedEventCount);
	Root->SetObjectField(TEXT("checks"), Checks);
	Root->SetBoolField(TEXT("success"), bSuccess);
	Root->SetStringField(TEXT("failureReason"), FailureReason);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bSerialized = FJsonSerializer::Serialize(Root, Writer);
	const bool bDirectoryReady =
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	const bool bWritten = bSerialized
		&& bDirectoryReady
		&& FFileHelper::SaveStringToFile(
			JsonText,
			*OutputPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bWritten && bSuccess)
	{
		UE_LOG(
			LogConfigurationStateProbe,
			Display,
			TEXT("配置状态探针通过，JSON=%s"),
			*OutputPath);
	}
	else
	{
		UE_LOG(
			LogConfigurationStateProbe,
			Error,
			TEXT("配置状态探针失败：%s，JSON=%s"),
			FailureReason.IsEmpty() ? TEXT("报告写入失败") : *FailureReason,
			*OutputPath);
	}

	// 必须在请求退出前解除 Root，避免模块关闭晚于 UObject 数组销毁。
	Shutdown();
	FPlatformMisc::RequestExitWithStatus(false, bWritten && bSuccess ? 0 : 6);
}
