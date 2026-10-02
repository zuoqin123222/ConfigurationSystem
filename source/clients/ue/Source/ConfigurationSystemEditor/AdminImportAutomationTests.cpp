#include "AdminImportPreflight.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAdminImportPreflightAutomationTest,
	"ConfigurationSystem.Editor.AdminImport.Preflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAdminImportPreflightAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString Session = TEXT("automation-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString TestDirectory = FPaths::Combine(
		FPaths::ProjectIntermediateDir(), TEXT("AdminImportTests"), Session);
	IFileManager::Get().MakeDirectory(*TestDirectory, true);

	const FString FbxPath = FPaths::Combine(TestDirectory, TEXT("test-model.fbx"));
	const FString SidecarPath = FPaths::Combine(TestDirectory, TEXT("test-model.json"));
	TestTrue(
		TEXT("创建测试 FBX"),
		FFileHelper::SaveStringToFile(
			TEXT("ConfigurationSystem admin import SHA-256 probe"),
			*FbxPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FString Sha256;
	FString HashError;
	TestTrue(
		TEXT("计算测试 FBX SHA-256"),
		FAdminImportPreflight::ComputeFileSha256(FbxPath, Sha256, HashError));
	TestEqual(TEXT("SHA-256 长度"), Sha256.Len(), 64);
	TestEqual(
		TEXT("SHA-256 标准向量"),
		Sha256,
		FString(TEXT("f15d9f8b5ccbe1f74dc2219e09770843a6b9179e41fa5e4c00537c1c0a31a1aa")));
	const int64 FileBytes = IFileManager::Get().FileSize(*FbxPath);

	const FString ValidJson = FString::Printf(
		TEXT(R"JSON({
  "schemaVersion":"1.0.0",
  "kind":"vehicle-model",
  "vehicleId":"automation-car",
  "modelVersion":"model-1",
  "coordinateSystem":{"unit":"centimeter","forward":"+X","right":"+Y","up":"+Z","handedness":"left"},
  "source":{"dcc":"blender","dccVersion":"4.5","file":{"path":"source.blend","sha256":"0000000000000000000000000000000000000000000000000000000000000000","bytes":1}},
  "export":{"format":"FBX","fbxVersion":"2020.2","binary":true,"bakeTransforms":true},
  "nodes":[{}],
  "materialSlots":[{}],
  "partBindings":[{}],
  "lods":[{}],
  "authorization":{"rightsHolder":"Automation","licenseId":"test","permittedUses":["modify","unreal-import"],"territory":"test","expiresOn":null,"redistribution":"none"},
  "artifacts":{"fbx":{"path":"test-model.fbx","sha256":"%s","bytes":%lld}}
})JSON"),
		*Sha256,
		FileBytes);
	TestTrue(
		TEXT("创建有效 sidecar"),
		FFileHelper::SaveStringToFile(
			ValidJson,
			*SidecarPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

	FAdminImportSelection Selection;
	Selection.Kind = EAdminImportAssetKind::Model;
	Selection.FbxFile = FbxPath;
	Selection.SidecarFile = SidecarPath;
	FAdminImportPreflightResult ValidResult =
		FAdminImportPreflight::Run({Selection}, Session + TEXT("-valid"));
	TestTrue(TEXT("有效 sidecar 通过"), ValidResult.bPassed);
	TestTrue(TEXT("有效预检写出 JSON"), FPaths::FileExists(ValidResult.ReportPath));
	TestEqual(TEXT("暂存目录固定"), ValidResult.StagingPath,
		FString(TEXT("/Game/Configurator/_ImportStaging/")) + Session + TEXT("-valid"));

	const FString InvalidJson = ValidJson.Replace(
		*Sha256,
		TEXT("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
		ESearchCase::CaseSensitive);
	TestTrue(
		TEXT("创建哈希不匹配 sidecar"),
		FFileHelper::SaveStringToFile(
			InvalidJson,
			*SidecarPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	FAdminImportPreflightResult InvalidResult =
		FAdminImportPreflight::Run({Selection}, Session + TEXT("-invalid"));
	TestFalse(TEXT("SHA-256 不匹配被拒绝"), InvalidResult.bPassed);
	TestTrue(
		TEXT("失败原因包含 SHA-256"),
		!InvalidResult.Items.IsEmpty()
			&& InvalidResult.Items[0].Errors.ContainsByPredicate(
				[](const FString& Error)
				{
					return Error.Contains(TEXT("SHA-256 不匹配"));
				}));

	const FString AnimationFbxPath = FPaths::Combine(TestDirectory, TEXT("test-animation.fbx"));
	const FString AnimationSidecarPath = FPaths::Combine(TestDirectory, TEXT("test-animation.json"));
	TestTrue(
		TEXT("创建测试动画 FBX"),
		FFileHelper::SaveStringToFile(
			TEXT("ConfigurationSystem admin import SHA-256 probe"),
			*AnimationFbxPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	const FString AnimationJson = FString::Printf(
		TEXT(R"JSON({
  "schemaVersion":"1.0.0",
  "kind":"vehicle-animation",
  "animationVersion":"animation-1",
  "modelRef":{"vehicleId":"automation-car","modelVersion":"model-1","fbxSha256":"0000000000000000000000000000000000000000000000000000000000000000"},
  "coordinateSystem":{"unit":"centimeter","forward":"+X","right":"+Y","up":"+Z","handedness":"left"},
  "export":{"format":"FBX","fbxVersion":"2020.2","binary":true,"bakeAnimation":true,"resampleAll":true},
  "clips":[{}],
  "authorization":{"rightsHolder":"Automation","licenseId":"test","permittedUses":["modify","unreal-import"],"territory":"test","expiresOn":null,"redistribution":"none"},
  "artifacts":[{"clipId":"test-clip","path":"test-animation.fbx","sha256":"%s","bytes":%lld}]
})JSON"),
		*Sha256,
		FileBytes);
	TestTrue(
		TEXT("创建有效动画 sidecar"),
		FFileHelper::SaveStringToFile(
			AnimationJson,
			*AnimationSidecarPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	Selection.Kind = EAdminImportAssetKind::Animation;
	Selection.FbxFile = AnimationFbxPath;
	Selection.SidecarFile = AnimationSidecarPath;
	FAdminImportPreflightResult AnimationResult =
		FAdminImportPreflight::Run({Selection}, Session + TEXT("-animation"));
	TestTrue(TEXT("有效动画 sidecar 通过"), AnimationResult.bPassed);

	IFileManager::Get().Delete(*ValidResult.ReportPath);
	IFileManager::Get().Delete(*InvalidResult.ReportPath);
	IFileManager::Get().Delete(*AnimationResult.ReportPath);
	IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
	return true;
}

#endif
