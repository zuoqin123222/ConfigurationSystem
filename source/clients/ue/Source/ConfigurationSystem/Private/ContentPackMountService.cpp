#include "ContentPackMountService.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IPlatformFilePak.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogContentPackMount, Log, All);

namespace ContentPack
{
	constexpr int64 ShaBufferBytes = 1024 * 1024;

	bool IsStableId(const FString& Value)
	{
		if (Value.IsEmpty() || Value.StartsWith(TEXT("-")) || Value.EndsWith(TEXT("-")))
		{
			return false;
		}
		bool bPreviousDash = false;
		for (const TCHAR Character : Value)
		{
			const bool bDash = Character == TEXT('-');
			const bool bLowerAlphaNumeric =
				(Character >= TEXT('0') && Character <= TEXT('9'))
				|| (Character >= TEXT('a') && Character <= TEXT('z'));
			if ((!bLowerAlphaNumeric && !bDash) || (bDash && bPreviousDash))
			{
				return false;
			}
			bPreviousDash = bDash;
		}
		return true;
	}

	bool IsEngineVersion(const FString& Value)
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT("."), false);
		if (Parts.Num() < 2 || Parts.Num() > 3 || Parts[0] != TEXT("5"))
		{
			return false;
		}
		for (int32 Index = 1; Index < Parts.Num(); ++Index)
		{
			if (Parts[Index].IsEmpty()
				|| !Parts[Index].Equals(FString::FromInt(FCString::Atoi(*Parts[Index]))))
			{
				return false;
			}
		}
		return true;
	}

	bool IsLowerSha256(const FString& Value)
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

	bool IsPrimaryAssetName(const FString& Value)
	{
		return IsStableId(Value);
	}

	bool IsPrimaryAssetType(const FString& Value)
	{
		if (Value.IsEmpty()
			|| !((Value[0] >= TEXT('A') && Value[0] <= TEXT('Z'))
				|| (Value[0] >= TEXT('a') && Value[0] <= TEXT('z'))))
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			const bool bAsciiAlphaNumeric =
				(Character >= TEXT('0') && Character <= TEXT('9'))
				|| (Character >= TEXT('A') && Character <= TEXT('Z'))
				|| (Character >= TEXT('a') && Character <= TEXT('z'));
			if (!bAsciiAlphaNumeric && Character != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	bool ParsePrimaryAssetId(const FString& Value, FPrimaryAssetId& OutId)
	{
		FString Type;
		FString Name;
		if (!Value.Split(TEXT(":"), &Type, &Name)
			|| Name.Contains(TEXT(":"))
			|| !IsPrimaryAssetType(Type)
			|| !IsPrimaryAssetName(Name))
		{
			return false;
		}
		OutId = FPrimaryAssetId(FPrimaryAssetType(*Type), FName(*Name));
		return true;
	}

	bool IsDirectPakFileName(const FString& Value)
	{
		return !Value.IsEmpty()
			&& Value == FPaths::GetCleanFilename(Value)
			&& FPaths::GetExtension(Value, true).Equals(TEXT(".pak"), ESearchCase::IgnoreCase)
			&& IsStableId(FPaths::GetBaseFilename(Value));
	}

	FString ToVirtualMountPoint(FString EmbeddedMountPoint)
	{
		FPaths::NormalizeDirectoryName(EmbeddedMountPoint);
		const int32 ContentIndex = EmbeddedMountPoint.Find(
			TEXT("/Content/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (ContentIndex != INDEX_NONE)
		{
			EmbeddedMountPoint = TEXT("/Game/")
				+ EmbeddedMountPoint.Mid(ContentIndex + FCString::Strlen(TEXT("/Content/")));
		}
		if (!EmbeddedMountPoint.EndsWith(TEXT("/")))
		{
			EmbeddedMountPoint += TEXT("/");
		}
		return EmbeddedMountPoint;
	}

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

	bool ReadRequiredString(
		const TSharedPtr<FJsonObject>& Object,
		const TCHAR* Field,
		FString& OutValue,
		TArray<FString>& Errors,
		const TCHAR* Context = TEXT("manifest"))
	{
		if (!Object.IsValid() || !Object->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("%s.%s 必须是非空字符串。"), Context, Field));
			return false;
		}
		return true;
	}

	void RejectUnknownFields(
		const TSharedPtr<FJsonObject>& Object,
		const TSet<FString>& Allowed,
		const TCHAR* Context,
		TArray<FString>& Errors)
	{
		if (!Object.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			if (!Allowed.Contains(Pair.Key))
			{
				Errors.Add(FString::Printf(TEXT("%s.%s 是不允许的字段。"), Context, *Pair.Key));
			}
		}
	}

	bool DefaultInspectPak(
		const FString& PakPath,
		FString& OutEmbeddedMountPoint,
		FString& OutError)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		TRefCountPtr<FPakFile> PakFile = new FPakFile(&PlatformFile, *PakPath, false);
		if (!PakFile->IsValid())
		{
			OutError = TEXT("pak 容器无效、索引不可读或需要未提供的密钥。");
			return false;
		}
		OutEmbeddedMountPoint = ToVirtualMountPoint(PakFile->GetMountPoint());
		return true;
	}
}

FContentPackMountService::FContentPackMountService(FContentPackMountPolicy InPolicy)
	: Policy(MoveTemp(InPolicy))
{
	MountPak = [](const FString& PakPath, const int32 PakOrder)
	{
		return FCoreDelegates::MountPak.IsBound()
			&& FCoreDelegates::MountPak.Execute(PakPath, PakOrder) != nullptr;
	};
	InspectPak = ContentPack::DefaultInspectPak;
}

void FContentPackMountService::SetMountPakForTesting(FMountPak InMountPak)
{
	MountPak = MoveTemp(InMountPak);
}

void FContentPackMountService::SetInspectPakForTesting(FInspectPak InInspectPak)
{
	InspectPak = MoveTemp(InInspectPak);
}

const TSet<FPrimaryAssetId>& FContentPackMountService::GetMountedPrimaryAssetIds() const
{
	return MountedPrimaryAssetIds;
}

bool FContentPackMountService::ComputeFileSha256(
	const FString& Filename,
	FString& OutSha256,
	FString& OutError)
{
	OutSha256.Reset();
	OutError.Reset();
	TUniquePtr<IFileHandle> File(
		FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Filename));
	if (!File)
	{
		OutError = FString::Printf(TEXT("无法打开文件：%s"), *Filename);
		return false;
	}

	ContentPack::FSha256 Hash;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(ContentPack::ShaBufferBytes);
	int64 Remaining = File->Size();
	while (Remaining > 0)
	{
		const int64 ReadBytes = FMath::Min<int64>(Remaining, Buffer.Num());
		if (!File->Read(Buffer.GetData(), ReadBytes))
		{
			OutError = FString::Printf(TEXT("读取文件失败：%s"), *Filename);
			return false;
		}
		Hash.Update(Buffer.GetData(), static_cast<uint64>(ReadBytes));
		Remaining -= ReadBytes;
	}
	OutSha256 = Hash.Final();
	return true;
}

FContentPackMountResult FContentPackMountService::Preflight(
	const FString& ManifestPath,
	const FString& PakPath) const
{
	FContentPackMountResult Result;
	const int64 ManifestBytes = IFileManager::Get().FileSize(*ManifestPath);
	if (ManifestBytes < 1 || ManifestBytes > 1024 * 1024)
	{
		Result.Errors.Add(TEXT("manifest 必须存在且大小不得超过 1 MiB。"));
		return Result;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ManifestPath))
	{
		Result.Errors.Add(TEXT("无法读取 manifest。"));
		return Result;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Result.Errors.Add(TEXT("manifest 不是有效 JSON object。"));
		return Result;
	}

	const TSet<FString> RootFields{
		TEXT("schemaVersion"), TEXT("packId"), TEXT("version"), TEXT("catalogVersion"),
		TEXT("engineVersion"), TEXT("platform"), TEXT("mountPoint"), TEXT("pak"),
		TEXT("primaryAssetIds"), TEXT("providerType")
	};
	ContentPack::RejectUnknownFields(Root, RootFields, TEXT("manifest"), Result.Errors);
	ContentPack::ReadRequiredString(Root, TEXT("schemaVersion"), Result.Manifest.SchemaVersion, Result.Errors);
	ContentPack::ReadRequiredString(Root, TEXT("packId"), Result.Manifest.PackId, Result.Errors);
	ContentPack::ReadRequiredString(Root, TEXT("version"), Result.Manifest.Version, Result.Errors);
	ContentPack::ReadRequiredString(Root, TEXT("catalogVersion"), Result.Manifest.CatalogVersion, Result.Errors);
	ContentPack::ReadRequiredString(Root, TEXT("engineVersion"), Result.Manifest.EngineVersion, Result.Errors);
	ContentPack::ReadRequiredString(Root, TEXT("platform"), Result.Manifest.Platform, Result.Errors);
	ContentPack::ReadRequiredString(Root, TEXT("mountPoint"), Result.Manifest.MountPoint, Result.Errors);

	if (Result.Manifest.SchemaVersion != TEXT("1.0.0")
		&& Result.Manifest.SchemaVersion != TEXT("2.0.0"))
	{
		Result.Errors.Add(TEXT("schemaVersion 仅支持 1.0.0 或 2.0.0。"));
	}
	if (Result.Manifest.SchemaVersion == TEXT("2.0.0"))
	{
		ContentPack::ReadRequiredString(
			Root, TEXT("providerType"), Result.Manifest.ProviderType, Result.Errors);
		if (Result.Manifest.ProviderType != TEXT("material")
			&& Result.Manifest.ProviderType != TEXT("environment")
			&& Result.Manifest.ProviderType != TEXT("vehicle"))
		{
			Result.Errors.Add(TEXT("providerType 必须是 material、environment 或 vehicle。"));
		}
	}
	else if (Root->HasField(TEXT("providerType")))
	{
		Result.Errors.Add(TEXT("schemaVersion 1.0.0 不允许 providerType。"));
	}
	if (!ContentPack::IsStableId(Result.Manifest.PackId)
		|| !ContentPack::IsStableId(Result.Manifest.Version)
		|| !ContentPack::IsStableId(Result.Manifest.CatalogVersion))
	{
		Result.Errors.Add(TEXT("packId、version 与 catalogVersion 必须是小写 kebab-case。"));
	}
	if (!ContentPack::IsEngineVersion(Result.Manifest.EngineVersion))
	{
		Result.Errors.Add(TEXT("engineVersion 必须是 UE 5.x 或 5.x.y。"));
	}
	if (!Policy.CatalogVersion.IsEmpty()
		&& Result.Manifest.CatalogVersion != Policy.CatalogVersion)
	{
		Result.Errors.Add(TEXT("catalogVersion 与当前目录不匹配。"));
	}
	if (!Policy.EngineVersion.IsEmpty()
		&& Result.Manifest.EngineVersion != Policy.EngineVersion)
	{
		Result.Errors.Add(TEXT("engineVersion 与当前运行时不匹配。"));
	}
	if (Result.Manifest.Platform != Policy.Platform)
	{
		Result.Errors.Add(TEXT("platform 与当前目标平台不匹配。"));
	}

	bool bAllowedMountPoint = false;
	for (FString RootPath : Policy.AllowedMountRoots)
	{
		if (!RootPath.EndsWith(TEXT("/")))
		{
			RootPath += TEXT("/");
		}
		const FString Expected = RootPath + Result.Manifest.PackId + TEXT("/");
		bAllowedMountPoint |= Result.Manifest.MountPoint == Expected;
	}
	if (!bAllowedMountPoint)
	{
		Result.Errors.Add(TEXT("mountPoint 不在白名单内，或未严格绑定 packId。"));
	}

	const TSharedPtr<FJsonObject>* PakObjectPointer = nullptr;
	if (!Root->TryGetObjectField(TEXT("pak"), PakObjectPointer)
		|| PakObjectPointer == nullptr
		|| !PakObjectPointer->IsValid())
	{
		Result.Errors.Add(TEXT("pak 必须是 object。"));
	}
	else
	{
		const TSharedPtr<FJsonObject>& PakObject = *PakObjectPointer;
		const TSet<FString> PakFields{TEXT("fileName"), TEXT("bytes"), TEXT("sha256")};
		ContentPack::RejectUnknownFields(PakObject, PakFields, TEXT("pak"), Result.Errors);
		ContentPack::ReadRequiredString(
			PakObject, TEXT("fileName"), Result.Manifest.PakFileName, Result.Errors, TEXT("pak"));
		ContentPack::ReadRequiredString(
			PakObject, TEXT("sha256"), Result.Manifest.PakSha256, Result.Errors, TEXT("pak"));
		double Bytes = 0.0;
		if (!PakObject->TryGetNumberField(TEXT("bytes"), Bytes)
			|| Bytes < 1.0
			|| Bytes > static_cast<double>(Policy.MaximumPakBytes)
			|| FMath::FloorToDouble(Bytes) != Bytes)
		{
			Result.Errors.Add(TEXT("pak.bytes 必须是策略上限内的正整数。"));
		}
		else
		{
			Result.Manifest.PakBytes = static_cast<int64>(Bytes);
		}
	}
	if (!ContentPack::IsDirectPakFileName(Result.Manifest.PakFileName))
	{
		Result.Errors.Add(TEXT("pak.fileName 必须是安全的小写 kebab-case .pak 文件名。"));
	}
	if (!ContentPack::IsLowerSha256(Result.Manifest.PakSha256))
	{
		Result.Errors.Add(TEXT("pak.sha256 必须是 64 位小写十六进制。"));
	}

	const TArray<TSharedPtr<FJsonValue>>* AssetValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("primaryAssetIds"), AssetValues)
		|| AssetValues == nullptr
		|| AssetValues->IsEmpty())
	{
		Result.Errors.Add(TEXT("primaryAssetIds 必须是非空数组。"));
	}
	else
	{
		TSet<FPrimaryAssetId> Seen;
		for (const TSharedPtr<FJsonValue>& Value : *AssetValues)
		{
			FString Text;
			FPrimaryAssetId AssetId;
			if (!Value.IsValid() || !Value->TryGetString(Text)
				|| !ContentPack::ParsePrimaryAssetId(Text, AssetId))
			{
				Result.Errors.Add(TEXT("PrimaryAssetId 格式必须为 Type:kebab-name。"));
				continue;
			}
			if (Seen.Contains(AssetId) || MountedPrimaryAssetIds.Contains(AssetId))
			{
				Result.Errors.Add(FString::Printf(
					TEXT("PrimaryAssetId 重复或与已挂载内容包冲突：%s"), *Text));
			}
			Seen.Add(AssetId);
			Result.Manifest.PrimaryAssetIds.Add(AssetId);
		}
	}

	const FString FullManifestPath = FPaths::ConvertRelativePathToFull(ManifestPath);
	const FString FullPakPath = FPaths::ConvertRelativePathToFull(PakPath);
	const FString DeclaredPakPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::GetPath(FullManifestPath), Result.Manifest.PakFileName));
	if (!FPaths::IsSamePath(FullPakPath, DeclaredPakPath))
	{
		Result.Errors.Add(TEXT("pak 必须与 manifest 同目录，且文件名必须匹配声明。"));
	}

	const int64 ActualPakBytes = IFileManager::Get().FileSize(*FullPakPath);
	if (ActualPakBytes < 1
		|| ActualPakBytes != Result.Manifest.PakBytes
		|| ActualPakBytes > Policy.MaximumPakBytes)
	{
		Result.Errors.Add(FString::Printf(
			TEXT("pak 文件大小不匹配或超过策略上限：声明 %lld，实际 %lld。"),
			Result.Manifest.PakBytes, ActualPakBytes));
	}
	FString HashError;
	if (ActualPakBytes > 0
		&& ActualPakBytes <= Policy.MaximumPakBytes
		&& !ComputeFileSha256(FullPakPath, Result.ActualPakSha256, HashError))
	{
		Result.Errors.Add(HashError);
	}
	else if (!Result.ActualPakSha256.IsEmpty()
		&& Result.ActualPakSha256 != Result.Manifest.PakSha256)
	{
		Result.Errors.Add(TEXT("pak SHA-256 与 manifest 不匹配。"));
	}

	if (Result.Errors.IsEmpty())
	{
		FString EmbeddedMountPoint;
		FString InspectError;
		if (!InspectPak(FullPakPath, EmbeddedMountPoint, InspectError))
		{
			Result.Errors.Add(InspectError);
		}
		else if (EmbeddedMountPoint != Result.Manifest.MountPoint)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("pak 内嵌挂载点与 manifest 不匹配：%s"), *EmbeddedMountPoint));
		}
	}

	Result.bPreflightPassed = Result.Errors.IsEmpty();
	return Result;
}

FContentPackMountResult FContentPackMountService::PreflightAndMount(
	const FString& ManifestPath,
	const FString& PakPath,
	const int32 PakOrder)
{
	FContentPackMountResult Result = Preflight(ManifestPath, PakPath);
	if (!Result.bPreflightPassed)
	{
		UE_LOG(
			LogContentPackMount,
			Warning,
			TEXT("内容包 %s 预检失败；未调用挂载。"),
			*Result.Manifest.PackId);
		return Result;
	}

	if (!MountPak(FPaths::ConvertRelativePathToFull(PakPath), PakOrder))
	{
		Result.Errors.Add(TEXT("底层 pak 平台文件拒绝挂载。"));
		return Result;
	}

	for (const FPrimaryAssetId& AssetId : Result.Manifest.PrimaryAssetIds)
	{
		MountedPrimaryAssetIds.Add(AssetId);
	}
	Result.bMounted = true;
	UE_LOG(
		LogContentPackMount,
		Display,
		TEXT("已安全挂载内容包 %s@%s，声明 %d 个 PrimaryAssetId。"),
		*Result.Manifest.PackId,
		*Result.Manifest.Version,
		Result.Manifest.PrimaryAssetIds.Num());
	return Result;
}
