#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IConsoleObject;
class SDockTab;
class FSpawnTabArgs;
class UCameraComponent;

/** 注册 Editor-only 探针与管理员资产接入界面。 */
class FConfigurationSystemEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	static void ApplyDefaultCameraFov(
		UCameraComponent* CameraComponent,
		float DefaultFov,
		bool bCameraWasCreated);

private:
	void RegisterMenus();
	TSharedRef<SDockTab> SpawnAdminImportTab(const FSpawnTabArgs& Args);
	bool TickAdminImportProbe(float DeltaTime);
	void RunAdminImportProbe();
	void CreatePrimaryAssetProbeAssets();
	void CreatePackagingProbeMap();
	void RequestCreateConfigShowroomMap();
	bool TickCreateConfigShowroomMap(float DeltaTime);
	void CreateConfigShowroomMap();
	void StartConfigurationBatchBake();

	IConsoleObject* CreateAssetsCommand = nullptr;
	IConsoleObject* CreateProjectMapCommand = nullptr;
	IConsoleObject* CreateShowroomMapCommand = nullptr;
	IConsoleObject* AdminImportProbeCommand = nullptr;
	IConsoleObject* ConfigurationBatchBakeCommand = nullptr;
	FTSTicker::FDelegateHandle AdminImportProbeTickerHandle;
	FTSTicker::FDelegateHandle CreateShowroomMapTickerHandle;
	bool bMenusRegistered = false;
};
