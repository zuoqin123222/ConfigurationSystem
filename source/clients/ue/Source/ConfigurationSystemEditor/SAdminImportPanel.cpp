#include "SAdminImportPanel.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
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
	const FOnTextChanged InvalidateDelegate = FOnTextChanged::CreateLambda(
		[this](const FText&)
		{
			InvalidatePreflight();
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
							"模型与动画可单独或同时预检。只有关键字段、文件大小和 SHA-256 全部通过后，"
							"才可批准导入唯一暂存目录；本工具不会写入正式资产目录。"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("ModelFbx", "模型 FBX"),
						ModelFbxTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseModelFbx),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("ModelSidecar", "模型 sidecar"),
						ModelSidecarTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseModelSidecar),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("AnimationFbx", "动画 FBX"),
						AnimationFbxTextBox,
						FOnClicked::CreateSP(this, &SAdminImportPanel::BrowseAnimationFbx),
						InvalidateDelegate)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 3.0f)
				[
					MakeFileRow(
						LOCTEXT("AnimationSidecar", "动画 sidecar"),
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
				.FillHeight(1.0f)
				[
					SAssignNew(StatusTextBox, SMultiLineEditableTextBox)
						.IsReadOnly(true)
						.AutoWrapText(true)
						.Text(LOCTEXT("InitialStatus", "尚未运行预检。"))
				]
			]
		]
	];
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
		InvalidatePreflight();
	}
	return FReply::Handled();
}

FReply SAdminImportPanel::RunPreflight()
{
	TArray<FAdminImportSelection> Selections;
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
			Item.Kind == EAdminImportAssetKind::Model ? TEXT("模型") : TEXT("动画"),
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

#undef LOCTEXT_NAMESPACE
