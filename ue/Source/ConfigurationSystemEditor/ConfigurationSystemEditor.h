#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IConsoleObject;

/** 注册可重复执行的测试资产创建命令。 */
class FConfigurationSystemEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void CreatePrimaryAssetProbeAssets();

	IConsoleObject* CreateAssetsCommand = nullptr;
};
