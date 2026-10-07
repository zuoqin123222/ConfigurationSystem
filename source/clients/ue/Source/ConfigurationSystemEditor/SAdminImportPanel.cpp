#include "SAdminImportPanel.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ConfigurationSystemAdminImport"

namespace
{
	TSharedRef<SWidget> MakeFileRow(
		const FText& Label,
		TSharedPtr<SEditableTextBox>& TextBox,
		const FOnClicked& OnBrowse,
		const FOnTextChanged& OnChanged)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(112.0f)
				[
					SNew(STextBlock).Text(Label)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew(TextBox, SEditableTextBox)
				.OnTextChanged(OnChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Browse", "浏览…"))
				.OnClicked(OnBrowse)
			];
	}
}

void SAdminImportPanel::Construct(const FArguments& InArgs)
{
	ProviderTypeOptions = {
		MakeShared<FString>(TEXT("material")),
		MakeShared<FString>(TEXT("environment")),
		MakeShared<FString>(TEXT("vehicle"))
	};
	SelectedProviderTypeOption = ProviderTypeOptions[0];

	FContentPackProviderManagerPolicy ContentPackPolicy;
	ContentPackPolicy.CatalogVersion = TEXT("sc01-draft-20261007");
	ContentPackPolicy.EngineVersion = TEXT("5.8");
	ContentPackPolicy.Platform = TEXT("Win64");
	ContentPackPolicy.RegistryPath = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("ContentPackProviders"),
		TEXT("active-registry.json"));
	const FString RegistryPath = ContentPackPolicy.RegistryPath;
	ContentPackProviderManager =
		MakeUnique<FContentPackProviderManager>(MoveTemp(ContentPackPolicy));
	if (IFileManager::Get().FileExists(*RegistryPath))
	{
		ContentPackProviderManager->RestoreActiveProviders(LastContentPackErrors);
	}

	const FOnTextChanged InvalidateDelegate = FOnTextChanged::CreateLambda(
		[this](const FText&)
		{
			InvalidatePreflight();
		});
	const FOnTextChanged InvalidateContentPackDelegate = FOnTextChanged::CreateLambda(
		[this](const FText&)
		{
			InvalidateContentPackPreflight();
		});

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(16.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("Title", "Configuration System 管理员导入"))
						.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 14.0f)
				[
					SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT(
							"Explanation",
							"首选单个骨骼车辆 FBX + v2 sidecar，一次导入 SkeletalMesh、Skeleton "
							"与一条完整 AnimSequence。只有关键字段、文件大小和 SHA-256 全部通过后，"
							"才可批准导入唯一暂存目录；本工具不会写入正式资产目录。"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("RiggedVehicleFbx", "骨骼车辆 FBX"),
						RiggedVehicleFbxTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseRiggedVehicleFbx),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("RiggedVehicleSidecar", "骨骼车辆 sidecar"),
						RiggedVehicleSidecarTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseRiggedVehicleSidecar),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
						.Text(LOCTEXT("LegacyImport", "兼容入口：分离模型 / 动画 FBX（v1）"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("ModelFbx", "旧模型 FBX"),
						ModelFbxTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseModelFbx),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("ModelSidecar", "旧模型 sidecar"),
						ModelSidecarTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseModelSidecar),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("AnimationFbx", "旧动画 FBX"),
						AnimationFbxTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseAnimationFbx),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("AnimationSidecar", "旧动画 sidecar"),
						AnimationSidecarTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseAnimationSidecar),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 14.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SUniformGridPanel)
						.SlotPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
						+ SUniformGridPanel::Slot(0, 0)
						[
							SNew(SButton)
								.Text(LOCTEXT("Preflight", "运行 C++ 预检"))
								.HAlign(HAlign_Center)
								.OnClicked(this, &SAdminImportPanel::RunPreflight)
						]
						+ SUniformGridPanel::Slot(1, 0)
						[
							SNew(SButton)
								.Text(LOCTEXT("ApproveImport", "批准并导入暂存区"))
								.HAlign(HAlign_Center)
								.IsEnabled(this, &SAdminImportPanel::CanImport)
								.OnClicked(this, &SAdminImportPanel::ImportApproved)
						]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 14.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("Result", "结果"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MinDesiredHeight(120.0f)
					[
						SAssignNew(StatusTextBox, SMultiLineEditableTextBox)
							.IsReadOnly(true)
							.AutoWrapText(true)
							.Text(LOCTEXT("InitialStatus", "尚未运行预检。"))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 18.0f, 0.0f, 14.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("ContentPackTitle", "SC01 Provider 管理"))
						.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 10.0f)
				[
					SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT(
							"ContentPackExplanation",
							"固定策略：catalog=sc01-draft-20261007、engine=5.8、platform=Win64。"
							"SC01 Provider 通过预检后才可激活并写入持久 registry；"
							"旧 mvp-v1 manifest 仅保留预检兼容，不能激活。"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(112.0f)
						[
							SNew(STextBlock).Text(LOCTEXT("ProviderType", "Provider 类型"))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&ProviderTypeOptions)
							.InitiallySelectedItem(SelectedProviderTypeOption)
							.OnGenerateWidget(
								this, &SAdminImportPanel::MakeProviderTypeOption)
							.OnSelectionChanged(
								this, &SAdminImportPanel::HandleProviderTypeChanged)
							[
								SNew(STextBlock)
									.Text(this, &SAdminImportPanel::GetSelectedProviderTypeText)
							]
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("ContentPackManifest", "内容包 manifest"),
						ContentPackManifestTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseContentPackManifest),
						InvalidateContentPackDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("ContentPackPak", "内容包 .pak"),
						ContentPackPakTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseContentPackPak),
						InvalidateContentPackDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f)
				[
					SNew(SUniformGridPanel)
						.SlotPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f))
						+ SUniformGridPanel::Slot(0, 0)
						[
							SNew(SButton)
								.Text(LOCTEXT("ContentPackPreflight", "预检 Provider"))
								.HAlign(HAlign_Center)
								.OnClicked(this, &SAdminImportPanel::RunContentPackPreflight)
						]
						+ SUniformGridPanel::Slot(1, 0)
						[
							SNew(SButton)
								.Text(LOCTEXT("ActivateContentPack", "激活 Provider"))
								.HAlign(HAlign_Center)
								.IsEnabled(
									this, &SAdminImportPanel::CanActivateContentPackProvider)
								.OnClicked(
									this, &SAdminImportPanel::ActivateContentPackProvider)
						]
						+ SUniformGridPanel::Slot(2, 0)
						[
							SNew(SButton)
								.Text(LOCTEXT("RollbackContentPack", "回滚"))
								.HAlign(HAlign_Center)
								.IsEnabled(
									this, &SAdminImportPanel::CanRollbackContentPackProvider)
								.OnClicked(
									this, &SAdminImportPanel::RollbackContentPackProvider)
						]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock).Text(LOCTEXT("ContentPackResult", "材质包结果"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.MinDesiredHeight(120.0f)
					[
						SAssignNew(ContentPackStatusTextBox, SMultiLineEditableTextBox)
							.IsReadOnly(true)
							.AutoWrapText(true)
							.Text(LOCTEXT(
								"ContentPackInitialStatus",
								"尚未预检 Provider。current / previous 状态将在此显示。"))
					]
				]
			]
		]
	];
	RefreshContentPackStatus();
}

FReply SAdminImportPanel::BrowseRiggedVehicleFbx()
{
	return BrowseInto(
		RiggedVehicleFbxTextBox,
		TEXT("选择骨骼车辆 FBX"),
		TEXT("FBX (*.fbx)|*.fbx"));
}

FReply SAdminImportPanel::BrowseRiggedVehicleSidecar()
{
	return BrowseInto(
		RiggedVehicleSidecarTextBox,
		TEXT("选择骨骼车辆 sidecar"),
		TEXT("JSON (*.json)|*.json"));
}

FReply SAdminImportPanel::BrowseModelFbx()
{
	return BrowseInto(ModelFbxTextBox, TEXT("选择模型 FBX"), TEXT("FBX (*.fbx)|*.fbx"));
}

FReply SAdminImportPanel::BrowseModelSidecar()
{
	return BrowseInto(ModelSidecarTextBox, TEXT("选择模型 sidecar"), TEXT("JSON (*.json)|*.json"));
}

FReply SAdminImportPanel::BrowseAnimationFbx()
{
	return BrowseInto(AnimationFbxTextBox, TEXT("选择动画 FBX"), TEXT("FBX (*.fbx)|*.fbx"));
}

FReply SAdminImportPanel::BrowseAnimationSidecar()
{
	return BrowseInto(AnimationSidecarTextBox, TEXT("选择动画 sidecar"), TEXT("JSON (*.json)|*.json"));
}

FReply SAdminImportPanel::BrowseContentPackManifest()
{
	return BrowseInto(
		ContentPackManifestTextBox,
		TEXT("选择材质包 manifest"),
		TEXT("JSON (*.json)|*.json"));
}

FReply SAdminImportPanel::BrowseContentPackPak()
{
	return BrowseInto(
		ContentPackPakTextBox,
		TEXT("选择材质内容包"),
		TEXT("Unreal Pak (*.pak)|*.pak"));
}

FReply SAdminImportPanel::BrowseInto(
	const TSharedPtr<SEditableTextBox>& Target,
	const FString& Title,
	const FString& Filter)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform == nullptr || !Target.IsValid())
	{
		return FReply::Handled();
	}

	TArray<FString> Filenames;
	const FString CurrentPath = Target->GetText().ToString();
	const FString DefaultPath = CurrentPath.IsEmpty()
		? FPaths::ProjectDir()
		: FPaths::GetPath(CurrentPath);
	if (DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		Title,
		DefaultPath,
		FString(),
		Filter,
		EFileDialogFlags::None,
		Filenames)
		&& !Filenames.IsEmpty())
	{
		Target->SetText(FText::FromString(Filenames[0]));
	}
	return FReply::Handled();
}

FReply SAdminImportPanel::RunPreflight()
{
	TArray<FAdminImportSelection> Selections;
	const FString RiggedVehicleFbx =
		RiggedVehicleFbxTextBox->GetText().ToString().TrimStartAndEnd();
	const FString RiggedVehicleSidecar =
		RiggedVehicleSidecarTextBox->GetText().ToString().TrimStartAndEnd();
	if (!RiggedVehicleFbx.IsEmpty() || !RiggedVehicleSidecar.IsEmpty())
	{
		FAdminImportSelection& Selection = Selections.AddDefaulted_GetRef();
		Selection.Kind = EAdminImportAssetKind::RiggedVehicle;
		Selection.FbxFile = RiggedVehicleFbx;
		Selection.SidecarFile = RiggedVehicleSidecar;
	}

	const FString ModelFbx = ModelFbxTextBox->GetText().ToString().TrimStartAndEnd();
	const FString ModelSidecar = ModelSidecarTextBox->GetText().ToString().TrimStartAndEnd();
	if (!ModelFbx.IsEmpty() || !ModelSidecar.IsEmpty())
	{
		FAdminImportSelection& Selection = Selections.AddDefaulted_GetRef();
		Selection.Kind = EAdminImportAssetKind::Model;
		Selection.FbxFile = ModelFbx;
		Selection.SidecarFile = ModelSidecar;
	}

	const FString AnimationFbx = AnimationFbxTextBox->GetText().ToString().TrimStartAndEnd();
	const FString AnimationSidecar = AnimationSidecarTextBox->GetText().ToString().TrimStartAndEnd();
	if (!AnimationFbx.IsEmpty() || !AnimationSidecar.IsEmpty())
	{
		FAdminImportSelection& Selection = Selections.AddDefaulted_GetRef();
		Selection.Kind = EAdminImportAssetKind::Animation;
		Selection.FbxFile = AnimationFbx;
		Selection.SidecarFile = AnimationSidecar;
	}

	LastResult = FAdminImportPreflight::Run(Selections);
	RefreshStatus();
	return FReply::Handled();
}

FReply SAdminImportPanel::ImportApproved()
{
	if (CanImport())
	{
		FAdminImportService::ImportApproved(LastResult.GetValue());
		RefreshStatus();
	}
	return FReply::Handled();
}

void SAdminImportPanel::InvalidatePreflight()
{
	LastResult.Reset();
	if (StatusTextBox.IsValid())
	{
		StatusTextBox->SetText(LOCTEXT("Invalidated", "选择已改变，请重新运行预检。"));
	}
}

void SAdminImportPanel::RefreshStatus()
{
	if (!StatusTextBox.IsValid() || !LastResult.IsSet())
	{
		return;
	}

	const FAdminImportPreflightResult& Result = LastResult.GetValue();
	FString Status = FString::Printf(
		TEXT("预检：%s\n会话：%s\n暂存目标：%s\nJSON 报告：%s\n"),
		Result.bPassed ? TEXT("通过") : TEXT("失败"),
		*Result.SessionId,
		*Result.StagingPath,
		*Result.ReportPath);
	for (const FAdminImportItemResult& Item : Result.Items)
	{
		Status += FString::Printf(
			TEXT("\n[%s] %s\n文件：%s\n大小：%lld / %lld\nSHA-256：%s\n"),
			Item.Kind == EAdminImportAssetKind::RiggedVehicle
				? TEXT("骨骼车辆")
				: Item.Kind == EAdminImportAssetKind::Model ? TEXT("模型") : TEXT("动画"),
			Item.bPassed ? TEXT("通过") : TEXT("失败"),
			*Item.FbxFile,
			Item.ActualBytes,
			Item.ExpectedBytes,
			*Item.ActualSha256);
		for (const FString& Error : Item.Errors)
		{
			Status += TEXT("错误：") + Error + TEXT("\n");
		}
	}
	if (Result.bImportAttempted)
	{
		Status += FString::Printf(
			TEXT("\n导入：%s\n"),
			Result.bImportSucceeded ? TEXT("成功") : TEXT("失败"));
		for (const FString& ObjectPath : Result.ImportedObjectPaths)
		{
			Status += ObjectPath + TEXT("\n");
		}
	}
	StatusTextBox->SetText(FText::FromString(Status));
}

bool SAdminImportPanel::CanImport() const
{
	return LastResult.IsSet()
		&& LastResult->bPassed
		&& !LastResult->bImportAttempted;
}

FReply SAdminImportPanel::RunContentPackPreflight()
{
	if (ContentPackProviderManager.IsValid())
	{
		LastContentPackErrors.Reset();
		LastContentPackResult = ContentPackProviderManager->Preflight(
			GetSelectedProviderType(),
			ContentPackManifestTextBox->GetText().ToString().TrimStartAndEnd(),
			ContentPackPakTextBox->GetText().ToString().TrimStartAndEnd(),
			&bLastPreflightWasLegacyV1);
		RefreshContentPackStatus();
	}
	return FReply::Handled();
}

FReply SAdminImportPanel::ActivateContentPackProvider()
{
	if (CanActivateContentPackProvider())
	{
		const FContentPackProviderActivationResult Result =
			ContentPackProviderManager->Activate(
				GetSelectedProviderType(),
				ContentPackManifestTextBox->GetText().ToString().TrimStartAndEnd(),
				ContentPackPakTextBox->GetText().ToString().TrimStartAndEnd());
		LastContentPackResult = Result.MountResult;
		LastContentPackErrors = Result.Errors;
		bLastPreflightWasLegacyV1 = false;
		RefreshContentPackStatus();
	}
	return FReply::Handled();
}

FReply SAdminImportPanel::RollbackContentPackProvider()
{
	if (CanRollbackContentPackProvider())
	{
		LastContentPackErrors.Reset();
		ContentPackProviderManager->Rollback(
			GetSelectedProviderType(), LastContentPackErrors);
		LastContentPackResult.Reset();
		bLastPreflightWasLegacyV1 = false;
		RefreshContentPackStatus();
	}
	return FReply::Handled();
}

void SAdminImportPanel::HandleProviderTypeChanged(
	TSharedPtr<FString> NewSelection,
	const ESelectInfo::Type SelectInfo)
{
	(void)SelectInfo;
	if (NewSelection.IsValid())
	{
		SelectedProviderTypeOption = MoveTemp(NewSelection);
		InvalidateContentPackPreflight();
		RefreshContentPackStatus();
	}
}

TSharedRef<SWidget> SAdminImportPanel::MakeProviderTypeOption(
	TSharedPtr<FString> Option) const
{
	return SNew(STextBlock).Text(FText::FromString(
		Option.IsValid() ? *Option : FString()));
}

FText SAdminImportPanel::GetSelectedProviderTypeText() const
{
	return FText::FromString(
		SelectedProviderTypeOption.IsValid()
			? *SelectedProviderTypeOption
			: FString(TEXT("material")));
}

EContentPackProviderType SAdminImportPanel::GetSelectedProviderType() const
{
	EContentPackProviderType Type = EContentPackProviderType::Material;
	if (SelectedProviderTypeOption.IsValid())
	{
		FContentPackProviderManager::TryParseProviderType(
			*SelectedProviderTypeOption, Type);
	}
	return Type;
}

bool SAdminImportPanel::CanActivateContentPackProvider() const
{
	return ContentPackProviderManager.IsValid()
		&& LastContentPackResult.IsSet()
		&& LastContentPackResult->bPreflightPassed
		&& !LastContentPackResult->bMounted
		&& !bLastPreflightWasLegacyV1;
}

bool SAdminImportPanel::CanRollbackContentPackProvider() const
{
	if (!ContentPackProviderManager.IsValid())
	{
		return false;
	}
	const FContentPackProviderSlot* Slot =
		ContentPackProviderManager->FindActive(GetSelectedProviderType());
	return Slot != nullptr && Slot->Previous.IsSet();
}

void SAdminImportPanel::InvalidateContentPackPreflight()
{
	LastContentPackResult.Reset();
	LastContentPackErrors.Reset();
	bLastPreflightWasLegacyV1 = false;
	if (ContentPackStatusTextBox.IsValid())
	{
		ContentPackStatusTextBox->SetText(
			LOCTEXT("ContentPackInvalidated", "Provider 选择已改变，请重新运行预检。"));
	}
}

void SAdminImportPanel::RefreshContentPackStatus()
{
	if (!ContentPackStatusTextBox.IsValid())
	{
		return;
	}

	const EContentPackProviderType Type = GetSelectedProviderType();
	FString Status = FString::Printf(
		TEXT("策略：catalog=sc01-draft-20261007；engine=5.8；platform=Win64\n"
			"类型：%s\n"),
		*FContentPackProviderManager::LexToString(Type));

	if (LastContentPackResult.IsSet())
	{
		const FContentPackMountResult& Result = LastContentPackResult.GetValue();
		Status += FString::Printf(
			TEXT("预检：%s%s\n激活挂载：%s\n"
				"包：%s@%s\n挂载点：%s\nPrimaryAssetId：%d\n"
				"实际 SHA-256：%s\n"),
			Result.bPreflightPassed ? TEXT("通过") : TEXT("失败"),
			bLastPreflightWasLegacyV1 ? TEXT("（旧 mvp-v1，仅兼容预检）") : TEXT(""),
			Result.bMounted ? TEXT("成功") : TEXT("未激活"),
			*Result.Manifest.PackId,
			*Result.Manifest.Version,
			*Result.Manifest.MountPoint,
			Result.Manifest.PrimaryAssetIds.Num(),
			*Result.ActualPakSha256);
		for (const FString& Error : Result.Errors)
		{
			Status += TEXT("错误：") + Error + TEXT("\n");
		}
	}

	for (const FString& Error : LastContentPackErrors)
	{
		Status += TEXT("错误：") + Error + TEXT("\n");
	}

	const FContentPackProviderSlot* Slot = ContentPackProviderManager.IsValid()
		? ContentPackProviderManager->FindActive(Type)
		: nullptr;
	if (Slot == nullptr)
	{
		Status += TEXT("current：无\nprevious：无\n");
	}
	else
	{
		Status += FString::Printf(
			TEXT("current：%s@%s\n"),
			*Slot->Current.PackId,
			*Slot->Current.Version);
		Status += Slot->Previous.IsSet()
			? FString::Printf(
				TEXT("previous：%s@%s\n"),
				*Slot->Previous->PackId,
				*Slot->Previous->Version)
			: FString(TEXT("previous：无\n"));
	}
	ContentPackStatusTextBox->SetText(FText::FromString(Status));
}

#undef LOCTEXT_NAMESPACE
