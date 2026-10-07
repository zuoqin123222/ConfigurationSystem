#include "AutomotiveCatalogDomain.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace AutomotiveCatalog
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

		bool ReadUiAnimationId(
			const TSharedPtr<FJsonObject>& Object,
			TOptional<FString>& Out,
			FError& OutError)
		{
			if (!Object.IsValid() || !Object->HasField(TEXT("ui")))
			{
				return true;
			}
			const TSharedPtr<FJsonObject>* Ui = nullptr;
			if (!Object->TryGetObjectField(TEXT("ui"), Ui)
				|| Ui == nullptr || !Ui->IsValid())
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("ui 必须是对象"));
				return false;
			}
			if (!(*Ui)->HasField(TEXT("animationId"))
				|| IsNullField(*Ui, TEXT("animationId")))
			{
				return true;
			}
			FString AnimationId;
			if (!(*Ui)->TryGetStringField(TEXT("animationId"), AnimationId)
				|| AnimationId.IsEmpty())
			{
				SetError(OutError, TEXT("INVALID_CATALOG"),
					TEXT("ui.animationId 必须是非空字符串或 null"));
				return false;
			}
			Out = MoveTemp(AnimationId);
			return true;
		}

		bool IsGameObjectPath(const FString& Path)
		{
			return Path.StartsWith(TEXT("/Game/"))
				&& Path.Contains(TEXT("."))
				&& FPackageName::IsValidObjectPath(Path);
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
			TOptional<int64> BasePriceMinor;
			if (!ReadNullableInteger(
				*Vehicle,
				TEXT("basePriceMinor"),
				BasePriceMinor,
				OutError)
				|| !BasePriceMinor.IsSet())
			{
				SetError(OutError, TEXT("INVALID_CATALOG"), TEXT("vehicle.basePriceMinor 必须是非负整数"));
				return false;
			}
			Out.BasePriceMinor = BasePriceMinor.GetValue();

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
			const TSharedPtr<FJsonObject>* DefaultSelections = nullptr;
			if (!Root->TryGetObjectField(TEXT("defaultSelections"), DefaultSelections))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"),
					TEXT("defaultSelections 必须是 object"));
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*DefaultSelections)->Values)
			{
				FString OptionId;
				if (!Pair.Value.IsValid()
					|| !Pair.Value->TryGetString(OptionId)
					|| OptionId.IsEmpty())
				{
					SetError(OutError, TEXT("INVALID_CATALOG"),
						TEXT("defaultSelections 包含非法 optionId"));
					return false;
				}
				Out.DefaultSelections.Add(Pair.Key, MoveTemp(OptionId));
			}
			const TSharedPtr<FJsonObject>* OptionIdAliases = nullptr;
			if (!Root->TryGetObjectField(TEXT("optionIdAliases"), OptionIdAliases))
			{
				SetError(OutError, TEXT("INVALID_CATALOG"),
					TEXT("optionIdAliases 必须是 object"));
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*OptionIdAliases)->Values)
			{
				FString OptionId;
				if (!Pair.Value.IsValid()
					|| !Pair.Value->TryGetString(OptionId)
					|| OptionId.IsEmpty())
				{
					SetError(OutError, TEXT("INVALID_CATALOG"),
						TEXT("optionIdAliases 包含非法迁移目标"));
					return false;
				}
				Out.OptionIdAliases.Add(Pair.Key, MoveTemp(OptionId));
			}

			const TArray<TSharedPtr<FJsonValue>>* Animations = nullptr;
			if (!ReadString(
					Root, TEXT("skeletalMeshPath"), Out.SkeletalMeshPath, OutError)
				|| !ReadString(Root, TEXT("sequencePath"), Out.SequencePath, OutError)
				|| !Root->TryGetArrayField(TEXT("animations"), Animations)
				|| Animations->IsEmpty())
			{
				SetError(OutError, TEXT("INVALID_CATALOG"),
					TEXT("animations 必须是非空数组"));
				return false;
			}
			const TSharedPtr<FJsonObject>* SurfaceBinding = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* BindingItems = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* UnsupportedItems = nullptr;
			if (!Root->TryGetObjectField(TEXT("vehicleSurfaceBinding"), SurfaceBinding)
				|| SurfaceBinding == nullptr
				|| !ReadString(*SurfaceBinding, TEXT("schemaVersion"),
					Out.VehicleSurfaceBinding.SchemaVersion, OutError)
				|| !ReadString(*SurfaceBinding, TEXT("capability"),
					Out.VehicleSurfaceBinding.Capability, OutError)
				|| !(*SurfaceBinding)->TryGetArrayField(TEXT("bindings"), BindingItems)
				|| BindingItems == nullptr
				|| !(*SurfaceBinding)->TryGetArrayField(
					TEXT("unsupportedSurfaceIds"), UnsupportedItems)
				|| UnsupportedItems == nullptr)
			{
				SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
					TEXT("vehicleSurfaceBinding 结构非法"));
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *BindingItems)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* SlotValues = nullptr;
				FSurfaceBinding Binding;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("surfaceId"), Binding.SurfaceId, OutError)
					|| !(*Object)->TryGetArrayField(TEXT("materialSlotIds"), SlotValues)
					|| SlotValues == nullptr || SlotValues->IsEmpty())
				{
					SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
						TEXT("vehicleSurfaceBinding.bindings 字段非法"));
					return false;
				}
				for (const TSharedPtr<FJsonValue>& SlotValue : *SlotValues)
				{
					FString SlotId;
					if (!SlotValue.IsValid() || !SlotValue->TryGetString(SlotId)
						|| SlotId.IsEmpty())
					{
						SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
							TEXT("materialSlotIds 必须包含非空字符串"));
						return false;
					}
					Binding.MaterialSlotIds.Add(FName(*SlotId));
				}
				Out.VehicleSurfaceBinding.Bindings.Add(MoveTemp(Binding));
			}
			for (const TSharedPtr<FJsonValue>& Value : *UnsupportedItems)
			{
				FString SurfaceId;
				if (!Value.IsValid() || !Value->TryGetString(SurfaceId)
					|| SurfaceId.IsEmpty())
				{
					SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
						TEXT("unsupportedSurfaceIds 包含非法 surfaceId"));
					return false;
				}
				Out.VehicleSurfaceBinding.UnsupportedSurfaceIds.Add(MoveTemp(SurfaceId));
			}
			for (const TSharedPtr<FJsonValue>& Value : *Animations)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FAnimation Animation;
				double StartFrame = 0.0;
				double EndFrame = 0.0;
				if (!Value.IsValid() || !Value->TryGetObject(Object)
					|| !ReadString(*Object, TEXT("animationId"), Animation.AnimationId, OutError)
					|| !ReadString(*Object, TEXT("displayName"), Animation.DisplayName, OutError)
					|| (*Object)->HasField(TEXT("sequencePath"))
					|| !(*Object)->TryGetNumberField(TEXT("frameRate"), Animation.FrameRate)
					|| !(*Object)->TryGetNumberField(TEXT("startFrame"), StartFrame)
					|| !(*Object)->TryGetNumberField(TEXT("endFrame"), EndFrame)
					|| !ReadString(*Object, TEXT("loopMode"), Animation.LoopMode, OutError)
					|| !ReadString(*Object, TEXT("closeMode"), Animation.CloseMode, OutError)
					|| !FMath::IsFinite(Animation.FrameRate)
					|| Animation.FrameRate <= 0.0
					|| FMath::FloorToDouble(StartFrame) != StartFrame
					|| FMath::FloorToDouble(EndFrame) != EndFrame
					|| StartFrame < 0.0 || EndFrame <= StartFrame
					|| EndFrame > static_cast<double>(MAX_int32))
				{
					SetError(OutError, TEXT("INVALID_CATALOG"),
						TEXT("animation 字段非法"));
					return false;
				}
				Animation.StartFrame = static_cast<int32>(StartFrame);
				Animation.EndFrame = static_cast<int32>(EndFrame);
				Out.Animations.Add(MoveTemp(Animation));
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
					|| !ReadString(*Object, TEXT("displayName"), Category.DisplayName, OutError)
					|| !ReadUiAnimationId(*Object, Category.AnimationId, OutError))
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
					|| !ReadString(*Object, TEXT("displayName"), Component.DisplayName, OutError)
					|| !ReadUiAnimationId(*Object, Component.AnimationId, OutError))
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
					|| !ReadBool(*Object, TEXT("required"), Surface.bRequired, OutError)
					|| !ReadUiAnimationId(*Object, Surface.AnimationId, OutError))
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
					|| !ReadNullableString(*Object, TEXT("colorCode"), Variant.ColorCode, OutError)
					|| !ReadString(*Object, TEXT("thumbnailUrl"), Variant.ThumbnailUrl, OutError))
				{
					return false;
				}
				const TSharedPtr<FJsonObject>* Ui = nullptr;
				if ((*Object)->TryGetObjectField(TEXT("ui"), Ui)
					&& Ui != nullptr
					&& !ReadNullableString(
						*Ui,
						TEXT("sortColorHex"),
						Variant.DisplayColorHex,
						OutError))
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
				const TSharedPtr<FJsonObject>* Parameters = nullptr;
				if (!(*Object)->TryGetObjectField(TEXT("parameters"), Parameters))
				{
					SetError(OutError, TEXT("INVALID_CATALOG"),
						FString::Printf(TEXT("%s 缺少 parameters"), *Option.OptionId));
					return false;
				}
				if (!IsNullField(*Parameters, TEXT("color")))
				{
					const TSharedPtr<FJsonObject>* Color = nullptr;
					FString ColorMode;
					if (!(*Parameters)->TryGetObjectField(TEXT("color"), Color)
						|| !ReadString(*Color, TEXT("mode"), ColorMode, OutError))
					{
						return false;
					}
					Option.ColorMode = MoveTemp(ColorMode);
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
		TMap<FString, int32> CandidateComponents;
		TMap<FString, int32> CandidateSurfaces;
		TMap<FString, int32> CandidateFamilies;
		TMap<FString, int32> CandidateAnimations;
		TMap<FString, TArray<FString>> CandidateCategoriesByRegion;
		TMap<FString, TArray<FString>> CandidateComponentsByCategory;
		TMap<FString, TArray<FString>> CandidateSurfacesByComponent;
		TMap<FString, TArray<FString>> CandidateSurfacesByCategory;
		TMap<FString, TArray<FString>> CandidateVariantsByFamily;
		TMap<FString, FString> CandidateDefaultsBySurface;
		TMap<FString, TArray<FName>> CandidateMaterialSlotsBySurface;
		TSet<FString> CandidateUnsupportedSurfaceBindings;
		TSet<FString> SurfaceIds;
		TSet<FString> FamilyIds;

		if (!Private::IsGameObjectPath(Candidate.SkeletalMeshPath)
			|| !Private::IsGameObjectPath(Candidate.SequencePath)
			|| Candidate.Animations.IsEmpty())
		{
			Private::SetError(
				OutError,
				TEXT("INVALID_ANIMATION_ASSET_PATH"),
				Candidate.SkeletalMeshPath + TEXT("|") + Candidate.SequencePath);
			return false;
		}
		for (int32 Index = 0; Index < Candidate.Animations.Num(); ++Index)
		{
			const FAnimation& Animation = Candidate.Animations[Index];
			if (Animation.AnimationId.IsEmpty()
				|| CandidateAnimations.Contains(Animation.AnimationId)
				|| Animation.DisplayName.IsEmpty()
				|| !FMath::IsFinite(Animation.FrameRate)
				|| Animation.FrameRate <= 0.0
				|| Animation.StartFrame < 0
				|| Animation.EndFrame <= Animation.StartFrame
				|| !(Animation.LoopMode == TEXT("none")
					|| Animation.LoopMode == TEXT("forward")
					|| Animation.LoopMode == TEXT("ping-pong"))
				|| !(Animation.CloseMode == TEXT("reverse")
					|| Animation.CloseMode == TEXT("reset-to-start")
					|| Animation.CloseMode == TEXT("stop")))
			{
				Private::SetError(OutError, TEXT("INVALID_ANIMATION"), Animation.AnimationId);
				return false;
			}
			CandidateAnimations.Add(Animation.AnimationId, Index);
		}

		if (Candidate.SchemaVersion != AutomotiveCatalog::SchemaVersion
			|| Candidate.Lifecycle != TEXT("draft")
			|| Candidate.Currency != TEXT("CNY")
			|| Candidate.CatalogVersion.IsEmpty()
			|| Candidate.VehicleId.IsEmpty()
			|| Candidate.VehicleDisplayName.IsEmpty()
			|| Candidate.Regions.IsEmpty()
			|| Candidate.Categories.IsEmpty()
			|| Candidate.Components.IsEmpty()
			|| Candidate.BasePriceMinor != 22980000
			|| Candidate.PriceStatus != TEXT("confirmed")
			|| Candidate.bQuotable
			|| Candidate.SelectionOrder.Num() != RequiredSelectionCount
			|| Candidate.Surfaces.Num() != RequiredSelectionCount)
		{
			Private::SetError(OutError, TEXT("INVALID_CATALOG"),
				TEXT("车型目录 v2 必须包含确认基础价、有序表面且保持 2.0.0 draft 不可报价"));
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
			if (Category.AnimationId.IsSet()
				&& !CandidateAnimations.Contains(Category.AnimationId.GetValue()))
			{
				Private::SetError(OutError, TEXT("UNKNOWN_ANIMATION"), Category.CategoryId);
				return false;
			}
			CandidateCategoriesByRegion.FindChecked(Category.RegionId).Add(Category.CategoryId);
			CandidateComponentsByCategory.Add(Category.CategoryId);
			CandidateSurfacesByCategory.Add(Category.CategoryId);
		}
		TMap<FString, FString> CategoryByComponent;
		for (int32 Index = 0; Index < Candidate.Components.Num(); ++Index)
		{
			const FComponent& Component = Candidate.Components[Index];
			if (Component.ComponentId.IsEmpty() || Component.DisplayName.IsEmpty()
				|| CategoryByComponent.Contains(Component.ComponentId)
				|| !CandidateCategories.Contains(Component.CategoryId))
			{
				Private::SetError(OutError, TEXT("INVALID_COMPONENT"), Component.ComponentId);
				return false;
			}
			CategoryByComponent.Add(Component.ComponentId, Component.CategoryId);
			if (Component.AnimationId.IsSet()
				&& !CandidateAnimations.Contains(Component.AnimationId.GetValue()))
			{
				Private::SetError(OutError, TEXT("UNKNOWN_ANIMATION"), Component.ComponentId);
				return false;
			}
			CandidateComponents.Add(Component.ComponentId, Index);
			CandidateComponentsByCategory.FindChecked(Component.CategoryId).Add(
				Component.ComponentId);
			CandidateSurfacesByComponent.Add(Component.ComponentId);
		}

		for (int32 Index = 0; Index < Candidate.Surfaces.Num(); ++Index)
		{
			const FSurface& Surface = Candidate.Surfaces[Index];
			const FString* CategoryId = CategoryByComponent.Find(Surface.ComponentId);
			if (Surface.SurfaceId.IsEmpty()
				|| SurfaceIds.Contains(Surface.SurfaceId)
				|| Surface.DisplayName.IsEmpty()
				|| CategoryId == nullptr)
			{
				Private::SetError(OutError, TEXT("INVALID_CATALOG"),
					TEXT("surfaces 必须包含 40 个唯一且组件有效的表面"));
				return false;
			}
			SurfaceIds.Add(Surface.SurfaceId);
			if (Surface.AnimationId.IsSet()
				&& !CandidateAnimations.Contains(Surface.AnimationId.GetValue()))
			{
				Private::SetError(OutError, TEXT("UNKNOWN_ANIMATION"), Surface.SurfaceId);
				return false;
			}
			CandidateSurfaces.Add(Surface.SurfaceId, Index);
			CandidateOptionsBySurface.Add(Surface.SurfaceId);
		}
		TSet<FString> OrderedSurfaceIds;
		for (const FString& SurfaceId : Candidate.SelectionOrder)
		{
			const int32* SurfaceIndex = CandidateSurfaces.Find(SurfaceId);
			if (SurfaceIndex == nullptr || OrderedSurfaceIds.Contains(SurfaceId))
			{
				Private::SetError(OutError, TEXT("INVALID_CATALOG"),
					TEXT("selectionOrder 必须覆盖 40 个唯一 surface"));
				return false;
			}
			OrderedSurfaceIds.Add(SurfaceId);
			const FSurface& Surface = Candidate.Surfaces[*SurfaceIndex];
			const FString& CategoryId = CategoryByComponent.FindChecked(Surface.ComponentId);
			CandidateSurfacesByComponent.FindChecked(Surface.ComponentId).Add(SurfaceId);
			CandidateSurfacesByCategory.FindChecked(CategoryId).Add(SurfaceId);
		}

		const FVehicleSurfaceBinding& SurfaceBinding = Candidate.VehicleSurfaceBinding;
		if (SurfaceBinding.SchemaVersion != TEXT("1.0.0")
			|| !(SurfaceBinding.Capability == TEXT("complete")
				|| SurfaceBinding.Capability == TEXT("proxy"))
			|| SurfaceBinding.Bindings.IsEmpty())
		{
			Private::SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
				TEXT("surface binding 版本、capability 或 bindings 非法"));
			return false;
		}
		TSet<FName> BoundSlots;
		for (const FSurfaceBinding& Binding : SurfaceBinding.Bindings)
		{
			if (!SurfaceIds.Contains(Binding.SurfaceId)
				|| CandidateMaterialSlotsBySurface.Contains(Binding.SurfaceId)
				|| Binding.MaterialSlotIds.IsEmpty())
			{
				Private::SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
					Binding.SurfaceId);
				return false;
			}
			TSet<FName> LocalSlots;
			for (const FName SlotId : Binding.MaterialSlotIds)
			{
				if (SlotId.IsNone() || LocalSlots.Contains(SlotId)
					|| BoundSlots.Contains(SlotId))
				{
					Private::SetError(OutError, TEXT("SURFACE_SLOT_COLLISION"),
						SlotId.ToString());
					return false;
				}
				LocalSlots.Add(SlotId);
				BoundSlots.Add(SlotId);
			}
			CandidateMaterialSlotsBySurface.Add(
				Binding.SurfaceId, Binding.MaterialSlotIds);
		}
		for (const FString& SurfaceId : SurfaceBinding.UnsupportedSurfaceIds)
		{
			if (!SurfaceIds.Contains(SurfaceId)
				|| CandidateMaterialSlotsBySurface.Contains(SurfaceId)
				|| CandidateUnsupportedSurfaceBindings.Contains(SurfaceId))
			{
				Private::SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
					SurfaceId);
				return false;
			}
			CandidateUnsupportedSurfaceBindings.Add(SurfaceId);
		}
		for (const FString& SurfaceId : Candidate.SelectionOrder)
		{
			if (!CandidateMaterialSlotsBySurface.Contains(SurfaceId)
				&& !CandidateUnsupportedSurfaceBindings.Contains(SurfaceId))
			{
				Private::SetError(OutError, TEXT("INCOMPLETE_SURFACE_BINDING"),
					SurfaceId);
				return false;
			}
		}
		if ((SurfaceBinding.Capability == TEXT("complete")
				&& !CandidateUnsupportedSurfaceBindings.IsEmpty())
			|| (SurfaceBinding.Capability == TEXT("proxy")
				&& CandidateUnsupportedSurfaceBindings.IsEmpty())
			|| CandidateMaterialSlotsBySurface.Num()
				+ CandidateUnsupportedSurfaceBindings.Num() != RequiredSelectionCount)
		{
			Private::SetError(OutError, TEXT("INVALID_SURFACE_BINDING_CAPABILITY"),
				SurfaceBinding.Capability);
			return false;
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
			if (Variant.VariantId.IsEmpty()
				|| Variant.ThumbnailUrl.IsEmpty()
				|| !Variant.ThumbnailUrl.StartsWith(TEXT("/"))
				|| CandidateVariants.Contains(Variant.VariantId))
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
				|| (Option.Pricing.Quantity.IsSet() && Option.Pricing.Quantity.GetValue() < 1)
				|| (Option.ColorMode.IsSet()
					&& Option.ColorMode.GetValue() == TEXT("variant")
					&& (!Option.MaterialFamilyId.IsSet()
						|| !Option.Pricing.UnitPriceMinor.IsSet())))
			{
				Private::SetError(OutError, TEXT("INVALID_PRICING"), Option.OptionId);
				return false;
			}
			CandidateOptions.Add(Option.OptionId, Index);
			CandidateOptionsBySurface.FindChecked(Option.SurfaceId).Add(Option.OptionId);
		}
		for (const TPair<FString, FString>& Pair : Candidate.OptionIdAliases)
		{
			if (Pair.Key.IsEmpty()
				|| CandidateOptions.Contains(Pair.Key)
				|| !CandidateOptions.Contains(Pair.Value))
			{
				Private::SetError(OutError, TEXT("INVALID_OPTION_ID_ALIAS"), Pair.Key);
				return false;
			}
		}
		for (const FString& SurfaceId : Candidate.SelectionOrder)
		{
			if (CandidateOptionsBySurface.FindChecked(SurfaceId).IsEmpty())
			{
				Private::SetError(OutError, TEXT("SURFACE_WITHOUT_OPTIONS"), SurfaceId);
				return false;
			}
			const FSurface& Surface = Candidate.Surfaces[
				CandidateSurfaces.FindChecked(SurfaceId)];
			const FString* DefaultOptionId = Candidate.DefaultSelections.Find(SurfaceId);
			if (Surface.bRequired != (DefaultOptionId != nullptr))
			{
				Private::SetError(OutError, TEXT("REQUIRED_SURFACE_WITHOUT_DEFAULT"), SurfaceId);
				return false;
			}
			if (DefaultOptionId != nullptr)
			{
				const int32* DefaultOptionIndex = CandidateOptions.Find(*DefaultOptionId);
				const FOption* DefaultOption = DefaultOptionIndex != nullptr
					? &Candidate.Options[*DefaultOptionIndex]
					: nullptr;
				if (DefaultOption == nullptr
					|| DefaultOption->SurfaceId != SurfaceId
					|| !DefaultOption->Pricing.bIsStandard)
				{
					Private::SetError(
						OutError,
						TEXT("INVALID_DEFAULT_SELECTION"),
						SurfaceId);
					return false;
				}
				CandidateDefaultsBySurface.Add(SurfaceId, *DefaultOptionId);
			}
		}
		for (const TPair<FString, FString>& Pair : Candidate.DefaultSelections)
		{
			if (!SurfaceIds.Contains(Pair.Key))
			{
				Private::SetError(
					OutError,
					TEXT("INVALID_DEFAULT_SELECTION"),
					Pair.Key);
				return false;
			}
		}

		Catalog = MoveTemp(Candidate);
		OptionIndexById = MoveTemp(CandidateOptions);
		OptionIdAliases = Catalog.OptionIdAliases;
		VariantIndexById = MoveTemp(CandidateVariants);
		OptionIdsBySurface = MoveTemp(CandidateOptionsBySurface);
		RegionIndexById = MoveTemp(CandidateRegions);
		CategoryIndexById = MoveTemp(CandidateCategories);
		ComponentIndexById = MoveTemp(CandidateComponents);
		SurfaceIndexById = MoveTemp(CandidateSurfaces);
		FamilyIndexById = MoveTemp(CandidateFamilies);
		AnimationIndexById = MoveTemp(CandidateAnimations);
		CategoryIdsByRegion = MoveTemp(CandidateCategoriesByRegion);
		ComponentIdsByCategory = MoveTemp(CandidateComponentsByCategory);
		SurfaceIdsByComponent = MoveTemp(CandidateSurfacesByComponent);
		SurfaceIdsByCategory = MoveTemp(CandidateSurfacesByCategory);
		VariantIdsByFamily = MoveTemp(CandidateVariantsByFamily);
		DefaultOptionIdBySurface = MoveTemp(CandidateDefaultsBySurface);
		MaterialSlotIdsBySurface = MoveTemp(CandidateMaterialSlotsBySurface);
		UnsupportedSurfaceBindingIds = MoveTemp(CandidateUnsupportedSurfaceBindings);
		bValid = true;
		return true;
	}

	const FOption* FCatalogIndex::FindOption(const FString& OptionId) const
	{
		const FString Resolved = ResolveOptionId(OptionId);
		const int32* Index = OptionIndexById.Find(Resolved);
		return bValid && Index != nullptr ? &Catalog.Options[*Index] : nullptr;
	}

	FString FCatalogIndex::ResolveOptionId(const FString& OptionId) const
	{
		const FString* Resolved = OptionIdAliases.Find(OptionId);
		return Resolved != nullptr ? *Resolved : OptionId;
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

	const TArray<FString>* FCatalogIndex::FindComponentIdsForCategory(
		const FString& CategoryId) const
	{
		return bValid ? ComponentIdsByCategory.Find(CategoryId) : nullptr;
	}

	const TArray<FString>* FCatalogIndex::FindSurfaceIdsForComponent(
		const FString& ComponentId) const
	{
		return bValid ? SurfaceIdsByComponent.Find(ComponentId) : nullptr;
	}

	const TArray<FString>* FCatalogIndex::FindSurfaceIdsForCategory(const FString& CategoryId) const
	{
		return bValid ? SurfaceIdsByCategory.Find(CategoryId) : nullptr;
	}

	const TArray<FString>* FCatalogIndex::FindVariantIdsForMaterialFamily(const FString& MaterialFamilyId) const
	{
		return bValid ? VariantIdsByFamily.Find(MaterialFamilyId) : nullptr;
	}

	const FString* FCatalogIndex::FindDefaultOptionIdForSurface(
		const FString& SurfaceId) const
	{
		return bValid ? DefaultOptionIdBySurface.Find(SurfaceId) : nullptr;
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

	const FComponent* FCatalogIndex::FindComponent(const FString& ComponentId) const
	{
		const int32* Index = ComponentIndexById.Find(ComponentId);
		return bValid && Index != nullptr ? &Catalog.Components[*Index] : nullptr;
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

	const FAnimation* FCatalogIndex::FindAnimation(const FString& AnimationId) const
	{
		const int32* Index = AnimationIndexById.Find(AnimationId);
		return bValid && Index != nullptr ? &Catalog.Animations[*Index] : nullptr;
	}

	const TArray<FName>* FCatalogIndex::FindMaterialSlotIdsForSurface(
		const FString& SurfaceId) const
	{
		return bValid ? MaterialSlotIdsBySurface.Find(SurfaceId) : nullptr;
	}

	bool FCatalogIndex::IsSurfaceBindingExplicitlyUnsupported(
		const FString& SurfaceId) const
	{
		return bValid && UnsupportedSurfaceBindingIds.Contains(SurfaceId);
	}

	bool FCatalogIndex::IsSurfaceBindingCovered(const FString& SurfaceId) const
	{
		return FindMaterialSlotIdsForSurface(SurfaceId) != nullptr
			|| IsSurfaceBindingExplicitlyUnsupported(SurfaceId);
	}

	bool FCatalogIndex::ResolveSurfaceBindingTransaction(
		const TSet<FString>& SurfaceIds,
		TMap<FString, TArray<FName>>& OutTargets,
		TSet<FString>& OutUnsupportedSurfaceIds,
		FError& OutError) const
	{
		OutTargets.Reset();
		OutUnsupportedSurfaceIds.Reset();
		OutError.Reset();
		if (!bValid)
		{
			Private::SetError(OutError, TEXT("INVALID_SURFACE_BINDING"),
				TEXT("catalog 未初始化"));
			return false;
		}
		for (const FString& SurfaceId : SurfaceIds)
		{
			if (const TArray<FName>* Slots =
				FindMaterialSlotIdsForSurface(SurfaceId))
			{
				OutTargets.Add(SurfaceId, *Slots);
			}
			else if (IsSurfaceBindingExplicitlyUnsupported(SurfaceId))
			{
				OutUnsupportedSurfaceIds.Add(SurfaceId);
			}
			else
			{
				Private::SetError(OutError, TEXT("UNBOUND_TRANSACTION_SURFACE"),
					SurfaceId);
				OutTargets.Reset();
				OutUnsupportedSurfaceIds.Reset();
				return false;
			}
		}
		return true;
	}

	int64 CalculateOptionsPriceMinor(
		const FSelections& Selections,
		const FCatalogIndex& Catalog)
	{
		int64 Result = 0;
		for (const TPair<FString, FString>& Pair : Selections)
		{
			const FOption* Option = Catalog.FindOption(Pair.Value);
			if (Option == nullptr
				|| Option->SurfaceId != Pair.Key
				|| !Option->Pricing.bConfirmed
				|| !Option->Pricing.UnitPriceMinor.IsSet())
			{
				continue;
			}
			const int64 Quantity = Option->Pricing.Quantity.Get(1);
			Result += Option->Pricing.UnitPriceMinor.GetValue() * Quantity;
		}
		return Result;
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
		if (!Catalog.IsValid())
		{
			Private::SetError(OutError, TEXT("INVALID_SELECTIONS"),
				TEXT("catalog 未初始化"));
			return false;
		}
		for (const TPair<FString, FString>& Pair : Selections)
		{
			if (Catalog.FindSurface(Pair.Key) == nullptr)
			{
				Private::SetError(OutError, TEXT("INVALID_SELECTIONS"),
					TEXT("selections 包含未知表面"));
				return false;
			}
		}
		for (const FString& SurfaceId : Catalog.GetCatalog().SelectionOrder)
		{
			const FString* OptionId = Selections.Find(SurfaceId);
			const FSurface* Surface = Catalog.FindSurface(SurfaceId);
			if (OptionId == nullptr && Surface != nullptr && !Surface->bRequired)
			{
				continue;
			}
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
				if (!Option->SupportsMaterialVariants())
				{
					Private::SetError(
						OutError,
						TEXT("MATERIAL_VARIANT_NOT_SUPPORTED"),
						FString::Printf(TEXT("%s 所选 option 不支持材料色卡"), *Pair.Key));
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
			else if (!Option->SupportsCustomColor() || !Private::IsPaintValid(Pair.Value.Paint))
			{
				Private::SetError(OutError, TEXT("INVALID_PAINT_CUSTOMIZATION"),
					FString::Printf(TEXT("%s 所选 option 不支持自定义色，或自定义色参数非法"), *Pair.Key));
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
		FSelections NormalizedSelections;
		for (const TPair<FString, FString>& Pair : Selections)
		{
			NormalizedSelections.Add(Pair.Key, Catalog.ResolveOptionId(Pair.Value));
		}
		if (!ValidateSelections(NormalizedSelections, Catalog, OutError)
			|| !ValidateCustomizations(Customizations, NormalizedSelections, Catalog, OutError))
		{
			return false;
		}
		const FCatalog& Data = Catalog.GetCatalog();
		TArray<FString> CanonicalLines = {
			FString::Printf(TEXT("schemaVersion=%s"), AutomotiveCatalog::SchemaVersion),
			FString::Printf(TEXT("catalogVersion=%s"), *Data.CatalogVersion),
			FString::Printf(TEXT("vehicleId=%s"), *Data.VehicleId)
		};
		TArray<FString> RenderLines = CanonicalLines;
		TSet<FString> RenderRelevant;
		for (const FString& SurfaceId : Data.SelectionOrder)
		{
			const FString* OptionId = NormalizedSelections.Find(SurfaceId);
			if (OptionId == nullptr)
			{
				continue;
			}
			const FString Line = SurfaceId + TEXT("=") + *OptionId;
			CanonicalLines.Add(Line);
			if (Catalog.FindOption(*OptionId)->bRenderRelevant)
			{
				RenderRelevant.Add(SurfaceId);
				RenderLines.Add(Line);
			}
		}
		Private::AppendCustomizationLines(CanonicalLines, Data, Customizations, nullptr);
		Private::AppendCustomizationLines(RenderLines, Data, Customizations, &RenderRelevant);

		FConfiguration Candidate;
		Candidate.SchemaVersion = AutomotiveCatalog::SchemaVersion;
		Candidate.CatalogVersion = Data.CatalogVersion;
		Candidate.VehicleId = Data.VehicleId;
		Candidate.ConfigurationId = TEXT("cfg-") + Private::Sha256Digest24(FString::Join(CanonicalLines, TEXT("\n")));
		Candidate.RenderKey = FString::Printf(
			TEXT("%s__%s__render-%s"),
			*Data.VehicleId,
			*Data.CatalogVersion,
			*Private::Sha256Digest24(FString::Join(RenderLines, TEXT("\n"))));
		Candidate.Selections = MoveTemp(NormalizedSelections);
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
