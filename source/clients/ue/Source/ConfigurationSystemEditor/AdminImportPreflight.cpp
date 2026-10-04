#include "AdminImportPreflight.h"

#include "AssetImportTask.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxImportUI.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogAdminImport, Log, All);

namespace AdminImport
{
	constexpr TCHAR StagingRoot[] = TEXT("/Game/Configurator/_ImportStaging");

	// UE's generic platform SHA-256 entry point is intentionally unimplemented on
	// some desktop builds. Keep the intake gate deterministic and cross-platform.
	struct FSha256
	{
		uint32 State[8] = {
			0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
			0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
		};
		uint8 Buffer[64] = {};
		uint64 TotalBytes = 0;
		uint32 BufferedBytes = 0;

		static uint32 RotateRight(const uint32 Value, const uint32 Count)
		{
			return (Value >> Count) | (Value << (32u - Count));
		}

		void Transform(const uint8* Block)
		{
			static constexpr uint32 K[64] = {
				0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
				0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
				0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
				0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
				0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
				0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
				0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
				0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
			};
			uint32 W[64];
			for (uint32 Index = 0; Index < 16; ++Index)
			{
				const uint32 Offset = Index * 4;
				W[Index] = (static_cast<uint32>(Block[Offset]) << 24)
					| (static_cast<uint32>(Block[Offset + 1]) << 16)
					| (static_cast<uint32>(Block[Offset + 2]) << 8)
					| static_cast<uint32>(Block[Offset + 3]);
			}
			for (uint32 Index = 16; Index < 64; ++Index)
			{
				const uint32 S0 = RotateRight(W[Index - 15], 7)
					^ RotateRight(W[Index - 15], 18) ^ (W[Index - 15] >> 3);
				const uint32 S1 = RotateRight(W[Index - 2], 17)
					^ RotateRight(W[Index - 2], 19) ^ (W[Index - 2] >> 10);
				W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
			}

			uint32 A = State[0], B = State[1], C = State[2], D = State[3];
			uint32 E = State[4], F = State[5], G = State[6], H = State[7];
			for (uint32 Index = 0; Index < 64; ++Index)
			{
				const uint32 S1 = RotateRight(E, 6) ^ RotateRight(E, 11) ^ RotateRight(E, 25);
				const uint32 Choice = (E & F) ^ (~E & G);
				const uint32 Temp1 = H + S1 + Choice + K[Index] + W[Index];
				const uint32 S0 = RotateRight(A, 2) ^ RotateRight(A, 13) ^ RotateRight(A, 22);
				const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
				const uint32 Temp2 = S0 + Majority;
				H = G; G = F; F = E; E = D + Temp1;
				D = C; C = B; B = A; A = Temp1 + Temp2;
			}
			State[0] += A; State[1] += B; State[2] += C; State[3] += D;
			State[4] += E; State[5] += F; State[6] += G; State[7] += H;
		}

		void Update(const uint8* Data, uint64 Length)
		{
			TotalBytes += Length;
			while (Length > 0)
			{
				const uint32 CopySize = static_cast<uint32>(
					FMath::Min<uint64>(Length, 64u - BufferedBytes));
				FMemory::Memcpy(Buffer + BufferedBytes, Data, CopySize);
				BufferedBytes += CopySize;
				Data += CopySize;
				Length -= CopySize;
				if (BufferedBytes == 64)
				{
					Transform(Buffer);
					BufferedBytes = 0;
				}
			}
		}

		FString Final()
		{
			const uint64 TotalBits = TotalBytes * 8u;
			const uint8 Marker = 0x80;
			Update(&Marker, 1);
			const uint8 Zero = 0;
			while (BufferedBytes != 56)
			{
				Update(&Zero, 1);
			}
			uint8 LengthBytes[8];
			for (int32 Index = 0; Index < 8; ++Index)
			{
				LengthBytes[7 - Index] = static_cast<uint8>(TotalBits >> (Index * 8));
			}
			Update(LengthBytes, 8);

			uint8 Digest[32];
			for (uint32 Index = 0; Index < 8; ++Index)
			{
				Digest[Index * 4] = static_cast<uint8>(State[Index] >> 24);
				Digest[Index * 4 + 1] = static_cast<uint8>(State[Index] >> 16);
				Digest[Index * 4 + 2] = static_cast<uint8>(State[Index] >> 8);
				Digest[Index * 4 + 3] = static_cast<uint8>(State[Index]);
			}
			return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
		}
	};

	const TCHAR* KindToString(const EAdminImportAssetKind Kind)
	{
		switch (Kind)
		{
		case EAdminImportAssetKind::RiggedVehicle:
			return TEXT("rigged-vehicle");
		case EAdminImportAssetKind::Model:
			return TEXT("model");
		case EAdminImportAssetKind::Animation:
			return TEXT("animation");
		default:
			return TEXT("unknown");
		}
	}

	bool IsLowerHexSha256(const FString& Value)
	{
		if (Value.Len() != 64)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (!FChar::IsDigit(Character) && (Character < TEXT('a') || Character > TEXT('f')))
			{
				return false;
			}
		}
		return true;
	}

	bool IsStableId(const FString& Value)
	{
		if (Value.IsEmpty() || Value.StartsWith(TEXT("-")) || Value.EndsWith(TEXT("-")))
		{
			return false;
		}
		bool bPreviousWasDash = false;
		for (const TCHAR Character : Value)
		{
			const bool bIsDash = Character == TEXT('-');
			if ((!FChar::IsDigit(Character) && (Character < TEXT('a') || Character > TEXT('z')) && !bIsDash)
				|| (bIsDash && bPreviousWasDash))
			{
				return false;
			}
			bPreviousWasDash = bIsDash;
		}
		return true;
	}

	void AddError(FAdminImportItemResult& Item, const FString& Message)
	{
		Item.Errors.Add(Message);
	}

	bool ReadRequiredString(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		FString& OutValue,
		FAdminImportItemResult& Item,
		const FString& Context)
	{
		if (!Object.IsValid() || !Object->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
		{
			AddError(Item, FString::Printf(TEXT("%s.%s 必须是非空字符串。"), *Context, Field));
			return false;
		}
		return true;
	}

	bool RequireStringValue(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		const TCHAR* Expected,
		FAdminImportItemResult& Item,
		const FString& Context)
	{
		FString Actual;
		if (!ReadRequiredString(Object, Field, Actual, Item, Context))
		{
			return false;
		}
		if (Actual != Expected)
		{
			AddError(Item, FString::Printf(
				TEXT("%s.%s 必须为 \"%s\"，实际为 \"%s\"。"),
				*Context, Field, Expected, *Actual));
			return false;
		}
		return true;
	}

	bool ReadRequiredObject(
		const TSharedPtr<FJsonObject>& Parent,
		const TCHAR* Field,
		TSharedPtr<FJsonObject>& OutObject,
		FAdminImportItemResult& Item,
		const FString& Context = TEXT("$"))
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		if (!Parent.IsValid() || !Parent->TryGetObjectField(Field, Value) || Value == nullptr || !Value->IsValid())
		{
			AddError(Item, FString::Printf(TEXT("%s.%s 必须是对象。"), *Context, Field));
			return false;
		}
		OutObject = *Value;
		return true;
	}

	bool RequireBoolValue(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		const bool Expected,
		FAdminImportItemResult& Item,
		const FString& Context)
	{
		bool Actual = false;
		if (!Object.IsValid() || !Object->TryGetBoolField(Field, Actual))
		{
			AddError(Item, FString::Printf(TEXT("%s.%s 必须是布尔值。"), *Context, Field));
			return false;
		}
		if (Actual != Expected)
		{
			AddError(Item, FString::Printf(
				TEXT("%s.%s 必须为 %s。"), *Context, Field, Expected ? TEXT("true") : TEXT("false")));
			return false;
		}
		return true;
	}

	bool RequireNonEmptyArray(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		const TArray<TSharedPtr<FJsonValue>>*& OutArray,
		FAdminImportItemResult& Item,
		const FString& Context = TEXT("$"))
	{
		if (!Object.IsValid() || !Object->TryGetArrayField(Field, OutArray) || OutArray == nullptr || OutArray->IsEmpty())
		{
			AddError(Item, FString::Printf(TEXT("%s.%s 必须是非空数组。"), *Context, Field));
			return false;
		}
		return true;
	}

	bool ReadRequiredInteger(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		int32& OutValue,
		FAdminImportItemResult& Item,
		const FString& Context)
	{
		double Value = 0.0;
		if (!Object.IsValid() || !Object->TryGetNumberField(Field, Value)
			|| Value < static_cast<double>(MIN_int32)
			|| Value > static_cast<double>(MAX_int32)
			|| FMath::FloorToDouble(Value) != Value)
		{
			AddError(Item, FString::Printf(TEXT("%s.%s 必须是整数。"), *Context, Field));
			return false;
		}
		OutValue = static_cast<int32>(Value);
		return true;
	}

	void ValidateCoordinateSystem(
		const TSharedPtr<FJsonObject>& Root,
		FAdminImportItemResult& Item)
	{
		TSharedPtr<FJsonObject> Coordinate;
		if (!ReadRequiredObject(Root, TEXT("coordinateSystem"), Coordinate, Item))
		{
			return;
		}
		RequireStringValue(Coordinate, TEXT("unit"), TEXT("centimeter"), Item, TEXT("$.coordinateSystem"));
		RequireStringValue(Coordinate, TEXT("forward"), TEXT("+X"), Item, TEXT("$.coordinateSystem"));
		RequireStringValue(Coordinate, TEXT("right"), TEXT("+Y"), Item, TEXT("$.coordinateSystem"));
		RequireStringValue(Coordinate, TEXT("up"), TEXT("+Z"), Item, TEXT("$.coordinateSystem"));
		RequireStringValue(Coordinate, TEXT("handedness"), TEXT("left"), Item, TEXT("$.coordinateSystem"));
	}

	void ValidateAuthorization(
		const TSharedPtr<FJsonObject>& Root,
		FAdminImportItemResult& Item)
	{
		TSharedPtr<FJsonObject> Authorization;
		if (!ReadRequiredObject(Root, TEXT("authorization"), Authorization, Item))
		{
			return;
		}
		FString Ignored;
		ReadRequiredString(Authorization, TEXT("rightsHolder"), Ignored, Item, TEXT("$.authorization"));
		ReadRequiredString(Authorization, TEXT("licenseId"), Ignored, Item, TEXT("$.authorization"));
		ReadRequiredString(Authorization, TEXT("territory"), Ignored, Item, TEXT("$.authorization"));
		ReadRequiredString(Authorization, TEXT("redistribution"), Ignored, Item, TEXT("$.authorization"));

		const TArray<TSharedPtr<FJsonValue>>* Uses = nullptr;
		if (RequireNonEmptyArray(
			Authorization, TEXT("permittedUses"), Uses, Item, TEXT("$.authorization")))
		{
			bool bAllowsUnrealImport = false;
			for (const TSharedPtr<FJsonValue>& Use : *Uses)
			{
				bAllowsUnrealImport |= Use.IsValid() && Use->AsString() == TEXT("unreal-import");
			}
			if (!bAllowsUnrealImport)
			{
				AddError(Item, TEXT("$.authorization.permittedUses 必须包含 \"unreal-import\"。"));
			}
		}
	}

	void ValidateExport(
		const TSharedPtr<FJsonObject>& Root,
		const EAdminImportAssetKind Kind,
		FAdminImportItemResult& Item)
	{
		TSharedPtr<FJsonObject> Export;
		if (!ReadRequiredObject(Root, TEXT("export"), Export, Item))
		{
			return;
		}
		RequireStringValue(Export, TEXT("format"), TEXT("FBX"), Item, TEXT("$.export"));
		RequireStringValue(Export, TEXT("fbxVersion"), TEXT("2020.2"), Item, TEXT("$.export"));
		RequireBoolValue(Export, TEXT("binary"), true, Item, TEXT("$.export"));
		if (Kind == EAdminImportAssetKind::Model)
		{
			RequireBoolValue(Export, TEXT("bakeTransforms"), true, Item, TEXT("$.export"));
		}
		else
		{
			RequireBoolValue(Export, TEXT("bakeAnimation"), true, Item, TEXT("$.export"));
			RequireBoolValue(Export, TEXT("resampleAll"), true, Item, TEXT("$.export"));
		}
		if (Kind == EAdminImportAssetKind::RiggedVehicle)
		{
			RequireBoolValue(Export, TEXT("skeletalMesh"), true, Item, TEXT("$.export"));
			RequireBoolValue(Export, TEXT("importAnimations"), true, Item, TEXT("$.export"));
			RequireStringValue(
				Export, TEXT("animationLength"), TEXT("exported-time"), Item, TEXT("$.export"));
		}
	}

	bool ReadArtifact(
		const TSharedPtr<FJsonObject>& Artifact,
		const FString& Context,
		FString& OutPath,
		FString& OutSha256,
		int64& OutBytes,
		FAdminImportItemResult& Item)
	{
		bool bValid = ReadRequiredString(Artifact, TEXT("path"), OutPath, Item, Context);
		bValid &= ReadRequiredString(Artifact, TEXT("sha256"), OutSha256, Item, Context);
		if (!OutSha256.IsEmpty() && !IsLowerHexSha256(OutSha256))
		{
			AddError(Item, FString::Printf(TEXT("%s.sha256 必须是 64 位小写十六进制。"), *Context));
			bValid = false;
		}

		double Bytes = 0.0;
		if (!Artifact.IsValid() || !Artifact->TryGetNumberField(TEXT("bytes"), Bytes)
			|| Bytes < 1.0 || Bytes > static_cast<double>(MAX_int64)
			|| FMath::FloorToDouble(Bytes) != Bytes)
		{
			AddError(Item, FString::Printf(TEXT("%s.bytes 必须是正整数。"), *Context));
			bValid = false;
		}
		else
		{
			OutBytes = static_cast<int64>(Bytes);
		}

		if (!OutPath.EndsWith(TEXT(".fbx"), ESearchCase::IgnoreCase))
		{
			AddError(Item, FString::Printf(TEXT("%s.path 必须指向 FBX。"), *Context));
			bValid = false;
		}
		return bValid;
	}

	bool SelectModelArtifact(
		const TSharedPtr<FJsonObject>& Root,
		FAdminImportItemResult& Item)
	{
		TSharedPtr<FJsonObject> Artifacts;
		TSharedPtr<FJsonObject> Fbx;
		if (!ReadRequiredObject(Root, TEXT("artifacts"), Artifacts, Item)
			|| !ReadRequiredObject(Artifacts, TEXT("fbx"), Fbx, Item, TEXT("$.artifacts")))
		{
			return false;
		}
		return ReadArtifact(
			Fbx, TEXT("$.artifacts.fbx"), Item.DeclaredArtifactPath,
			Item.ExpectedSha256, Item.ExpectedBytes, Item);
	}

	bool SelectAnimationArtifact(
		const TSharedPtr<FJsonObject>& Root,
		FAdminImportItemResult& Item)
	{
		const TArray<TSharedPtr<FJsonValue>>* Artifacts = nullptr;
		if (!RequireNonEmptyArray(Root, TEXT("artifacts"), Artifacts, Item))
		{
			return false;
		}

		const FString SelectedName = FPaths::GetCleanFilename(Item.FbxFile);
		TSharedPtr<FJsonObject> Match;
		int32 MatchIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Artifacts->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Candidate = (*Artifacts)[Index].IsValid()
				? (*Artifacts)[Index]->AsObject()
				: nullptr;
			FString CandidatePath;
			if (Candidate.IsValid() && Candidate->TryGetStringField(TEXT("path"), CandidatePath)
				&& FPaths::GetCleanFilename(CandidatePath).Equals(SelectedName, ESearchCase::IgnoreCase))
			{
				Match = Candidate;
				MatchIndex = Index;
				break;
			}
		}
		if (!Match.IsValid() && Artifacts->Num() == 1)
		{
			Match = (*Artifacts)[0].IsValid() ? (*Artifacts)[0]->AsObject() : nullptr;
			MatchIndex = 0;
		}
		if (!Match.IsValid())
		{
			AddError(Item, TEXT("$.artifacts 中没有与所选动画 FBX 文件名匹配的条目。"));
			return false;
		}

		FString ClipId;
		ReadRequiredString(
			Match, TEXT("clipId"), ClipId, Item,
			FString::Printf(TEXT("$.artifacts[%d]"), MatchIndex));
		return ReadArtifact(
			Match,
			FString::Printf(TEXT("$.artifacts[%d]"), MatchIndex),
			Item.DeclaredArtifactPath,
			Item.ExpectedSha256,
			Item.ExpectedBytes,
			Item);
	}

	void ValidateModel(
		const TSharedPtr<FJsonObject>& Root,
		FAdminImportItemResult& Item)
	{
		FString Id;
		if (ReadRequiredString(Root, TEXT("vehicleId"), Id, Item, TEXT("$")) && !IsStableId(Id))
		{
			AddError(Item, TEXT("$.vehicleId 不是合法稳定 ID。"));
		}
		if (ReadRequiredString(Root, TEXT("modelVersion"), Id, Item, TEXT("$")) && !IsStableId(Id))
		{
			AddError(Item, TEXT("$.modelVersion 不是合法稳定 ID。"));
		}

		TSharedPtr<FJsonObject> Source;
		if (ReadRequiredObject(Root, TEXT("source"), Source, Item))
		{
			ReadRequiredString(Source, TEXT("dcc"), Id, Item, TEXT("$.source"));
			ReadRequiredString(Source, TEXT("dccVersion"), Id, Item, TEXT("$.source"));
			TSharedPtr<FJsonObject> SourceFile;
			if (ReadRequiredObject(Source, TEXT("file"), SourceFile, Item, TEXT("$.source")))
			{
				FString SourcePath;
				FString SourceHash;
				ReadRequiredString(SourceFile, TEXT("path"), SourcePath, Item, TEXT("$.source.file"));
				if (ReadRequiredString(
					SourceFile, TEXT("sha256"), SourceHash, Item, TEXT("$.source.file"))
					&& !IsLowerHexSha256(SourceHash))
				{
					AddError(Item, TEXT("$.source.file.sha256 必须是 64 位小写十六进制。"));
				}
				double SourceBytes = 0.0;
				if (!SourceFile->TryGetNumberField(TEXT("bytes"), SourceBytes)
					|| SourceBytes < 1.0 || FMath::FloorToDouble(SourceBytes) != SourceBytes)
				{
					AddError(Item, TEXT("$.source.file.bytes 必须是正整数。"));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Ignored = nullptr;
		RequireNonEmptyArray(Root, TEXT("nodes"), Ignored, Item);
		RequireNonEmptyArray(Root, TEXT("materialSlots"), Ignored, Item);
		RequireNonEmptyArray(Root, TEXT("partBindings"), Ignored, Item);
		RequireNonEmptyArray(Root, TEXT("lods"), Ignored, Item);
		SelectModelArtifact(Root, Item);
	}

	void ValidateAnimation(
		const TSharedPtr<FJsonObject>& Root,
		FAdminImportItemResult& Item)
	{
		FString Id;
		if (ReadRequiredString(Root, TEXT("animationVersion"), Id, Item, TEXT("$")) && !IsStableId(Id))
		{
			AddError(Item, TEXT("$.animationVersion 不是合法稳定 ID。"));
		}

		TSharedPtr<FJsonObject> ModelRef;
		if (ReadRequiredObject(Root, TEXT("modelRef"), ModelRef, Item))
		{
			if (ReadRequiredString(ModelRef, TEXT("vehicleId"), Id, Item, TEXT("$.modelRef")) && !IsStableId(Id))
			{
				AddError(Item, TEXT("$.modelRef.vehicleId 不是合法稳定 ID。"));
			}
			if (ReadRequiredString(ModelRef, TEXT("modelVersion"), Id, Item, TEXT("$.modelRef")) && !IsStableId(Id))
			{
				AddError(Item, TEXT("$.modelRef.modelVersion 不是合法稳定 ID。"));
			}
			if (ReadRequiredString(ModelRef, TEXT("fbxSha256"), Id, Item, TEXT("$.modelRef"))
				&& !IsLowerHexSha256(Id))
			{
				AddError(Item, TEXT("$.modelRef.fbxSha256 必须是 64 位小写十六进制。"));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Ignored = nullptr;
		RequireNonEmptyArray(Root, TEXT("clips"), Ignored, Item);
		SelectAnimationArtifact(Root, Item);
	}

	void ValidateRiggedVehicle(
		const TSharedPtr<FJsonObject>& Root,
		FAdminImportItemResult& Item)
	{
		FString Id;
		for (const TCHAR* Field : {TEXT("vehicleId"), TEXT("modelVersion"), TEXT("animationVersion")})
		{
			if (ReadRequiredString(Root, Field, Id, Item, TEXT("$")) && !IsStableId(Id))
			{
				AddError(Item, FString::Printf(TEXT("$.%s 不是合法稳定 ID。"), Field));
			}
		}

		TSharedPtr<FJsonObject> Source;
		if (ReadRequiredObject(Root, TEXT("source"), Source, Item))
		{
			ReadRequiredString(Source, TEXT("dcc"), Id, Item, TEXT("$.source"));
			ReadRequiredString(Source, TEXT("dccVersion"), Id, Item, TEXT("$.source"));
			TSharedPtr<FJsonObject> SourceFile;
			if (ReadRequiredObject(Source, TEXT("file"), SourceFile, Item, TEXT("$.source")))
			{
				FString IgnoredPath;
				FString SourceHash;
				ReadRequiredString(SourceFile, TEXT("path"), IgnoredPath, Item, TEXT("$.source.file"));
				if (ReadRequiredString(
					SourceFile, TEXT("sha256"), SourceHash, Item, TEXT("$.source.file"))
					&& !IsLowerHexSha256(SourceHash))
				{
					AddError(Item, TEXT("$.source.file.sha256 必须是 64 位小写十六进制。"));
				}
				double SourceBytes = 0.0;
				if (!SourceFile->TryGetNumberField(TEXT("bytes"), SourceBytes)
					|| SourceBytes < 1.0 || FMath::FloorToDouble(SourceBytes) != SourceBytes)
				{
					AddError(Item, TEXT("$.source.file.bytes 必须是正整数。"));
				}
			}
		}

		TSet<FString> Bones;
		FString RootBone;
		TSharedPtr<FJsonObject> Skeleton;
		if (ReadRequiredObject(Root, TEXT("skeleton"), Skeleton, Item))
		{
			ReadRequiredString(Skeleton, TEXT("rootBone"), RootBone, Item, TEXT("$.skeleton"));
			const TArray<TSharedPtr<FJsonValue>>* BoneValues = nullptr;
			if (RequireNonEmptyArray(
				Skeleton, TEXT("bones"), BoneValues, Item, TEXT("$.skeleton")))
			{
				for (int32 Index = 0; Index < BoneValues->Num(); ++Index)
				{
					FString Bone;
					if (!(*BoneValues)[Index].IsValid()
						|| !(*BoneValues)[Index]->TryGetString(Bone)
						|| Bone.IsEmpty())
					{
						AddError(Item, FString::Printf(
							TEXT("$.skeleton.bones[%d] 必须是非空字符串。"), Index));
						continue;
					}
					if (Bones.Contains(Bone))
					{
						AddError(Item, FString::Printf(
							TEXT("$.skeleton.bones[%d] 骨骼名重复。"), Index));
					}
					Bones.Add(Bone);
				}
				if (!RootBone.IsEmpty() && !Bones.Contains(RootBone))
				{
					AddError(Item, TEXT("$.skeleton.rootBone 必须存在于 skeleton.bones。"));
				}
			}
		}

		TSharedPtr<FJsonObject> Sequence;
		if (ReadRequiredObject(Root, TEXT("sequence"), Sequence, Item))
		{
			if (ReadRequiredString(
				Sequence, TEXT("sequenceId"), Item.SequenceId, Item, TEXT("$.sequence"))
				&& !IsStableId(Item.SequenceId))
			{
				AddError(Item, TEXT("$.sequence.sequenceId 不是合法稳定 ID。"));
			}
			ReadRequiredInteger(
				Sequence, TEXT("startFrame"), Item.SequenceStartFrame, Item, TEXT("$.sequence"));
			ReadRequiredInteger(
				Sequence, TEXT("endFrame"), Item.SequenceEndFrame, Item, TEXT("$.sequence"));
			if (Item.SequenceEndFrame <= Item.SequenceStartFrame)
			{
				AddError(Item, TEXT("$.sequence.endFrame 必须大于 startFrame。"));
			}
			if (Item.SequenceStartFrame < 0)
			{
				AddError(Item, TEXT("$.sequence.startFrame 不得小于 0。"));
			}
			double FrameRate = 0.0;
			if (!Sequence->TryGetNumberField(TEXT("frameRate"), FrameRate) || FrameRate < 1.0)
			{
				AddError(Item, TEXT("$.sequence.frameRate 必须大于等于 1。"));
			}
			else
			{
				Item.SequenceFrameRate = FrameRate;
			}
			RequireBoolValue(Sequence, TEXT("loop"), false, Item, TEXT("$.sequence"));
			RequireBoolValue(Sequence, TEXT("rootMotion"), false, Item, TEXT("$.sequence"));
		}

		const TArray<TSharedPtr<FJsonValue>>* Clips = nullptr;
		if (RequireNonEmptyArray(Root, TEXT("clips"), Clips, Item))
		{
			TSet<FString> ClipIds;
			for (int32 Index = 0; Index < Clips->Num(); ++Index)
			{
				const FString Context = FString::Printf(TEXT("$.clips[%d]"), Index);
				const TSharedPtr<FJsonObject> Clip =
					(*Clips)[Index].IsValid() ? (*Clips)[Index]->AsObject() : nullptr;
				if (!Clip.IsValid())
				{
					AddError(Item, Context + TEXT(" 必须是对象。"));
					continue;
				}
				FString ClipId;
				FString TargetBone;
				if (ReadRequiredString(Clip, TEXT("clipId"), ClipId, Item, Context))
				{
					if (!IsStableId(ClipId))
					{
						AddError(Item, Context + TEXT(".clipId 不是合法稳定 ID。"));
					}
					if (ClipIds.Contains(ClipId))
					{
						AddError(Item, Context + TEXT(".clipId 必须唯一。"));
					}
					ClipIds.Add(ClipId);
				}
				if (ReadRequiredString(Clip, TEXT("targetBone"), TargetBone, Item, Context)
					&& !Bones.Contains(TargetBone))
				{
					AddError(Item, Context + TEXT(".targetBone 必须引用 skeleton.bones。"));
				}
				FString PrimaryBone;
				if (Clip->TryGetStringField(TEXT("primaryBone"), PrimaryBone))
				{
					if (PrimaryBone.IsEmpty() || !Bones.Contains(PrimaryBone))
					{
						AddError(Item, Context + TEXT(".primaryBone 必须引用 skeleton.bones。"));
					}
					else if (ClipId == TEXT("wheel-spin"))
					{
						Item.PrimaryBone = PrimaryBone;
					}
				}
				if (ClipId == TEXT("wheel-spin"))
				{
					for (const TCHAR* WheelBone : {
						TEXT("Wheel_FL"), TEXT("Wheel_FR"), TEXT("Wheel_RL"), TEXT("Wheel_RR")})
					{
						if (!Bones.Contains(WheelBone))
						{
							AddError(Item, Context + FString::Printf(
								TEXT(" 缺少四轮曲线骨骼 %s。"), WheelBone));
						}
						else
						{
							Item.WheelCurveBones.AddUnique(WheelBone);
						}
					}
				}
				FString Action;
				if (ReadRequiredString(Clip, TEXT("action"), Action, Item, Context)
					&& Action != TEXT("open") && Action != TEXT("close")
					&& Action != TEXT("rotate") && Action != TEXT("translate")
					&& Action != TEXT("custom"))
				{
					AddError(Item, Context + TEXT(".action 不是支持的动作。"));
				}
				int32 StartFrame = 0;
				int32 EndFrame = 0;
				const bool bHasStart =
					ReadRequiredInteger(Clip, TEXT("startFrame"), StartFrame, Item, Context);
				const bool bHasEnd =
					ReadRequiredInteger(Clip, TEXT("endFrame"), EndFrame, Item, Context);
				if (bHasStart && bHasEnd)
				{
					if (EndFrame <= StartFrame)
					{
						AddError(Item, Context + TEXT(".endFrame 必须大于 startFrame。"));
					}
					if (StartFrame < Item.SequenceStartFrame || EndFrame > Item.SequenceEndFrame)
					{
						AddError(Item, Context + TEXT(" 帧范围必须位于完整 sequence 内。"));
					}
				}
				bool bReversible = false;
				if (!Clip->TryGetBoolField(TEXT("reversible"), bReversible))
				{
					AddError(Item, Context + TEXT(".reversible 必须是布尔值。"));
				}
				bool bLoop = false;
				if (!Clip->TryGetBoolField(TEXT("loop"), bLoop))
				{
					AddError(Item, Context + TEXT(".loop 必须是布尔值。"));
				}
			}
		}
		SelectModelArtifact(Root, Item);
	}

	void ValidateSelection(const FAdminImportSelection& Selection, FAdminImportItemResult& Item)
	{
		Item.Kind = Selection.Kind;
		Item.FbxFile = FPaths::ConvertRelativePathToFull(Selection.FbxFile);
		Item.SidecarFile = FPaths::ConvertRelativePathToFull(Selection.SidecarFile);

		if (Selection.FbxFile.IsEmpty())
		{
			AddError(Item, TEXT("未选择 FBX 文件。"));
		}
		else if (!FPaths::FileExists(Item.FbxFile))
		{
			AddError(Item, FString::Printf(TEXT("FBX 文件不存在：%s"), *Item.FbxFile));
		}
		else if (!Item.FbxFile.EndsWith(TEXT(".fbx"), ESearchCase::IgnoreCase))
		{
			AddError(Item, TEXT("所选模型/动画文件扩展名必须是 .fbx。"));
		}

		if (Selection.SidecarFile.IsEmpty())
		{
			AddError(Item, TEXT("未选择 sidecar JSON。"));
			return;
		}
		if (!FPaths::FileExists(Item.SidecarFile))
		{
			AddError(Item, FString::Printf(TEXT("sidecar 文件不存在：%s"), *Item.SidecarFile));
			return;
		}
		if (!Item.SidecarFile.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
		{
			AddError(Item, TEXT("sidecar 文件扩展名必须是 .json。"));
		}

		const int64 SidecarSize = IFileManager::Get().FileSize(*Item.SidecarFile);
		if (SidecarSize <= 0)
		{
			AddError(Item, TEXT("sidecar 文件必须非空。"));
			return;
		}

		FString JsonText;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(JsonText, *Item.SidecarFile))
		{
			AddError(Item, TEXT("无法读取 sidecar 文件。"));
			return;
		}
		const TSharedRef<TJsonReader<>> LoadedReader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(LoadedReader, Root) || !Root.IsValid())
		{
			AddError(Item, FString::Printf(TEXT("sidecar JSON 无效：%s"), *LoadedReader->GetErrorMessage()));
			return;
		}

		RequireStringValue(
			Root,
			TEXT("schemaVersion"),
			Selection.Kind == EAdminImportAssetKind::RiggedVehicle
				? TEXT("2.0.0")
				: TEXT("1.0.0"),
			Item,
			TEXT("$"));
		const TCHAR* ExpectedKind = TEXT("vehicle-animation");
		if (Selection.Kind == EAdminImportAssetKind::RiggedVehicle)
		{
			ExpectedKind = TEXT("rigged-vehicle");
		}
		else if (Selection.Kind == EAdminImportAssetKind::Model)
		{
			ExpectedKind = TEXT("vehicle-model");
		}
		RequireStringValue(
			Root,
			TEXT("kind"),
			ExpectedKind,
			Item,
			TEXT("$"));
		ValidateCoordinateSystem(Root, Item);
		ValidateExport(Root, Selection.Kind, Item);
		ValidateAuthorization(Root, Item);
		if (Selection.Kind == EAdminImportAssetKind::RiggedVehicle)
		{
			ValidateRiggedVehicle(Root, Item);
		}
		else if (Selection.Kind == EAdminImportAssetKind::Model)
		{
			ValidateModel(Root, Item);
		}
		else
		{
			ValidateAnimation(Root, Item);
		}

		if (!Item.DeclaredArtifactPath.IsEmpty()
			&& !FPaths::GetCleanFilename(Item.DeclaredArtifactPath).Equals(
				FPaths::GetCleanFilename(Item.FbxFile), ESearchCase::IgnoreCase))
		{
			AddError(Item, TEXT("所选 FBX 文件名与 sidecar artifact.path 不匹配。"));
		}

		if (FPaths::FileExists(Item.FbxFile))
		{
			Item.ActualBytes = IFileManager::Get().FileSize(*Item.FbxFile);
			if (Item.ActualBytes <= 0)
			{
				AddError(Item, TEXT("FBX 文件必须非空。"));
			}
			else if (Item.ExpectedBytes > 0 && Item.ActualBytes != Item.ExpectedBytes)
			{
				AddError(Item, FString::Printf(
					TEXT("FBX 大小不匹配：sidecar=%lld，实际=%lld。"),
					Item.ExpectedBytes, Item.ActualBytes));
			}

			FString HashError;
			if (!FAdminImportPreflight::ComputeFileSha256(
				Item.FbxFile, Item.ActualSha256, HashError))
			{
				AddError(Item, HashError);
			}
			else if (!Item.ExpectedSha256.IsEmpty()
				&& Item.ActualSha256 != Item.ExpectedSha256)
			{
				AddError(Item, FString::Printf(
					TEXT("FBX SHA-256 不匹配：sidecar=%s，实际=%s。"),
					*Item.ExpectedSha256, *Item.ActualSha256));
			}
		}

		Item.bPassed = Item.Errors.IsEmpty();
	}

	TSharedRef<FJsonObject> ItemToJson(const FAdminImportItemResult& Item)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("kind"), KindToString(Item.Kind));
		Json->SetStringField(TEXT("fbxFile"), Item.FbxFile);
		Json->SetStringField(TEXT("sidecarFile"), Item.SidecarFile);
		Json->SetStringField(TEXT("declaredArtifactPath"), Item.DeclaredArtifactPath);
		Json->SetStringField(TEXT("expectedSha256"), Item.ExpectedSha256);
		Json->SetStringField(TEXT("actualSha256"), Item.ActualSha256);
		Json->SetNumberField(TEXT("expectedBytes"), static_cast<double>(Item.ExpectedBytes));
		Json->SetNumberField(TEXT("actualBytes"), static_cast<double>(Item.ActualBytes));
		if (Item.Kind == EAdminImportAssetKind::RiggedVehicle)
		{
			Json->SetStringField(TEXT("sequenceId"), Item.SequenceId);
			Json->SetNumberField(TEXT("sequenceFrameRate"), Item.SequenceFrameRate);
			Json->SetNumberField(TEXT("sequenceStartFrame"), Item.SequenceStartFrame);
			Json->SetNumberField(TEXT("sequenceEndFrame"), Item.SequenceEndFrame);
			Json->SetStringField(TEXT("primaryBone"), Item.PrimaryBone);
			TArray<TSharedPtr<FJsonValue>> WheelCurveValues;
			for (const FString& Bone : Item.WheelCurveBones)
			{
				WheelCurveValues.Add(MakeShared<FJsonValueString>(Bone));
			}
			Json->SetArrayField(TEXT("wheelCurveBones"), WheelCurveValues);
		}
		Json->SetBoolField(TEXT("passed"), Item.bPassed);

		TArray<TSharedPtr<FJsonValue>> Errors;
		for (const FString& Error : Item.Errors)
		{
			Errors.Add(MakeShared<FJsonValueString>(Error));
		}
		Json->SetArrayField(TEXT("errors"), Errors);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Warning : Item.Warnings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		Json->SetArrayField(TEXT("warnings"), Warnings);
		return Json;
	}
}

FAdminImportPreflightResult FAdminImportPreflight::Run(
	const TArray<FAdminImportSelection>& Selections,
	const FString& SessionId)
{
	FAdminImportPreflightResult Result;
	bool bValidSessionId = !SessionId.IsEmpty();
	for (const TCHAR Character : SessionId)
	{
		bValidSessionId &= FChar::IsAlnum(Character) || Character == TEXT('-');
	}
	Result.SessionId = bValidSessionId
		? SessionId
		: FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Result.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Result.StagingPath = FString::Printf(TEXT("%s/%s"), AdminImport::StagingRoot, *Result.SessionId);

	if (Selections.IsEmpty())
	{
		FAdminImportItemResult& Item = Result.Items.AddDefaulted_GetRef();
		Item.Errors.Add(TEXT("至少选择一组模型或动画 FBX 与 sidecar。"));
	}
	else
	{
		for (const FAdminImportSelection& Selection : Selections)
		{
			FAdminImportItemResult& Item = Result.Items.AddDefaulted_GetRef();
			AdminImport::ValidateSelection(Selection, Item);
		}
	}

	TSet<FString> ImportNames;
	for (FAdminImportItemResult& Item : Result.Items)
	{
		const FString ImportName = FPaths::GetBaseFilename(Item.FbxFile).ToLower();
		if (!ImportName.IsEmpty() && ImportNames.Contains(ImportName))
		{
			Item.Errors.Add(TEXT("同一会话中的 FBX 基础文件名重复，会在暂存目录产生资产名冲突。"));
			Item.bPassed = false;
		}
		ImportNames.Add(ImportName);
	}

	Result.bPassed = !Result.Items.IsEmpty();
	for (const FAdminImportItemResult& Item : Result.Items)
	{
		Result.bPassed &= Item.bPassed;
	}
	WriteJsonReport(Result);
	return Result;
}

bool FAdminImportPreflight::ComputeFileSha256(
	const FString& Filename,
	FString& OutSha256,
	FString& OutError)
{
	OutSha256.Reset();
	OutError.Reset();
	const int64 Size = IFileManager::Get().FileSize(*Filename);
	if (Size <= 0)
	{
		OutError = FString::Printf(TEXT("无法哈希不存在或为空的文件：%s"), *Filename);
		return false;
	}

	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Filename));
	if (!Reader.IsValid())
	{
		OutError = FString::Printf(TEXT("无法打开 FBX 以计算 SHA-256：%s"), *Filename);
		return false;
	}

	AdminImport::FSha256 Hash;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(1024 * 1024);
	while (!Reader->AtEnd())
	{
		const int64 Remaining = Reader->TotalSize() - Reader->Tell();
		const int64 ReadSize = FMath::Min<int64>(Remaining, Buffer.Num());
		Reader->Serialize(Buffer.GetData(), ReadSize);
		if (Reader->IsError())
		{
			OutError = FString::Printf(TEXT("读取 FBX 时发生错误：%s"), *Filename);
			return false;
		}
		Hash.Update(Buffer.GetData(), static_cast<uint64>(ReadSize));
	}
	OutSha256 = Hash.Final();
	return true;
}

bool FAdminImportPreflight::WriteJsonReport(FAdminImportPreflightResult& Result)
{
	if (Result.ReportPath.IsEmpty())
	{
		Result.ReportPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("ConfigurationSystem"),
			TEXT("AdminImportReports"),
			Result.SessionId + TEXT(".json")));
	}

	TArray<TSharedPtr<FJsonValue>> ItemValues;
	for (const FAdminImportItemResult& Item : Result.Items)
	{
		ItemValues.Add(MakeShared<FJsonValueObject>(AdminImport::ItemToJson(Item)));
	}
	TArray<TSharedPtr<FJsonValue>> ImportedValues;
	for (const FString& ObjectPath : Result.ImportedObjectPaths)
	{
		ImportedValues.Add(MakeShared<FJsonValueString>(ObjectPath));
	}
	TArray<TSharedPtr<FJsonValue>> ImportedAssetValues;
	for (const FAdminImportedAssetResult& Asset : Result.ImportedAssets)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("objectPath"), Asset.ObjectPath);
		Json->SetStringField(TEXT("assetType"), Asset.AssetType);
		Json->SetStringField(TEXT("skeletonPath"), Asset.SkeletonPath);
		Json->SetNumberField(TEXT("animSequenceFrames"), Asset.AnimSequenceFrames);
		ImportedAssetValues.Add(MakeShared<FJsonValueObject>(Json));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("tool"), TEXT("ConfigurationSystemAdminImport"));
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("sessionId"), Result.SessionId);
	Root->SetStringField(TEXT("timestampUtc"), Result.TimestampUtc);
	Root->SetStringField(TEXT("stagingPath"), Result.StagingPath);
	Root->SetBoolField(TEXT("preflightPassed"), Result.bPassed);
	Root->SetBoolField(TEXT("importAttempted"), Result.bImportAttempted);
	Root->SetBoolField(TEXT("importSucceeded"), Result.bImportSucceeded);
	Root->SetArrayField(TEXT("items"), ItemValues);
	Root->SetArrayField(TEXT("importedObjectPaths"), ImportedValues);
	Root->SetArrayField(TEXT("importedAssets"), ImportedAssetValues);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	const bool bSerialized = FJsonSerializer::Serialize(Root, Writer);
	const bool bDirectoryReady =
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Result.ReportPath), true);
	const bool bWritten = bSerialized && bDirectoryReady
		&& FFileHelper::SaveStringToFile(
			JsonText,
			*Result.ReportPath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bWritten)
	{
		UE_LOG(LogAdminImport, Error, TEXT("无法写入管理员导入报告：%s"), *Result.ReportPath);
	}
	return bWritten;
}

void FAdminImportService::ConfigureImportTask(
	UAssetImportTask& Task,
	const EAdminImportAssetKind Kind)
{
	if (Kind != EAdminImportAssetKind::RiggedVehicle)
	{
		return;
	}

	UFbxImportUI* ImportUI = NewObject<UFbxImportUI>(&Task);
	ImportUI->bAutomatedImportShouldDetectType = false;
	ImportUI->MeshTypeToImport = FBXIT_SkeletalMesh;
	ImportUI->OriginalImportType = FBXIT_SkeletalMesh;
	ImportUI->bImportMesh = true;
	ImportUI->bImportAnimations = true;
	ImportUI->bImportMaterials = false;
	ImportUI->bImportTextures = false;
	ImportUI->bCreatePhysicsAsset = false;
	if (ImportUI->AnimSequenceImportData != nullptr)
	{
		ImportUI->AnimSequenceImportData->AnimationLength = FBXALIT_ExportedTime;
	}
	Task.Options = ImportUI;
}

TArray<FName> FAdminImportService::FindNewPackageNames(
	const TArray<FName>& Before,
	const TArray<FName>& After)
{
	TSet<FName> Existing;
	for (const FName PackageName : Before)
	{
		Existing.Add(PackageName);
	}
	TArray<FName> NewPackages;
	for (const FName PackageName : After)
	{
		if (!Existing.Contains(PackageName))
		{
			NewPackages.AddUnique(PackageName);
		}
	}
	return NewPackages;
}

bool FAdminImportService::ImportApproved(FAdminImportPreflightResult& Result)
{
	TArray<FAdminImportSelection> Selections;
	for (const FAdminImportItemResult& Item : Result.Items)
	{
		FAdminImportSelection& Selection = Selections.AddDefaulted_GetRef();
		Selection.Kind = Item.Kind;
		Selection.FbxFile = Item.FbxFile;
		Selection.SidecarFile = Item.SidecarFile;
	}
	// Close the time-of-check/time-of-use gap for files edited after the UI pass.
	Result = FAdminImportPreflight::Run(Selections, Result.SessionId);
	Result.bImportAttempted = true;
	Result.bImportSucceeded = false;
	Result.ImportedObjectPaths.Reset();
	Result.ImportedAssets.Reset();

	if (!Result.bPassed
		|| !Result.StagingPath.StartsWith(
			FString(AdminImport::StagingRoot) + TEXT("/"),
			ESearchCase::CaseSensitive))
	{
		UE_LOG(LogAdminImport, Error, TEXT("拒绝导入：预检未通过或暂存路径越界。"));
		FAdminImportPreflight::WriteJsonReport(Result);
		return false;
	}

	TArray<UAssetImportTask*> Tasks;
	for (const FAdminImportItemResult& Item : Result.Items)
	{
		if (!Item.bPassed)
		{
			FAdminImportPreflight::WriteJsonReport(Result);
			return false;
		}
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = Item.FbxFile;
		Task->DestinationPath = Result.StagingPath;
		Task->bAutomated = true;
		Task->bAsync = false;
		Task->bReplaceExisting = false;
		Task->bReplaceExistingSettings = false;
		Task->bSave = true;
		ConfigureImportTask(*Task, Item.Kind);
		Tasks.Add(Task);
	}

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();
	const auto QueryStagingAssets =
		[&AssetRegistry, &Result](TArray<FAssetData>& OutAssets)
	{
		OutAssets.Reset();
		AssetRegistry.GetAssetsByPath(
			FName(*Result.StagingPath), OutAssets, true);
	};
	TArray<FAssetData> SessionNewAssets;
	Result.bImportSucceeded = !Tasks.IsEmpty();
	for (int32 Index = 0; Index < Tasks.Num(); ++Index)
	{
		UAssetImportTask* Task = Tasks[Index];
		FAdminImportItemResult& Item = Result.Items[Index];
		TArray<FAssetData> AssetsBeforeTask;
		QueryStagingAssets(AssetsBeforeTask);
		TArray<FName> PackagesBeforeTask;
		for (const FAssetData& Asset : AssetsBeforeTask)
		{
			PackagesBeforeTask.AddUnique(Asset.PackageName);
		}

		TArray<UAssetImportTask*> SingleTask = {Task};
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(SingleTask);
		AssetRegistry.ScanPathsSynchronous({Result.StagingPath}, true);
		TArray<FAssetData> AssetsAfterTask;
		QueryStagingAssets(AssetsAfterTask);
		TArray<FName> PackagesAfterTask;
		for (const FAssetData& Asset : AssetsAfterTask)
		{
			PackagesAfterTask.AddUnique(Asset.PackageName);
		}
		TSet<FName> NewPackageNames;
		for (const FName PackageName :
			FindNewPackageNames(PackagesBeforeTask, PackagesAfterTask))
		{
			NewPackageNames.Add(PackageName);
		}
		TArray<FAssetData> ItemNewAssets;
		for (const FAssetData& Asset : AssetsAfterTask)
		{
			if (NewPackageNames.Contains(Asset.PackageName))
			{
				ItemNewAssets.Add(Asset);
				SessionNewAssets.Add(Asset);
				Result.ImportedObjectPaths.AddUnique(Asset.GetObjectPathString());
				FAdminImportedAssetResult& Imported =
					Result.ImportedAssets.AddDefaulted_GetRef();
				Imported.ObjectPath = Asset.GetObjectPathString();
				Imported.AssetType = Asset.AssetClassPath.ToString();
				if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Asset.GetAsset()))
				{
					if (SkeletalMesh->GetSkeleton() != nullptr)
					{
						Imported.SkeletonPath = SkeletalMesh->GetSkeleton()->GetPathName();
					}
				}
				else if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(Asset.GetAsset()))
				{
					if (AnimSequence->GetSkeleton() != nullptr)
					{
						Imported.SkeletonPath = AnimSequence->GetSkeleton()->GetPathName();
					}
					Imported.AnimSequenceFrames = AnimSequence->GetNumberOfSampledKeys();
				}
			}
		}

		Result.bImportSucceeded &= !ItemNewAssets.IsEmpty();
		if (Item.Kind != EAdminImportAssetKind::RiggedVehicle)
		{
			continue;
		}

		int32 SkeletalMeshCount = 0;
		int32 AnimSequenceCount = 0;
		USkeletalMesh* ImportedMesh = nullptr;
		UAnimSequence* ImportedSequence = nullptr;
		for (const FAssetData& Asset : ItemNewAssets)
		{
			UObject* ImportedObject = Asset.GetAsset();
			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(ImportedObject))
			{
				++SkeletalMeshCount;
				ImportedMesh = SkeletalMesh;
			}
			else if (UAnimSequence* AnimSequence = Cast<UAnimSequence>(ImportedObject))
			{
				++AnimSequenceCount;
				ImportedSequence = AnimSequence;
			}
		}
		if (SkeletalMeshCount != 1)
		{
			AdminImport::AddError(Item, FString::Printf(
				TEXT("骨骼车辆导入必须产出 1 个 SkeletalMesh，实际为 %d。"),
				SkeletalMeshCount));
		}
		if (ImportedMesh == nullptr || ImportedMesh->GetSkeleton() == nullptr)
		{
			AdminImport::AddError(Item, TEXT("骨骼车辆导入产物必须关联有效 Skeleton。"));
		}
		if (AnimSequenceCount != 1)
		{
			AdminImport::AddError(Item, FString::Printf(
				TEXT("骨骼车辆导入必须产出 1 条完整 AnimSequence，实际为 %d。"),
				AnimSequenceCount));
		}
		if (ImportedSequence != nullptr && ImportedMesh != nullptr
			&& ImportedSequence->GetSkeleton() != ImportedMesh->GetSkeleton())
		{
			AdminImport::AddError(Item, TEXT("AnimSequence 与 SkeletalMesh 必须使用同一 Skeleton。"));
		}
		if (ImportedSequence != nullptr)
		{
			const int32 ExpectedFrames =
				Item.SequenceEndFrame - Item.SequenceStartFrame + 1;
			const int32 ActualFrames = ImportedSequence->GetNumberOfSampledKeys();
			if (ActualFrames != ExpectedFrames)
			{
				AdminImport::AddError(Item, FString::Printf(
					TEXT("AnimSequence 帧数不匹配：sidecar=%d，实际=%d。"),
					ExpectedFrames,
					ActualFrames));
			}
		}
		Item.bPassed = Item.Errors.IsEmpty();
		Result.bImportSucceeded &= Item.bPassed;
	}

	if (!Result.bImportSucceeded && !SessionNewAssets.IsEmpty())
	{
		const int32 DeletedCount = ObjectTools::DeleteAssets(SessionNewAssets, false);
		if (DeletedCount != SessionNewAssets.Num())
		{
			UE_LOG(
				LogAdminImport,
				Error,
				TEXT("导入失败后清理暂存资产不完整：期望=%d，实际=%d。"),
				SessionNewAssets.Num(),
				DeletedCount);
		}
	}

	FAdminImportPreflight::WriteJsonReport(Result);
	if (Result.bImportSucceeded)
	{
		UE_LOG(
			LogAdminImport,
			Display,
			TEXT("管理员暂存导入成功：会话=%s，目标=%s，报告=%s"),
			*Result.SessionId,
			*Result.StagingPath,
			*Result.ReportPath);
	}
	else
	{
		UE_LOG(
			LogAdminImport,
			Error,
			TEXT("管理员暂存导入失败：会话=%s，目标=%s，报告=%s"),
			*Result.SessionId,
			*Result.StagingPath,
			*Result.ReportPath);
	}
	return Result.bImportSucceeded;
}
