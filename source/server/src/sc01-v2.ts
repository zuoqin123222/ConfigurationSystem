import { createHash } from "node:crypto";
import { accessSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { RequestError } from "./data.js";

export const SC01_SCHEMA_VERSION = "2.0.0";

export interface Sc01Pricing {
  unitPriceMinor: number | null;
  quantity: number | null;
  pricingUnit: string;
  isStandard: boolean;
  status: "confirmed" | "unconfirmed";
  quotable: false;
}

export interface Sc01Option {
  optionId: string;
  surfaceId: string;
  materialFamilyId: string | null;
  renderRelevant: boolean;
  parameters: {
    color: {
      mode: "fixed" | "custom" | "choice" | "variant";
      value: string | null;
      required: boolean;
    } | null;
    material: {
      materialFamilyId: string;
      variantId: string | null;
    } | null;
  };
  pricing: Sc01Pricing;
  [key: string]: unknown;
}

export interface Sc01MaterialVariant {
  variantId: string;
  materialFamilyId: string;
  [key: string]: unknown;
}

export interface Sc01Catalog {
  schemaVersion: "2.0.0";
  catalogVersion: string;
  lifecycle: "draft";
  currency: "CNY";
  vehicle: {
    vehicleId: string;
    basePriceMinor: number;
    priceStatus: "confirmed";
    quotable: false;
    [key: string]: unknown;
  };
  selectionOrder: string[];
  defaultSelections: Record<string, string>;
  optionIdAliases: Record<string, string>;
  surfaces: Array<{ surfaceId: string; required: boolean; [key: string]: unknown }>;
  materialVariants: Sc01MaterialVariant[];
  options: Sc01Option[];
  [key: string]: unknown;
}

export type Sc01Selections = Record<string, string>;
export interface Sc01MaterialCustomization {
  materialVariantId: string;
}
export interface Sc01PaintCustomization {
  colorHex: string;
  metallic: number;
  roughness: number;
  clearCoat: number;
  orangePeel: number;
  flakeIntensity: number;
}
export type Sc01Customization = Sc01MaterialCustomization | Sc01PaintCustomization;
export type Sc01Customizations = Record<string, Sc01Customization>;

export interface Sc01Configuration {
  schemaVersion: "2.0.0";
  catalogVersion: string;
  vehicleId: string;
  configurationId: string;
  renderKey: string;
  selections: Sc01Selections;
  customizations: Sc01Customizations;
}

export interface Sc01PriceResult {
  schemaVersion: "2.0.0";
  catalogVersion: string;
  vehicleId: string;
  configurationId: string;
  currency: "CNY";
  basePriceMinor: number;
  lineItems: Array<{
    surfaceId: string;
    optionId: string;
    unitPriceMinor: number | null;
    quantity: number | null;
    subtotalMinor: number | null;
    priceStatus: "confirmed" | "unconfirmed";
  }>;
  totalPriceMinor: number;
  quoteAllowed: false;
  blockingReasons: ["PRICE_UNCONFIRMED"];
}

export interface Sc01V2Data {
  catalog: Sc01Catalog;
  options: ReadonlyMap<string, Sc01Option>;
  materialVariants: ReadonlyMap<string, Sc01MaterialVariant>;
  optionIdsBySurface: ReadonlyMap<string, ReadonlySet<string>>;
}

function defaultContractRoot(): string {
  const moduleDirectory = dirname(fileURLToPath(import.meta.url));
  const candidates = [
    resolve(moduleDirectory, "../../../contracts"),
    resolve(moduleDirectory, "../../../../contracts"),
    resolve(process.cwd(), "../../contracts"),
    resolve(process.cwd(), "contracts"),
  ];
  for (const candidate of candidates) {
    try {
      accessSync(resolve(candidate, "fixtures/sc01.catalog.draft.v2.json"));
      return candidate;
    } catch {
      // 兼容源码、编译产物和仓库根目录运行。
    }
  }
  throw new Error("无法定位根目录 SC01 v2 fixture");
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function digest24(value: string): string {
  return createHash("sha256").update(value, "utf8").digest("hex").slice(0, 24);
}

export function validateSc01Selections(
  value: unknown,
  data: Sc01V2Data,
): Sc01Selections {
  if (!isRecord(value)) {
    throw new RequestError(400, "INVALID_SELECTIONS", "selections 必须是对象");
  }
  const expected = data.catalog.selectionOrder;
  const actual = Object.keys(value);
  if (!actual.every((surfaceId) => expected.includes(surfaceId))) {
    throw new RequestError(
      400,
      "INVALID_SELECTIONS",
      "selections 包含未知表面",
    );
  }

  const selections: Sc01Selections = {};
  for (const surfaceId of expected) {
    const submittedOptionId = value[surfaceId];
    const surface = data.catalog.surfaces.find((item) => item.surfaceId === surfaceId);
    if (submittedOptionId === undefined && surface?.required === false) continue;
    const optionId = typeof submittedOptionId === "string"
      ? data.catalog.optionIdAliases[submittedOptionId] ?? submittedOptionId
      : submittedOptionId;
    if (
      typeof optionId !== "string" ||
      !data.optionIdsBySurface.get(surfaceId)?.has(optionId)
    ) {
      throw new RequestError(
        400,
        "INVALID_OPTION",
        `${surfaceId} 包含未知或跨表面的 optionId`,
      );
    }
    selections[surfaceId] = optionId;
  }
  return selections;
}

const PAINT_KEYS = [
  "colorHex",
  "metallic",
  "roughness",
  "clearCoat",
  "orangePeel",
  "flakeIntensity",
] as const;

function isUnitInterval(value: unknown): value is number {
  return typeof value === "number"
    && Number.isFinite(value)
    && value >= 0
    && value <= 1;
}

export function validateSc01Customizations(
  value: unknown,
  selections: Sc01Selections,
  data: Sc01V2Data,
): Sc01Customizations {
  if (value === undefined) return {};
  if (!isRecord(value)) {
    throw new RequestError(400, "INVALID_CUSTOMIZATIONS", "customizations 必须是对象");
  }

  const result: Sc01Customizations = {};
  for (const surfaceId of data.catalog.selectionOrder) {
    if (!Object.hasOwn(value, surfaceId)) continue;
    const customization = value[surfaceId];
    if (!isRecord(customization)) {
      throw new RequestError(
        400,
        "INVALID_CUSTOMIZATION",
        `${surfaceId} customization 必须是对象`,
      );
    }
    const option = data.options.get(selections[surfaceId]!);
    if (!option) {
      throw new RequestError(
        400,
        "INVALID_CUSTOMIZATION",
        `${surfaceId} 未选择 option，不能提交 customization`,
      );
    }

    if (Object.hasOwn(customization, "materialVariantId")) {
      if (
        Object.keys(customization).length !== 1
        || typeof customization.materialVariantId !== "string"
      ) {
        throw new RequestError(
          400,
          "INVALID_MATERIAL_VARIANT",
          `${surfaceId} 材料定制仅允许 materialVariantId`,
        );
      }
      const variant = data.materialVariants.get(customization.materialVariantId);
      if (!variant) {
        throw new RequestError(
          400,
          "INVALID_MATERIAL_VARIANT",
          `${surfaceId} 引用了未知 materialVariantId`,
        );
      }
      if (
        option.parameters.color?.mode !== "variant"
        || option.pricing.unitPriceMinor === null
      ) {
        throw new RequestError(
          400,
          "MATERIAL_VARIANT_NOT_SUPPORTED",
          `${surfaceId} 所选 option 不支持材料色卡`,
        );
      }
      if (
        option.materialFamilyId === null
        || variant.materialFamilyId !== option.materialFamilyId
      ) {
        throw new RequestError(
          400,
          "MATERIAL_VARIANT_FAMILY_MISMATCH",
          `${surfaceId} 的材料色卡与所选选项材料族不匹配`,
        );
      }
      result[surfaceId] = { materialVariantId: variant.variantId };
      continue;
    }

    if (
      option.parameters.color?.mode !== "custom"
      || Object.keys(customization).length !== PAINT_KEYS.length
      || !PAINT_KEYS.every((key) => Object.hasOwn(customization, key))
      || typeof customization.colorHex !== "string"
      || !/^#[0-9a-fA-F]{6}$/.test(customization.colorHex)
      || !PAINT_KEYS.slice(1).every((key) => isUnitInterval(customization[key]))
    ) {
      throw new RequestError(
        400,
        "INVALID_PAINT_CUSTOMIZATION",
        `${surfaceId} 所选 option 不支持自定义色，或自定义色参数非法`,
      );
    }
    result[surfaceId] = {
      colorHex: customization.colorHex.toUpperCase(),
      metallic: customization.metallic as number,
      roughness: customization.roughness as number,
      clearCoat: customization.clearCoat as number,
      orangePeel: customization.orangePeel as number,
      flakeIntensity: customization.flakeIntensity as number,
    };
  }

  if (Object.keys(value).some((surfaceId) => !data.catalog.selectionOrder.includes(surfaceId))) {
    throw new RequestError(
      400,
      "INVALID_CUSTOMIZATIONS",
      "customizations 包含未知 surfaceId",
    );
  }
  return result;
}

function customizationLines(
  catalog: Sc01Catalog,
  customizations: Sc01Customizations,
  renderRelevant?: ReadonlySet<string>,
): string[] {
  return catalog.selectionOrder.flatMap((surfaceId) => {
    if (renderRelevant && !renderRelevant.has(surfaceId)) return [];
    const customization = customizations[surfaceId];
    if (!customization) return [];
    if ("materialVariantId" in customization) {
      return [`customizations.${surfaceId}.materialVariantId=${customization.materialVariantId}`];
    }
    return PAINT_KEYS.map(
      (key) => `customizations.${surfaceId}.${key}=${customization[key]}`,
    );
  });
}

export function deriveSc01Configuration(
  value: unknown,
  data: Sc01V2Data,
  customizationValue?: unknown,
): Sc01Configuration {
  const selections = validateSc01Selections(value, data);
  const customizations = validateSc01Customizations(
    customizationValue,
    selections,
    data,
  );
  const catalog = data.catalog;
  const header = [
    `schemaVersion=${SC01_SCHEMA_VERSION}`,
    `catalogVersion=${catalog.catalogVersion}`,
    `vehicleId=${catalog.vehicle.vehicleId}`,
  ];
  const canonicalInput = [
    ...header,
    ...catalog.selectionOrder.flatMap((surfaceId) =>
      selections[surfaceId] ? [`${surfaceId}=${selections[surfaceId]}`] : [],
    ),
    ...customizationLines(catalog, customizations),
  ].join("\n");
  const renderRelevant = new Set<string>();
  const canonicalRenderInput = [
    ...header,
    ...catalog.selectionOrder.flatMap((surfaceId) => {
      const optionId = selections[surfaceId]!;
      if (data.options.get(optionId)?.renderRelevant !== true) return [];
      renderRelevant.add(surfaceId);
      return [`${surfaceId}=${optionId}`];
    }),
    ...customizationLines(catalog, customizations, renderRelevant),
  ].join("\n");

  return {
    schemaVersion: SC01_SCHEMA_VERSION,
    catalogVersion: catalog.catalogVersion,
    vehicleId: catalog.vehicle.vehicleId,
    configurationId: `cfg-${digest24(canonicalInput)}`,
    renderKey:
      `${catalog.vehicle.vehicleId}__${catalog.catalogVersion}__render-${digest24(canonicalRenderInput)}`,
    selections,
    customizations,
  };
}

export function buildSc01PriceResult(
  configuration: Sc01Configuration,
  data: Sc01V2Data,
): Sc01PriceResult {
  return {
    schemaVersion: SC01_SCHEMA_VERSION,
    catalogVersion: data.catalog.catalogVersion,
    vehicleId: data.catalog.vehicle.vehicleId,
    configurationId: configuration.configurationId,
    currency: data.catalog.currency,
    basePriceMinor: data.catalog.vehicle.basePriceMinor,
    lineItems: data.catalog.surfaces.flatMap(({ surfaceId }) => {
      const optionId = configuration.selections[surfaceId];
      if (!optionId) return [];
      const option = data.options.get(optionId);
      if (!option) {
        throw new RequestError(
          500,
          "CONTRACT_INCONSISTENT",
          `catalog 缺少选项 ${optionId}`,
        );
      }
      return [{
        surfaceId,
        optionId,
        unitPriceMinor: option.pricing.unitPriceMinor,
        quantity: option.pricing.quantity,
        subtotalMinor:
          option.pricing.unitPriceMinor === null
            ? null
            : option.pricing.unitPriceMinor * (option.pricing.quantity ?? 1),
        priceStatus: option.pricing.status,
      }];
    }),
    totalPriceMinor:
      data.catalog.vehicle.basePriceMinor
      + data.catalog.selectionOrder.reduce((total, surfaceId) => {
        const optionId = configuration.selections[surfaceId];
        const option = optionId ? data.options.get(optionId) : undefined;
        return total + (
          option?.pricing.unitPriceMinor === null || option?.pricing.unitPriceMinor === undefined
            ? 0
            : option.pricing.unitPriceMinor * (option.pricing.quantity ?? 1)
        );
      }, 0),
    quoteAllowed: false,
    blockingReasons: ["PRICE_UNCONFIRMED"],
  };
}

export function assertSc01Version(
  catalogVersion: unknown,
  vehicleId: unknown,
  data: Sc01V2Data,
): void {
  if (
    typeof catalogVersion !== "string" ||
    typeof vehicleId !== "string"
  ) {
    throw new RequestError(400, "INVALID_REQUEST", "版本或车型字段类型非法");
  }
  if (catalogVersion !== data.catalog.catalogVersion) {
    throw new RequestError(
      409,
      "VERSION_CONFLICT",
      "客户端 catalogVersion 与当前 SC01 v2 catalog 冲突",
    );
  }
  if (vehicleId !== data.catalog.vehicle.vehicleId) {
    throw new RequestError(404, "VEHICLE_NOT_FOUND", "车型不存在");
  }
}

export function loadSc01V2(contractRoot = defaultContractRoot()): Sc01V2Data {
  const path = resolve(
    contractRoot,
    "fixtures/sc01.catalog.draft.v2.json",
  );
  const catalog = JSON.parse(readFileSync(path, "utf8")) as Sc01Catalog;
  if (
    catalog.schemaVersion !== SC01_SCHEMA_VERSION ||
    catalog.lifecycle !== "draft" ||
    catalog.vehicle.basePriceMinor !== 22_980_000 ||
    catalog.vehicle.priceStatus !== "confirmed" ||
    catalog.vehicle.quotable !== false ||
    catalog.selectionOrder.length !== catalog.surfaces.length ||
    !isRecord(catalog.defaultSelections) ||
    !isRecord(catalog.optionIdAliases) ||
    catalog.selectionOrder.some((surfaceId) =>
      !catalog.surfaces.some((surface) => surface.surfaceId === surfaceId)
    ) ||
    catalog.surfaces.some((surface) => {
      const hasStandard = catalog.options.some(
        (option) => option.surfaceId === surface.surfaceId && option.pricing.isStandard,
      );
      const defaultOptionId = catalog.defaultSelections[surface.surfaceId];
      const defaultOption = catalog.options.find(
        (option) => option.optionId === defaultOptionId,
      );
      return surface.required !== hasStandard
        || surface.required !== Object.hasOwn(catalog.defaultSelections, surface.surfaceId)
        || (surface.required
          && (
            defaultOption?.surfaceId !== surface.surfaceId
            || defaultOption.pricing.isStandard !== true
          ));
    })
    || Object.keys(catalog.defaultSelections).some((surfaceId) =>
      !catalog.surfaces.some((surface) => surface.surfaceId === surfaceId)
    )
  ) {
    throw new Error(
      "SC01 v2 catalog 必须包含确认基础价、显式标配和完整顺序的 2.0.0 draft",
    );
  }

  const options = new Map<string, Sc01Option>();
  const materialVariants = new Map<string, Sc01MaterialVariant>();
  const optionIdsBySurface = new Map<string, Set<string>>();
  for (const surfaceId of catalog.selectionOrder) {
    optionIdsBySurface.set(surfaceId, new Set());
  }
  for (const option of catalog.options) {
    if (options.has(option.optionId)) {
      throw new Error(`SC01 v2 optionId 重复：${option.optionId}`);
    }
    if (
      (
        option.pricing.unitPriceMinor === null
          ? option.pricing.status !== "unconfirmed"
          : !Number.isInteger(option.pricing.unitPriceMinor)
            || option.pricing.unitPriceMinor < 0
            || option.pricing.status !== "confirmed"
      ) ||
      option.pricing.quotable !== false ||
      (
        option.parameters.color?.mode === "variant"
        && (
          option.materialFamilyId === null
          || option.pricing.unitPriceMinor === null
        )
      )
    ) {
      throw new Error(`SC01 v2 选项 ${option.optionId} 价格状态非法`);
    }
    options.set(option.optionId, option);
    optionIdsBySurface.get(option.surfaceId)?.add(option.optionId);
  }
  for (const [legacyOptionId, optionId] of Object.entries(catalog.optionIdAliases)) {
    if (
      options.has(legacyOptionId)
      || typeof optionId !== "string"
      || !options.has(optionId)
    ) {
      throw new Error(
        `SC01 v2 旧 optionId 迁移非法：${legacyOptionId} -> ${String(optionId)}`,
      );
    }
  }
  for (const variant of catalog.materialVariants) {
    if (materialVariants.has(variant.variantId)) {
      throw new Error(`SC01 v2 variantId 重复：${variant.variantId}`);
    }
    materialVariants.set(variant.variantId, variant);
  }
  for (const surfaceId of catalog.selectionOrder) {
    if (optionIdsBySurface.get(surfaceId)?.size === 0) {
      throw new Error(`SC01 v2 必选表面 ${surfaceId} 没有选项`);
    }
  }
  return { catalog, options, materialVariants, optionIdsBySurface };
}
