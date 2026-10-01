#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"

class AActor;
class UReversiblePartActuatorComponent;
class USceneComponent;

struct FNegativeHierarchyAuditTestResult
{
	FString Name;
	FString ExpectedIssueSubstring;
	bool bAuditFailed = false;
	bool bExpectedIssueFound = false;
	bool bRestoredAuditPassed = false;
	TArray<FString> Issues;

	bool Passed() const
	{
		return bAuditFailed && bExpectedIssueFound && bRestoredAuditPassed;
	}
};

/** 由 -VehicleHierarchyProbe 启用的 P0-4 Runtime 自动探针。 */
class FVehicleHierarchyProbe final
{
public:
	void Start();
	void Shutdown();

private:
	void OnEngineLoopInitComplete();
	bool Tick(float DeltaTime);
	bool BuildCubeHierarchy();
	void RunChecksAndFinish();
	void Finish(bool bSuccess, const FString& FailureReason);
	void RecordNegativeAudit(
		const FString& Name,
		const FString& ExpectedIssueSubstring,
		TFunction<void()> IntroduceError,
		TFunction<void()> Restore);

	static double TransformError(const FTransform& Left, const FTransform& Right);
	void SampleStep(float DeltaTime);
	void DriveTo(bool bOpen);

	FDelegateHandle EngineInitCompleteHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	TWeakObjectPtr<AActor> ProbeActor;
	TObjectPtr<UReversiblePartActuatorComponent> Actuator;
	TObjectPtr<USceneComponent> ActuatedPart;
	FString OutputPath;
	double StartTimeSeconds = 0.0;
	double TimeoutSeconds = 30.0;

	bool bCubeMeshLoaded = false;
	bool bHierarchyPassed = false;
	bool bStepJumpPassed = false;
	bool bReverseAtFortyPercentPassed = false;
	bool bFullOpenClosePassed = false;
	bool bThreeCyclesPassed = false;
	bool bSetOpenSameValuePassed = false;
	bool bAllInvalidFixturesRejected = false;
	int32 CompletedCycles = 0;
	double MaxCommandJump = 0.0;
	double MaxIdempotentCommandJump = 0.0;
	double MaxStepJump = 0.0;
	double MaxInterpolationError = 0.0;
	double MaxTerminalError = 0.0;
	TArray<FNegativeHierarchyAuditTestResult> NegativeAuditTests;
};
