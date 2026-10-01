#include "ConfigurationSystemEditor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "PrimaryAssetProbeData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogPrimaryAssetProbeEditor, Log, All);

namespace PrimaryAssetProbeEditor
{
	constexpr TCHAR TexturePackageName[] = TEXT("/Game/PrimaryAssetProbe/T_ProbeUnreferenced");
	constexpr TCHAR TextureAssetName[] = TEXT("T_ProbeUnreferenced");
	constexpr TCHAR DataPackageName[] = TEXT("/Game/PrimaryAssetProbe/DA_ProbeUnreferenced");
	constexpr TCHAR DataAssetName[] = TEXT("DA_ProbeUnreferenced");

	template <typename AssetType>
	AssetType* LoadOrCreateAsset(const TCHAR* PackageName, const TCHAR* AssetName, bool& bWasCreated)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackageName, AssetName);
		if (AssetType* ExistingAsset = LoadObject<AssetType>(nullptr, *ObjectPath))
		{
			bWasCreated = false;
			return ExistingAsset;
		}

		UPackage* Package = CreatePackage(PackageName);
		bWasCreated = true;
		return NewObject<AssetType>(Package, AssetName, RF_Public | RF_Standalone);
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}
}

IMPLEMENT_MODULE(FConfigurationSystemEditorModule, ConfigurationSystemEditor);

void FConfigurationSystemEditorModule::StartupModule()
{
	CreateAssetsCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PrimaryAssetProbe.CreateTestAssets"),
		TEXT("创建或刷新未被地图硬引用的测试纹理和 Primary Data Asset。"),
		FConsoleCommandDelegate::CreateRaw(this, &FConfigurationSystemEditorModule::CreatePrimaryAssetProbeAssets),
		ECVF_Default);
}

void FConfigurationSystemEditorModule::ShutdownModule()
{
	if (CreateAssetsCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(CreateAssetsCommand);
		CreateAssetsCommand = nullptr;
	}
}

void FConfigurationSystemEditorModule::CreatePrimaryAssetProbeAssets()
{
	using namespace PrimaryAssetProbeEditor;

	bool bTextureCreated = false;
	UTexture2D* Texture = LoadOrCreateAsset<UTexture2D>(
		TexturePackageName,
		TextureAssetName,
		bTextureCreated);

	// 固定 2x2 BGRA 像素，重复运行会得到同样的资产内容。
	const uint8 Pixels[] =
	{
		0, 0, 255, 255,       0, 255, 0, 255,
		255, 0, 0, 255,       255, 255, 255, 255
	};
	Texture->Source.Init(2, 2, 1, 1, TSF_BGRA8, Pixels);
	Texture->SRGB = true;
	Texture->NeverStream = true;
	Texture->PostEditChange();

	if (bTextureCreated)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}

	bool bDataAssetCreated = false;
	UPrimaryAssetProbeData* DataAsset = LoadOrCreateAsset<UPrimaryAssetProbeData>(
		DataPackageName,
		DataAssetName,
		bDataAssetCreated);
	DataAsset->ProbeTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Texture));
	DataAsset->PostEditChange();

	if (bDataAssetCreated)
	{
		FAssetRegistryModule::AssetCreated(DataAsset);
	}

	const bool bTextureSaved = SaveAsset(Texture);
	const bool bDataAssetSaved = SaveAsset(DataAsset);
	if (bTextureSaved && bDataAssetSaved)
	{
		UE_LOG(
			LogPrimaryAssetProbeEditor,
			Display,
			TEXT("测试资产已创建/刷新：%s，%s"),
			TexturePackageName,
			DataPackageName);
	}
	else
	{
		UE_LOG(LogPrimaryAssetProbeEditor, Error, TEXT("测试资产保存失败，请检查 Content 目录是否可写。"));
	}
}
