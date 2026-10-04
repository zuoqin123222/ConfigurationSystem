#pragma once

#include "CoreMinimal.h"

/** 车型目录 v2 独立领域层。类型名称与实现均不复用 v1 的四分区状态。 */
namespace AutomotiveCatalog
{
	inline constexpr int32 RequiredSelectionCount = 38;
	inline constexpr TCHAR SchemaVersion[] = TEXT("2.0.0");

	struct CONFIGURATIONSYSTEM_API FError
	{
		FString Code;
		FString Message;

		bool IsSet() const { return !Code.IsEmpty(); }
		void Reset()
		{
			Code.Reset();
			Message.Reset();
		}
	};

	struct CONFIGURATIONSYSTEM_API FPricing
	{
		TOptional<int64> UnitPriceMinor;
		TOptional<int64> Quantity;
		FString PricingUnit;
		bool bIsStandard = false;
		bool bConfirmed = false;
		bool bQuotable = false;
	};

	struct CONFIGURATIONSYSTEM_API FSurface
	{
		FString SurfaceId;
		FString ComponentId;
		FString DisplayName;
		bool bRequired = false;
	};

	struct CONFIGURATIONSYSTEM_API FRegion
	{
		FString RegionId;
		FString DisplayName;
	};

	struct CONFIGURATIONSYSTEM_API FCategory
	{
		FString CategoryId;
		FString RegionId;
		FString DisplayName;
	};

	struct CONFIGURATIONSYSTEM_API FComponent
	{
		FString ComponentId;
		FString CategoryId;
		FString DisplayName;
	};

	struct CONFIGURATIONSYSTEM_API FMaterialFamily
	{
		FString MaterialFamilyId;
		FString DisplayName;
	};

	struct CONFIGURATIONSYSTEM_API FOption
	{
		FString OptionId;
		FString SurfaceId;
		FString DisplayName;
		TOptional<FString> ColorCode;
		TOptional<FString> Finish;
		TOptional<FString> MaterialFamilyId;
		TOptional<FString> ColorMode;
		bool bRenderRelevant = false;
		FPricing Pricing;

		bool SupportsMaterialVariants() const
		{
			return ColorMode.IsSet()
				&& ColorMode.GetValue() == TEXT("variant")
				&& Pricing.UnitPriceMinor.IsSet();
		}

		bool SupportsCustomColor() const
		{
			return ColorMode.IsSet() && ColorMode.GetValue() == TEXT("custom");
		}
	};

	struct CONFIGURATIONSYSTEM_API FMaterialVariant
	{
		FString VariantId;
		FString MaterialFamilyId;
		FString DisplayName;
		TOptional<FString> ColorCode;
		FString ThumbnailUrl;
	};

	struct CONFIGURATIONSYSTEM_API FCatalog
	{
		FString SchemaVersion;
		FString CatalogVersion;
		FString Lifecycle;
		FString Currency;
		FString VehicleId;
		FString VehicleDisplayName;
		int64 BasePriceMinor = 0;
		FString PriceStatus;
		bool bQuotable = false;
		TArray<FString> SelectionOrder;
		TMap<FString, FString> DefaultSelections;
		TMap<FString, FString> OptionIdAliases;
		TArray<FRegion> Regions;
		TArray<FCategory> Categories;
		TArray<FComponent> Components;
		TArray<FSurface> Surfaces;
		TArray<FMaterialFamily> MaterialFamilies;
		TArray<FMaterialVariant> MaterialVariants;
		TArray<FOption> Options;
	};

	/**
	 * 经验证的只读目录索引。Initialize 原子构造全部索引，失败不会留下半有效数据。
	 */
	class CONFIGURATIONSYSTEM_API FCatalogIndex
	{
	public:
		bool Initialize(const FCatalog& InCatalog, FError& OutError);
		bool LoadJson(const FString& Json, FError& OutError);
		bool LoadJsonFile(const FString& Filename, FError& OutError);

		bool IsValid() const { return bValid; }
		const FCatalog& GetCatalog() const { return Catalog; }
		const FOption* FindOption(const FString& OptionId) const;
		FString ResolveOptionId(const FString& OptionId) const;
		const FMaterialVariant* FindMaterialVariant(const FString& VariantId) const;
		const TArray<FString>* FindOptionIdsForSurface(const FString& SurfaceId) const;
		const TArray<FString>* FindCategoryIdsForRegion(const FString& RegionId) const;
		const TArray<FString>* FindComponentIdsForCategory(const FString& CategoryId) const;
		const TArray<FString>* FindSurfaceIdsForComponent(const FString& ComponentId) const;
		const TArray<FString>* FindSurfaceIdsForCategory(const FString& CategoryId) const;
		const TArray<FString>* FindVariantIdsForMaterialFamily(const FString& MaterialFamilyId) const;
		const FString* FindDefaultOptionIdForSurface(const FString& SurfaceId) const;
		const FRegion* FindRegion(const FString& RegionId) const;
		const FCategory* FindCategory(const FString& CategoryId) const;
		const FComponent* FindComponent(const FString& ComponentId) const;
		const FSurface* FindSurface(const FString& SurfaceId) const;
		const FMaterialFamily* FindMaterialFamily(const FString& MaterialFamilyId) const;

	private:
		FCatalog Catalog;
		TMap<FString, int32> OptionIndexById;
		TMap<FString, FString> OptionIdAliases;
		TMap<FString, int32> VariantIndexById;
		TMap<FString, TArray<FString>> OptionIdsBySurface;
		TMap<FString, int32> RegionIndexById;
		TMap<FString, int32> CategoryIndexById;
		TMap<FString, int32> ComponentIndexById;
		TMap<FString, int32> SurfaceIndexById;
		TMap<FString, int32> FamilyIndexById;
		TMap<FString, TArray<FString>> CategoryIdsByRegion;
		TMap<FString, TArray<FString>> ComponentIdsByCategory;
		TMap<FString, TArray<FString>> SurfaceIdsByComponent;
		TMap<FString, TArray<FString>> SurfaceIdsByCategory;
		TMap<FString, TArray<FString>> VariantIdsByFamily;
		TMap<FString, FString> DefaultOptionIdBySurface;
		bool bValid = false;
	};

	using FSelections = TMap<FString, FString>;

	CONFIGURATIONSYSTEM_API int64 CalculateOptionsPriceMinor(
		const FSelections& Selections,
		const FCatalogIndex& Catalog);

	struct CONFIGURATIONSYSTEM_API FPaintCustomization
	{
		FString ColorHex;
		double Metallic = 0.0;
		double Roughness = 0.0;
		double ClearCoat = 0.0;
		double OrangePeel = 0.0;
		double FlakeIntensity = 0.0;
	};

	enum class ECustomizationKind : uint8
	{
		MaterialVariant,
		Paint
	};

	struct CONFIGURATIONSYSTEM_API FCustomization
	{
		ECustomizationKind Kind = ECustomizationKind::MaterialVariant;
		FString MaterialVariantId;
		FPaintCustomization Paint;

		static FCustomization ForMaterialVariant(const FString& VariantId);
		static FCustomization ForPaint(const FPaintCustomization& Value);
	};

	using FCustomizations = TMap<FString, FCustomization>;

	struct CONFIGURATIONSYSTEM_API FConfiguration
	{
		FString SchemaVersion;
		FString CatalogVersion;
		FString VehicleId;
		FString ConfigurationId;
		FString RenderKey;
		FSelections Selections;
		FCustomizations Customizations;
	};

	CONFIGURATIONSYSTEM_API bool ValidateSelections(
		const FSelections& Selections,
		const FCatalogIndex& Catalog,
		FError& OutError);

	CONFIGURATIONSYSTEM_API bool ValidateCustomizations(
		const FCustomizations& Customizations,
		const FSelections& Selections,
		const FCatalogIndex& Catalog,
		FError& OutError);

	/** 与 source/server/src/automotive-catalog-v2.ts 的规范行、UTF-8 编码及 SHA-256 截断规则一致。 */
	CONFIGURATIONSYSTEM_API bool DeriveConfiguration(
		const FSelections& Selections,
		const FCustomizations& Customizations,
		const FCatalogIndex& Catalog,
		FConfiguration& OutConfiguration,
		FError& OutError);
}
