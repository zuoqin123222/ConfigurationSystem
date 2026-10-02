#include "Sc01V2Domain.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace Sc01V2
{
	namespace Private
	{
		void SetError(FError& OutError, const TCHAR* Code, const FString& Message)
		{
			OutError.Code = Code;
			OutError.Message = Message;
		}

		bool ReadString(
			const TSharedPtr<FJsonObject>& Object,
			const TCHAR* Field,
			FString& Out,
			FError& OutError)
		{
			if (!Object.IsValid() || !Object->TryGetStringField(Field, Out) || Out.IsEmpty())
			{
				SetError(OutError, TEXT("INVALID_CATALOG"),
					FString::Printf(TEXT("%s 必须是非空字符串"), Field));
				return false;
			}
			return true;
		}

		bool ReadBool(
			const TSharedPtr<FJsonObject>& Object,
			const TCHAR* Field,
			bool& Out,
			FError& OutError)
		{
			if (!Object.IsValid() || !Object->TryGetBoolField(Field, Out))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"),
					FString::Printf(TEXT("%s 必须是布尔值"), Field));
				return false;
			}
			return true;
		}

		bool IsNullField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
		{
			const TSharedPtr<FJsonValue>* Value = Object.IsValid()
				? Object->Values.Find(Field)
				: nullptr;
			return Value != nullptr && Value->IsValid() && (*Value)->Type == EJson::Null;
		}

		bool ReadNullableString(
			const TSharedPtr<FJsonObject>& Object,
			const TCHAR* Field,
			TOptional<FString>& Out,
			FError& OutError)
		{
			if (IsNullField(Object, Field))
			{
				Out.Reset();
				return true;
			}
			FString Value;
			if (!ReadString(Object, Field, Value, OutError))
			{
				return false;
			}
			Out = MoveTemp(Value);
			return true;
		}

		bool ReadNullableInteger(
			const TSharedPtr<FJsonObject>& Object,
			const TCHAR* Field,
			TOptional<int64>& Out,
			FError& OutError)
		{
			if (IsNullField(Object, Field))
			{
				Out.Reset();
				return true;
			}
			double Number = 0.0;
			if (!Object.IsValid()
				|| !Object->TryGetNumberField(Field, Number)
				|| !FMath::IsFinite(Number)
				|| FMath::FloorToDouble(Number) != Number
				|| Number < 0.0
				|| Number > static_cast<double>(MAX_int64))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"),
					FString::Printf(TEXT("%s 必须是非负整数或 null"), Field));
				return false;
			}
			Out = static_cast<int64>(Number);
			return true;
		}

		bool ParseCatalog(
			const TSharedPtr<FJsonObject>& Root,
			FCatalog& Out,
			FError& OutError)
		{
			if (!ReadString(Root, TEXT("schemaVersion"), Out.SchemaVersion, OutError)
				|| !ReadString(Root, TEXT("catalogVersion"), Out.CatalogVersion, OutError)
				|| !ReadString(Root, TEXT("lifecycle"), Out.Lifecycle, OutError)
				|| !ReadString(Root, TEXT("currency"), Out.Currency, OutError))
			{
				return false;
			}

			const TSharedPtr<FJsonObject>* Vehicle = nullptr;
			if (!Root->TryGetObjectField(TEXT("vehicle"), Vehicle)
				|| !ReadString(*Vehicle, TEXT("vehicleId"), Out.VehicleId, OutError)
				|| !ReadString(*Vehicle, TEXT("displayName"), Out.VehicleDisplayName, OutError)
				|| !ReadString(*Vehicle, TEXT("priceStatus"), Out.PriceStatus, OutError)
				|| !ReadBool(*Vehicle, TEXT("quotable"), Out.bQuotable, OutError))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("vehicle 结构非法"));
				return false;
			}
			Out.bBasePriceIsNull = IsNullField(*Vehicle, TEXT("basePriceMinor"));

			const TArray<TSharedPtr<FJsonValue>>* SelectionOrder = nullptr;
			if (!Root->TryGetArrayField(TEXT("selectionOrder"), SelectionOrder))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("selectionOrder 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *SelectionOrder)
			{
				FString SurfaceId;
				if (!Value.IsValid() || !Value->TryGetString(SurfaceId) || SurfaceId.IsEmpty())
				{
					SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("selectionOrder 包含非法 surfaceId"));
					return false;
				}
				Out.SelectionOrder.Add(MoveTemp(SurfaceId));
			}

			const TArray<TSharedPtr<FJsonValue>>* Regions = nullptr;
			if (!Root->TryGetArrayField(TEXT("regions"), Regions))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("regions 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Regions)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FRegion Region;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("regionId"), Region.RegionId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Region.DisplayName, OutError))
				{
					return false;
				}
				Out.Regions.Add(MoveTemp(Region));
			}

			const TArray<TSharedPtr<FJsonValue>>* Categories = nullptr;
			if (!Root->TryGetArrayField(TEXT("categories"), Categories))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("categories 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Categories)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FCategory Category;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("categoryId"), Category.CategoryId, OutError)
					|| !ReadString(*Object, TEXT("regionId"), Category.RegionId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Category.DisplayName, OutError))
				{
					return false;
				}
				Out.Categories.Add(MoveTemp(Category));
			}

			const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
			if (!Root->TryGetArrayField(TEXT("components"), Components))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("components 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Components)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FComponent Component;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("componentId"), Component.ComponentId, OutError)
					|| !ReadString(*Object, TEXT("categoryId"), Component.CategoryId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Component.DisplayName, OutError))
				{
					return false;
				}
				Out.Components.Add(MoveTemp(Component));
			}

			const TArray<TSharedPtr<FJsonValue>>* Surfaces = nullptr;
			if (!Root->TryGetArrayField(TEXT("surfaces"), Surfaces))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("surfaces 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Surfaces)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FSurface Surface;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("surfaceId"), Surface.SurfaceId, OutError)
					|| !ReadString(*Object, TEXT("componentId"), Surface.ComponentId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Surface.DisplayName, OutError)
					|| !ReadBool(*Object, TEXT("required"), Surface.bRequired, OutError))
				{
					return false;
				}
				Out.Surfaces.Add(MoveTemp(Surface));
			}

			const TArray<TSharedPtr<FJsonValue>>* Families = nullptr;
			if (!Root->TryGetArrayField(TEXT("materialFamilies"), Families))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("materialFamilies 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Families)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FMaterialFamily Family;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("materialFamilyId"), Family.MaterialFamilyId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Family.DisplayName, OutError))
				{
					return false;
				}
				Out.MaterialFamilies.Add(MoveTemp(Family));
			}

			const TArray<TSharedPtr<FJsonValue>>* Variants = nullptr;
			if (!Root->TryGetArrayField(TEXT("materialVariants"), Variants))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("materialVariants 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Variants)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FMaterialVariant Variant;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("variantId"), Variant.VariantId, OutError)
					|| !ReadString(*Object, TEXT("materialFamilyId"), Variant.MaterialFamilyId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Variant.DisplayName, OutError)
					|| !ReadNullableString(*Object, TEXT("colorCode"), Variant.ColorCode, OutError))
				{
					return false;
				}
				Out.MaterialVariants.Add(MoveTemp(Variant));
			}

			const TArray<TSharedPtr<FJsonValue>>* Options = nullptr;
			if (!Root->TryGetArrayField(TEXT("options"), Options))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("options 必须是数组"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Options)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FOption Option;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("optionId"), Option.OptionId, OutError)
					|| !ReadString(*Object, TEXT("surfaceId"), Option.SurfaceId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Option.DisplayName, OutError)
					|| !ReadNullableString(*Object, TEXT("colorCode"), Option.ColorCode, OutError)
					|| !ReadNullableString(*Object, TEXT("finish"), Option.Finish, OutError)
					|| !ReadBool(*Object, TEXT("renderRelevant"), Option.bRenderRelevant, OutError))
				{
					return false;
				}
				if (!IsNullField(*Object, TEXT("materialFamilyId")))
				{
					FString FamilyId;
					if (!ReadString(*Object, TEXT("materialFamilyId"), FamilyId, OutError))
					{
						return false;
					}
					Option.MaterialFamilyId = MoveTemp(FamilyId);
				}

				const TSharedPtr<FJsonObject>* Pricing = nullptr;
				FString Status;
				if (!(*Object)->TryGetObjectField(TEXT("pricing"), Pricing)
					|| !ReadNullableInteger(*Pricing, TEXT("unitPriceMinor"), Option.Pricing.UnitPriceMinor, OutError)
					|| !ReadNullableInteger(*Pricing, TEXT("quantity"), Option.Pricing.Quantity, OutError)
					|| !ReadString(*Pricing, TEXT("pricingUnit"), Option.Pricing.PricingUnit, OutError)
					|| !ReadBool(*Pricing, TEXT("isStandard"), Option.Pricing.bIsStandard, OutError)
					|| !ReadString(*Pricing, TEXT("status"), Status, OutError)
					|| !ReadBool(*Pricing, TEXT("quotable"), Option.Pricing.bQuotable, OutError))
				{
					return false;
				}
				Option.Pricing.bConfirmed = Status == TEXT("confirmed");
				if (Status != TEXT("confirmed") && Status != TEXT("unconfirmed"))
				{
					SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("pricing.status 非法"));
					return false;
				}
				Out.Options.Add(MoveTemp(Option));
			}
			return true;
		}

		uint32 RotateRight(const uint32 Value, const uint32 Bits)
		{
			return (Value >> Bits) | (Value << (32u - Bits));
		}

		FString Sha256Digest24(const FString& Input)
		{
			const FTCHARToUTF8 Utf8(*Input);
			TArray<uint8> Message;
			Message.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			const uint64 BitLength = static_cast<uint64>(Message.Num()) * 8u;
			Message.Add(0x80);
			while ((Message.Num() % 64) != 56)
			{
				Message.Add(0);
			}
			for (int32 Shift = 56; Shift >= 0; Shift -= 8)
			{
				Message.Add(static_cast<uint8>((BitLength >> Shift) & 0xffu));
			}

			uint32 Hash[] = {
				0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
				0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
			};
			static constexpr uint32 K[] = {
				0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
				0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
				0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
				0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
				0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
				0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
				0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
				0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
			};

			for (int32 Offset = 0; Offset < Message.Num(); Offset += 64)
			{
				uint32 W[64] = {};
				for (int32 Index = 0; Index < 16; ++Index)
				{
					const int32 Byte = Offset + Index * 4;
					W[Index] = (static_cast<uint32>(Message[Byte]) << 24)
						| (static_cast<uint32>(Message[Byte + 1]) << 16)
						| (static_cast<uint32>(Message[Byte + 2]) << 8)
						| static_cast<uint32>(Message[Byte + 3]);
				}
				for (int32 Index = 16; Index < 64; ++Index)
				{
					const uint32 S0 = RotateRight(W[Index - 15], 7) ^ RotateRight(W[Index - 15], 18) ^ (W[Index - 15] >> 3);
					const uint32 S1 = RotateRight(W[Index - 2], 17) ^ RotateRight(W[Index - 2], 19) ^ (W[Index - 2] >> 10);
					W[Index] = W[Index - 16] + S0 + W[Index - 7] + S1;
				}
				uint32 A = Hash[0], B = Hash[1], C = Hash[2], D = Hash[3];
				uint32 E = Hash[4], F = Hash[5], G = Hash[6], H = Hash[7];
				for (int32 Index = 0; Index < 64; ++Index)
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
				Hash[0] += A; Hash[1] += B; Hash[2] += C; Hash[3] += D;
				Hash[4] += E; Hash[5] += F; Hash[6] += G; Hash[7] += H;
			}

			FString Result;
			Result.Reserve(24);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Result += FString::Printf(TEXT("%08x"), Hash[Index]);
			}
			return Result;
		}

		bool IsPaintValid(const FPaintCustomization& Paint)
		{
			if (Paint.ColorHex.Len() != 7 || Paint.ColorHex[0] != TEXT('#'))
			{
				return false;
			}
			for (int32 Index = 1; Index < Paint.ColorHex.Len(); ++Index)
			{
				if (!FChar::IsHexDigit(Paint.ColorHex[Index]))
				{
					return false;
				}
			}
			const double Values[] = {
				Paint.Metallic, Paint.Roughness, Paint.ClearCoat,
				Paint.OrangePeel, Paint.FlakeIntensity
			};
			for (const double Value : Values)
			{
				if (!FMath::IsFinite(Value) || Value < 0.0 || Value > 1.0)
				{
					return false;
				}
			}
			return true;
		}

		FString CanonicalNumber(const double Value)
		{
			return FString::SanitizeFloat(Value, 0);
		}

		void AppendCustomizationLines(
			TArray<FString>& Lines,
			const FCatalog& Catalog,
			const FCustomizations& Customizations,
			const TSet<FString>* RenderRelevant)
		{
			for (const FString& SurfaceId : Catalog.SelectionOrder)
			{
				if (RenderRelevant != nullptr && !RenderRelevant->Contains(SurfaceId))
				{
					continue;
				}
				const FCustomization* Customization = Customizations.Find(SurfaceId);
				if (Customization == nullptr)
				{
					continue;
				}
				if (Customization->Kind == ECustomizationKind::MaterialVariant)
				{
					Lines.Add(FString::Printf(
						TEXT("customizations.%s.materialVariantId=%s"),
						*SurfaceId,
						*Customization->MaterialVariantId));
					continue;
				}
				const FPaintCustomization& Paint = Customization->Paint;
				Lines.Add(FString::Printf(TEXT("customizations.%s.colorHex=%s"), *SurfaceId, *Paint.ColorHex.ToUpper()));
				Lines.Add(FString::Printf(TEXT("customizations.%s.metallic=%s"), *SurfaceId, *CanonicalNumber(Paint.Metallic)));
				Lines.Add(FString::Printf(TEXT("customizations.%s.roughness=%s"), *SurfaceId, *CanonicalNumber(Paint.Roughness)));
				Lines.Add(FString::Printf(TEXT("customizations.%s.clearCoat=%s"), *SurfaceId, *CanonicalNumber(Paint.ClearCoat)));
				Lines.Add(FString::Printf(TEXT("customizations.%s.orangePeel=%s"), *SurfaceId, *CanonicalNumber(Paint.OrangePeel)));
				Lines.Add(FString::Printf(TEXT("customizations.%s.flakeIntensity=%s"), *SurfaceId, *CanonicalNumber(Paint.FlakeIntensity)));
			}
		}
	}

	bool FCatalogIndex::LoadJsonFile(const FString& Filename, FError& OutError)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Filename))
		{
			Private::SetError(OutError, TEXT("CATALOG_READ_FAILED"),
				FString::Printf(TEXT("无法读取 catalog：%s"), *Filename));
			return false;
		}
		return LoadJson(Json, OutError);
	}

	bool FCatalogIndex::LoadJson(const FString& Json, FError& OutError)
	{
		OutError.Reset();
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Private::SetError(OutError, TEXT("INVALID_CATALOG_JSON"), TEXT("catalog 不是合法 JSON 对象"));
			return false;
		}
		FCatalog Candidate;
		if (!Private::ParseCatalog(Root, Candidate, OutError))
		{
			return false;
		}
		return Initialize(Candidate, OutError);
	}

	bool FCatalogIndex::Initialize(const FCatalog& InCatalog, FError& OutError)
	{
		OutError.Reset();
		FCatalog Candidate = InCatalog;
		TMap<FString, int32> CandidateOptions;
		TMap<FString, int32> CandidateVariants;
		TMap<FString, TArray<FString>> CandidateOptionsBySurface;
		TMap<FString, int32> CandidateRegions;
		TMap<FString, int32> CandidateCategories;
		TMap<FString, int32> CandidateSurfaces;
		TMap<FString, int32> CandidateFamilies;
		TMap<FString, TArray<FString>> CandidateCategoriesByRegion;
		TMap<FString, TArray<FString>> CandidateSurfacesByCategory;
		TMap<FString, TArray<FString>> CandidateVariantsByFamily;
		TSet<FString> SurfaceIds;
		TSet<FString> FamilyIds;

		if (Candidate.SchemaVersion != Sc01V2::SchemaVersion
			|| Candidate.Lifecycle != TEXT("draft")
			|| Candidate.Currency != TEXT("CNY")
			|| Candidate.CatalogVersion.IsEmpty()
			|| Candidate.VehicleId.IsEmpty()
			|| Candidate.VehicleDisplayName.IsEmpty()
			|| Candidate.Regions.IsEmpty()
			|| Candidate.Categories.IsEmpty()
			|| Candidate.Components.IsEmpty()
			|| !Candidate.bBasePriceIsNull
			|| Candidate.PriceStatus != TEXT("unconfirmed")
			|| Candidate.bQuotable
			|| Candidate.SelectionOrder.Num() != RequiredSelectionCount
			|| Candidate.Surfaces.Num() != RequiredSelectionCount)
		{
			Private::SetError(OutError, TEXT("INVALID_CATALOG"),
				TEXT("SC01 v2 catalog 必须是禁止报价、38 表面必选且顺序完整的 2.0.0 draft"));
			return false;
		}

		for (int32 Index = 0; Index < Candidate.Regions.Num(); ++Index)
		{
			const FRegion& Region = Candidate.Regions[Index];
			if (Region.RegionId.IsEmpty() || Region.DisplayName.IsEmpty()
				|| CandidateRegions.Contains(Region.RegionId))
			{
				Private::SetError(OutError, TEXT("INVALID_REGION"), Region.RegionId);
				return false;
			}
			CandidateRegions.Add(Region.RegionId, Index);
			CandidateCategoriesByRegion.Add(Region.RegionId);
		}
		for (int32 Index = 0; Index < Candidate.Categories.Num(); ++Index)
		{
			const FCategory& Category = Candidate.Categories[Index];
			if (Category.CategoryId.IsEmpty() || Category.DisplayName.IsEmpty()
				|| CandidateCategories.Contains(Category.CategoryId)
				|| !CandidateRegions.Contains(Category.RegionId))
			{
				Private::SetError(OutError, TEXT("INVALID_CATEGORY"), Category.CategoryId);
				return false;
			}
			CandidateCategories.Add(Category.CategoryId, Index);
			CandidateCategoriesByRegion.FindChecked(Category.RegionId).Add(Category.CategoryId);
			CandidateSurfacesByCategory.Add(Category.CategoryId);
		}
		TMap<FString, FString> CategoryByComponent;
		for (const FComponent& Component : Candidate.Components)
		{
			if (Component.ComponentId.IsEmpty() || Component.DisplayName.IsEmpty()
				|| CategoryByComponent.Contains(Component.ComponentId)
				|| !CandidateCategories.Contains(Component.CategoryId))
			{
				Private::SetError(OutError, TEXT("INVALID_COMPONENT"), Component.ComponentId);
				return false;
			}
			CategoryByComponent.Add(Component.ComponentId, Component.CategoryId);
		}

		for (int32 Index = 0; Index < Candidate.SelectionOrder.Num(); ++Index)
		{
			const FString& SurfaceId = Candidate.SelectionOrder[Index];
			const FSurface& Surface = Candidate.Surfaces[Index];
			const FString* CategoryId = CategoryByComponent.Find(Surface.ComponentId);
			if (SurfaceId.IsEmpty()
				|| SurfaceIds.Contains(SurfaceId)
				|| !Surface.bRequired
				|| Surface.SurfaceId != SurfaceId
				|| Surface.DisplayName.IsEmpty()
				|| CategoryId == nullptr)
			{
				Private::SetError(OutError, TEXT("INVALID_CATALOG"),
					TEXT("selectionOrder 必须与 38 个唯一必选 surface 完全同序"));
				return false;
			}
			SurfaceIds.Add(SurfaceId);
			CandidateSurfaces.Add(SurfaceId, Index);
			CandidateSurfacesByCategory.FindChecked(*CategoryId).Add(SurfaceId);
			CandidateOptionsBySurface.Add(SurfaceId);
		}

		for (int32 Index = 0; Index < Candidate.MaterialFamilies.Num(); ++Index)
		{
			const FMaterialFamily& Family = Candidate.MaterialFamilies[Index];
			if (Family.MaterialFamilyId.IsEmpty() || Family.DisplayName.IsEmpty()
				|| FamilyIds.Contains(Family.MaterialFamilyId))
			{
				Private::SetError(OutError, TEXT("DUPLICATE_MATERIAL_FAMILY"), Family.MaterialFamilyId);
				return false;
			}
			FamilyIds.Add(Family.MaterialFamilyId);
			CandidateFamilies.Add(Family.MaterialFamilyId, Index);
			CandidateVariantsByFamily.Add(Family.MaterialFamilyId);
		}
		for (int32 Index = 0; Index < Candidate.MaterialVariants.Num(); ++Index)
		{
			const FMaterialVariant& Variant = Candidate.MaterialVariants[Index];
			if (Variant.VariantId.IsEmpty() || CandidateVariants.Contains(Variant.VariantId))
			{
				Private::SetError(OutError, TEXT("DUPLICATE_VARIANT_ID"), Variant.VariantId);
				return false;
			}
			if (!FamilyIds.Contains(Variant.MaterialFamilyId))
			{
				Private::SetError(OutError, TEXT("UNKNOWN_MATERIAL_FAMILY"), Variant.VariantId);
				return false;
			}
			CandidateVariants.Add(Variant.VariantId, Index);
			CandidateVariantsByFamily.FindChecked(Variant.MaterialFamilyId).Add(Variant.VariantId);
		}

		for (int32 Index = 0; Index < Candidate.Options.Num(); ++Index)
		{
			const FOption& Option = Candidate.Options[Index];
			if (Option.OptionId.IsEmpty() || CandidateOptions.Contains(Option.OptionId))
			{
				Private::SetError(OutError, TEXT("DUPLICATE_OPTION_ID"), Option.OptionId);
				return false;
			}
			if (!SurfaceIds.Contains(Option.SurfaceId))
			{
				Private::SetError(OutError, TEXT("UNKNOWN_SURFACE"), Option.OptionId);
				return false;
			}
			if (Option.MaterialFamilyId.IsSet() && !FamilyIds.Contains(Option.MaterialFamilyId.GetValue()))
			{
				Private::SetError(OutError, TEXT("UNKNOWN_MATERIAL_FAMILY"), Option.OptionId);
				return false;
			}
			if (Option.Pricing.bQuotable
				|| (Option.Pricing.UnitPriceMinor.IsSet() != Option.Pricing.bConfirmed)
				|| (Option.Pricing.Quantity.IsSet() && Option.Pricing.Quantity.GetValue() < 1))
			{
				Private::SetError(OutError, TEXT("INVALID_PRICING"), Option.OptionId);
				return false;
			}
			CandidateOptions.Add(Option.OptionId, Index);
			CandidateOptionsBySurface.FindChecked(Option.SurfaceId).Add(Option.OptionId);
		}
		for (const FString& SurfaceId : Candidate.SelectionOrder)
		{
			if (CandidateOptionsBySurface.FindChecked(SurfaceId).IsEmpty())
			{
				Private::SetError(OutError, TEXT("SURFACE_WITHOUT_OPTIONS"), SurfaceId);
				return false;
			}
		}

		Catalog = MoveTemp(Candidate);
		OptionIndexById = MoveTemp(CandidateOptions);
		VariantIndexById = MoveTemp(CandidateVariants);
		OptionIdsBySurface = MoveTemp(CandidateOptionsBySurface);
		RegionIndexById = MoveTemp(CandidateRegions);
		CategoryIndexById = MoveTemp(CandidateCategories);
		SurfaceIndexById = MoveTemp(CandidateSurfaces);
		FamilyIndexById = MoveTemp(CandidateFamilies);
		CategoryIdsByRegion = MoveTemp(CandidateCategoriesByRegion);
		SurfaceIdsByCategory = MoveTemp(CandidateSurfacesByCategory);
		VariantIdsByFamily = MoveTemp(CandidateVariantsByFamily);
		bValid = true;
		return true;
	}

	const FOption* FCatalogIndex::FindOption(const FString& OptionId) const
	{
		const int32* Index = OptionIndexById.Find(OptionId);
		return bValid && Index != nullptr ? &Catalog.Options[*Index] : nullptr;
	}

	const FMaterialVariant* FCatalogIndex::FindMaterialVariant(const FString& VariantId) const
	{
		const int32* Index = VariantIndexById.Find(VariantId);
		return bValid && Index != nullptr ? &Catalog.MaterialVariants[*Index] : nullptr;
	}

	const TArray<FString>* FCatalogIndex::FindOptionIdsForSurface(const FString& SurfaceId) const
	{
		return bValid ? OptionIdsBySurface.Find(SurfaceId) : nullptr;
	}

	const TArray<FString>* FCatalogIndex::FindCategoryIdsForRegion(const FString& RegionId) const
	{
		return bValid ? CategoryIdsByRegion.Find(RegionId) : nullptr;
	}

	const TArray<FString>* FCatalogIndex::FindSurfaceIdsForCategory(const FString& CategoryId) const
	{
		return bValid ? SurfaceIdsByCategory.Find(CategoryId) : nullptr;
	}

	const TArray<FString>* FCatalogIndex::FindVariantIdsForMaterialFamily(const FString& MaterialFamilyId) const
	{
		return bValid ? VariantIdsByFamily.Find(MaterialFamilyId) : nullptr;
	}

	const FRegion* FCatalogIndex::FindRegion(const FString& RegionId) const
	{
		const int32* Index = RegionIndexById.Find(RegionId);
		return bValid && Index != nullptr ? &Catalog.Regions[*Index] : nullptr;
	}

	const FCategory* FCatalogIndex::FindCategory(const FString& CategoryId) const
	{
		const int32* Index = CategoryIndexById.Find(CategoryId);
		return bValid && Index != nullptr ? &Catalog.Categories[*Index] : nullptr;
	}

	const FSurface* FCatalogIndex::FindSurface(const FString& SurfaceId) const
	{
		const int32* Index = SurfaceIndexById.Find(SurfaceId);
		return bValid && Index != nullptr ? &Catalog.Surfaces[*Index] : nullptr;
	}

	const FMaterialFamily* FCatalogIndex::FindMaterialFamily(const FString& MaterialFamilyId) const
	{
		const int32* Index = FamilyIndexById.Find(MaterialFamilyId);
		return bValid && Index != nullptr ? &Catalog.MaterialFamilies[*Index] : nullptr;
	}

	FCustomization FCustomization::ForMaterialVariant(const FString& VariantId)
	{
		FCustomization Result;
		Result.Kind = ECustomizationKind::MaterialVariant;
		Result.MaterialVariantId = VariantId;
		return Result;
	}

	FCustomization FCustomization::ForPaint(const FPaintCustomization& Value)
	{
		FCustomization Result;
		Result.Kind = ECustomizationKind::Paint;
		Result.Paint = Value;
		Result.Paint.ColorHex = Result.Paint.ColorHex.ToUpper();
		return Result;
	}

	bool ValidateSelections(
		const FSelections& Selections,
		const FCatalogIndex& Catalog,
		FError& OutError)
	{
		OutError.Reset();
		if (!Catalog.IsValid() || Selections.Num() != RequiredSelectionCount)
		{
			Private::SetError(OutError, TEXT("INVALID_SELECTIONS"),
				TEXT("selections 必须恰好包含 selectionOrder 中的全部 38 个表面"));
			return false;
		}
		for (const FString& SurfaceId : Catalog.GetCatalog().SelectionOrder)
		{
			const FString* OptionId = Selections.Find(SurfaceId);
			const FOption* Option = OptionId != nullptr ? Catalog.FindOption(*OptionId) : nullptr;
			if (Option == nullptr || Option->SurfaceId != SurfaceId)
			{
				Private::SetError(OutError, TEXT("INVALID_OPTION"),
					FString::Printf(TEXT("%s 包含未知或跨表面的 optionId"), *SurfaceId));
				return false;
			}
		}
		return true;
	}

	bool ValidateCustomizations(
		const FCustomizations& Customizations,
		const FSelections& Selections,
		const FCatalogIndex& Catalog,
		FError& OutError)
	{
		OutError.Reset();
		for (const TPair<FString, FCustomization>& Pair : Customizations)
		{
			const FString* OptionId = Selections.Find(Pair.Key);
			const FOption* Option = OptionId != nullptr ? Catalog.FindOption(*OptionId) : nullptr;
			if (Option == nullptr || Option->SurfaceId != Pair.Key)
			{
				Private::SetError(OutError, TEXT("INVALID_CUSTOMIZATIONS"),
					TEXT("customizations 包含未知 surfaceId"));
				return false;
			}
			if (Pair.Value.Kind == ECustomizationKind::MaterialVariant)
			{
				const FMaterialVariant* Variant = Catalog.FindMaterialVariant(Pair.Value.MaterialVariantId);
				if (Variant == nullptr)
				{
					Private::SetError(OutError, TEXT("INVALID_MATERIAL_VARIANT"),
						FString::Printf(TEXT("%s 引用了未知 materialVariantId"), *Pair.Key));
					return false;
				}
				if (!Option->MaterialFamilyId.IsSet()
					|| Variant->MaterialFamilyId != Option->MaterialFamilyId.GetValue())
				{
					Private::SetError(OutError, TEXT("MATERIAL_VARIANT_FAMILY_MISMATCH"),
						FString::Printf(TEXT("%s 的材料色卡与所选选项材料族不匹配"), *Pair.Key));
					return false;
				}
			}
			else if (*OptionId != CustomPaintOptionId || !Private::IsPaintValid(Pair.Value.Paint))
			{
				Private::SetError(OutError, TEXT("INVALID_PAINT_CUSTOMIZATION"),
					FString::Printf(TEXT("%s 自定义车漆必须包含合法色值和 0 到 1 的完整参数"), *Pair.Key));
				return false;
			}
		}
		return true;
	}

	bool DeriveConfiguration(
		const FSelections& Selections,
		const FCustomizations& Customizations,
		const FCatalogIndex& Catalog,
		FConfiguration& OutConfiguration,
		FError& OutError)
	{
		if (!ValidateSelections(Selections, Catalog, OutError)
			|| !ValidateCustomizations(Customizations, Selections, Catalog, OutError))
		{
			return false;
		}
		const FCatalog& Data = Catalog.GetCatalog();
		TArray<FString> CanonicalLines = {
			FString::Printf(TEXT("schemaVersion=%s"), Sc01V2::SchemaVersion),
			FString::Printf(TEXT("catalogVersion=%s"), *Data.CatalogVersion),
			FString::Printf(TEXT("vehicleId=%s"), *Data.VehicleId)
		};
		TArray<FString> RenderLines = CanonicalLines;
		TSet<FString> RenderRelevant;
		for (const FString& SurfaceId : Data.SelectionOrder)
		{
			const FString& OptionId = Selections.FindChecked(SurfaceId);
			const FString Line = SurfaceId + TEXT("=") + OptionId;
			CanonicalLines.Add(Line);
			if (Catalog.FindOption(OptionId)->bRenderRelevant)
			{
				RenderRelevant.Add(SurfaceId);
				RenderLines.Add(Line);
			}
		}
		Private::AppendCustomizationLines(CanonicalLines, Data, Customizations, nullptr);
		Private::AppendCustomizationLines(RenderLines, Data, Customizations, &RenderRelevant);

		FConfiguration Candidate;
		Candidate.SchemaVersion = Sc01V2::SchemaVersion;
		Candidate.CatalogVersion = Data.CatalogVersion;
		Candidate.VehicleId = Data.VehicleId;
		Candidate.ConfigurationId = TEXT("cfg-") + Private::Sha256Digest24(FString::Join(CanonicalLines, TEXT("\n")));
		Candidate.RenderKey = FString::Printf(
			TEXT("%s__%s__render-%s"),
			*Data.VehicleId,
			*Data.CatalogVersion,
			*Private::Sha256Digest24(FString::Join(RenderLines, TEXT("\n"))));
		Candidate.Selections = Selections;
		Candidate.Customizations = Customizations;
		for (TPair<FString, FCustomization>& Pair : Candidate.Customizations)
		{
			if (Pair.Value.Kind == ECustomizationKind::Paint)
			{
				Pair.Value.Paint.ColorHex = Pair.Value.Paint.ColorHex.ToUpper();
			}
		}
		OutConfiguration = MoveTemp(Candidate);
		return true;
	}
}
