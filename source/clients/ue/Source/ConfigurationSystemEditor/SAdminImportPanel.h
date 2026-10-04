#pragma once

#include "AdminImportPreflight.h"
#include "ContentPackProviderManager.h"
#include "CoreMinimal.h"
#include "Widgets/Input/SComboBox.h"
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
	FReply BrowseRiggedVehicleFbx();
	FReply BrowseRiggedVehicleSidecar();
	FReply BrowseModelFbx();
	FReply BrowseModelSidecar();
	FReply BrowseAnimationFbx();
	FReply BrowseAnimationSidecar();
	FReply RunPreflight();
	FReply ImportApproved();
	FReply BrowseContentPackManifest();
	FReply BrowseContentPackPak();
	FReply RunContentPackPreflight();
	FReply ActivateContentPackProvider();
	FReply RollbackContentPackProvider();

	FReply BrowseInto(
		const TSharedPtr<SEditableTextBox>& Target,
		const FString& Title,
		const FString& Filter);
	void InvalidatePreflight();
	void RefreshStatus();
	bool CanImport() const;
	void InvalidateContentPackPreflight();
	void RefreshContentPackStatus();
	void HandleProviderTypeChanged(
		TSharedPtr<FString> NewSelection,
		ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> MakeProviderTypeOption(TSharedPtr<FString> Option) const;
	FText GetSelectedProviderTypeText() const;
	bool CanActivateContentPackProvider() const;
	bool CanRollbackContentPackProvider() const;
	EContentPackProviderType GetSelectedProviderType() const;

	TSharedPtr<SEditableTextBox> RiggedVehicleFbxTextBox;
	TSharedPtr<SEditableTextBox> RiggedVehicleSidecarTextBox;
	TSharedPtr<SEditableTextBox> ModelFbxTextBox;
	TSharedPtr<SEditableTextBox> ModelSidecarTextBox;
	TSharedPtr<SEditableTextBox> AnimationFbxTextBox;
	TSharedPtr<SEditableTextBox> AnimationSidecarTextBox;
	TSharedPtr<SMultiLineEditableTextBox> StatusTextBox;
	TOptional<FAdminImportPreflightResult> LastResult;
	TSharedPtr<SEditableTextBox> ContentPackManifestTextBox;
	TSharedPtr<SEditableTextBox> ContentPackPakTextBox;
	TSharedPtr<SMultiLineEditableTextBox> ContentPackStatusTextBox;
	TArray<TSharedPtr<FString>> ProviderTypeOptions;
	TSharedPtr<FString> SelectedProviderTypeOption;
	TUniquePtr<FContentPackProviderManager> ContentPackProviderManager;
	TOptional<FContentPackMountResult> LastContentPackResult;
	TArray<FString> LastContentPackErrors;
	bool bLastPreflightWasLegacyV1 = false;
};
