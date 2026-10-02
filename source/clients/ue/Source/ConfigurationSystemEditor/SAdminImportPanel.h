#pragma once

#include "AdminImportPreflight.h"
#include "ContentPackMountService.h"
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SMultiLineEditableTextBox;

/** Nomad-tab content for the editor-only administrator intake flow. */
class SAdminImportPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAdminImportPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply BrowseModelFbx();
	FReply BrowseModelSidecar();
	FReply BrowseAnimationFbx();
	FReply BrowseAnimationSidecar();
	FReply RunPreflight();
	FReply ImportApproved();
	FReply BrowseContentPackManifest();
	FReply BrowseContentPackPak();
	FReply RunContentPackPreflight();
	FReply MountContentPack();

	FReply BrowseInto(
		const TSharedPtr<SEditableTextBox>& Target,
		const FString& Title,
		const FString& Filter);
	void InvalidatePreflight();
	void RefreshStatus();
	bool CanImport() const;
	void InvalidateContentPackPreflight();
	void RefreshContentPackStatus();
	bool CanMountContentPack() const;

	TSharedPtr<SEditableTextBox> ModelFbxTextBox;
	TSharedPtr<SEditableTextBox> ModelSidecarTextBox;
	TSharedPtr<SEditableTextBox> AnimationFbxTextBox;
	TSharedPtr<SEditableTextBox> AnimationSidecarTextBox;
	TSharedPtr<SMultiLineEditableTextBox> StatusTextBox;
	TOptional<FAdminImportPreflightResult> LastResult;
	TSharedPtr<SEditableTextBox> ContentPackManifestTextBox;
	TSharedPtr<SEditableTextBox> ContentPackPakTextBox;
	TSharedPtr<SMultiLineEditableTextBox> ContentPackStatusTextBox;
	TUniquePtr<FContentPackMountService> ContentPackMountService;
	TOptional<FContentPackMountResult> LastContentPackResult;
};
