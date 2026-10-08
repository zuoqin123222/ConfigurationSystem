#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "MaterialVisualBaselineEditorSubsystem.generated.h"

class AMaterialVisualBaselineProbeActor;
class IConsoleObject;

/** Editor 命令入口：幂等创建固定基准地图，并在真实 Editor viewport 运行同一探针。 */
UCLASS()
class CONFIGURATIONSYSTEMEDITOR_API UMaterialVisualBaselineEditorSubsystem final
	: public UEditorSubsystem
{
	GENERATED_BODY()

public:
	static constexpr TCHAR MapPackageName[] =
		TEXT("/Game/Maps/L_MaterialVisualBaseline");
	static constexpr TCHAR ProbeActorLabel[] =
		TEXT("MaterialVisualBaselinePlane");
	static constexpr TCHAR LightActorLabel[] =
		TEXT("MaterialVisualBaselineDirectionalLight");
	static constexpr TCHAR CameraActorLabel[] =
		TEXT("MaterialVisualBaselineCamera");
	static constexpr TCHAR ExposureActorLabel[] =
		TEXT("MaterialVisualBaselineExposure");

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static bool CreateOrRefreshScene(FString& OutError);

private:
	void CreateSceneCommand(const TArray<FString>& Args);
	void RunProbeCommand(const TArray<FString>& Args);
	bool TickProbe(float DeltaSeconds);

	IConsoleObject* CreateSceneConsoleCommand = nullptr;
	IConsoleObject* RunProbeConsoleCommand = nullptr;
	FTSTicker::FDelegateHandle ProbeTickerHandle;
	TWeakObjectPtr<AMaterialVisualBaselineProbeActor> ActiveProbe;
};
