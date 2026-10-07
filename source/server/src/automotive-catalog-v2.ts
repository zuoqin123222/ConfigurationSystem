import { createHash } from "node:crypto";
import { accessSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { RequestError } from "./data.js";

export const CATALOG_SCHEMA_VERSION = "2.0.0";

export type CatalogCameraId = string | number;

export interface CatalogNodeUi {
  order?: number;
  iconUrl?: string | null;
  cameraId?: CatalogCameraId | null;
  navigationMode?: "tabs" | "list" | "none" | "surfaces-as-components";
  layout?: "single" | "stack" | "grid";
  animationId?: string | null;
}

export interface CatalogAnimation {
  animationId: string;
  displayName: string;
  frameRate: number;
  startFrame: number;
  endFrame: number;
  loopMode: "none" | "forward" | "ping-pong";
  closeMode: "reverse" | "reset-to-start" | "stop";
}

export interface CatalogInteractionCamera {
  cameraId: CatalogCameraId;
  legacyIndex?: 0 | 1 | 2 | 3 | 4 | 5 | null;
  zone: string;
  order: number;
  displayName: string;
  iconUrl: string;
}

export interface CatalogPricing {
  unitPriceMinor: number | null;
  quantity: number | null;
  pricingUnit: string;
  isStandard: boolean;
  status: "confirmed" | "unconfirmed";
  quotable: false;
}

export interface CatalogOption {
  optionId: string;
  surfaceId: string;
  requiresSelections?: Record<string, string>;
  availability?: {
    status: "disabled";
    reason: string;
  };
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
  pricing: CatalogPricing;
  [key: string]: unknown;
}

export interface CatalogMaterialVariant {
  variantId: string;
  materialFamilyId: string;
  ui?: {
    sortColorHex?: string;
  };
  [key: string]: unknown;
}

export interface AutomotiveCatalog {
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
  interactionCameras?: CatalogInteractionCamera[];
  skeletalMeshPath: string;
  sequencePath: string;
  vehicleSurfaceBinding: {
    schemaVersion: "1.0.0";
    capability: "complete" | "proxy";
    bindings: Array<{ surfaceId: string; materialSlotIds: string[] }>;
    unsupportedSurfaceIds: string[];
  };
  animations: CatalogAnimation[];
  categories: Array<{ categoryId: string; ui?: CatalogNodeUi; [key: string]: unknown }>;
  components: Array<{ componentId: string; ui?: CatalogNodeUi; [key: string]: unknown }>;
  surfaces: Array<{
    surfaceId: string;
    componentId: string;
    required: boolean;
    ui?: CatalogNodeUi;
    [key: string]: unknown;
  }>;
  materialFamilies: CatalogMaterialFamily[];
  materialVariants: CatalogMaterialVariant[];
  options: CatalogOption[];
  [key: string]: unknown;
}

export type CatalogSelections = Record<string, string>;
export interface MaterialCustomization {
  materialVariantId: string;
}
export interface PaintCustomization {
  colorHex: string;
  metallic: number;
  roughness: number;
  clearCoat: number;
  orangePeel: number;
  flakeIntensity: number;
}
export type CatalogCustomization = MaterialCustomization | PaintCustomization;
export type CatalogCustomizations = Record<string, CatalogCustomization>;

export interface VehicleConfiguration {
  schemaVersion: "2.0.0";
  catalogVersion: string;
  vehicleId: string;
  configurationId: string;
  renderKey: string;
  selections: CatalogSelections;
  customizations: CatalogCustomizations;
}

export interface VehiclePriceResult {
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

export interface AutomotiveCatalogData {
  catalog: AutomotiveCatalog;
  options: ReadonlyMap<string, CatalogOption>;
  materialVariants: ReadonlyMap<string, CatalogMaterialVariant>;
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
  throw new Error("无法定位根目录车型目录 v2 fixture");
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function cameraKey(cameraId: CatalogCameraId): string {
  return `${typeof cameraId}:${cameraId}`;
}

function isGameObjectPath(value: unknown): value is string {
  return typeof value === "string"
    && /^\/Game\/(?:[A-Za-z0-9_-]+\/)*[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/.test(value);
}

export interface CatalogMaterialFamily {
  materialFamilyId: string;
  ui?: {
    variantSort?: "achromatic-then-rainbow";
    defaultVariantId?: string;
  };
  [key: string]: unknown;
}

function validateCatalogUi(catalog: AutomotiveCatalog): void {
  if (catalog.interactionCameras !== undefined && !Array.isArray(catalog.interactionCameras)) {
    throw new Error("车型目录 v2 interactionCameras 必须是数组");
  }
  const cameraIds = new Set<string>();
  for (const camera of catalog.interactionCameras ?? []) {
    if (
      !isRecord(camera)
      || !(typeof camera.cameraId === "string" || Number.isInteger(camera.cameraId))
      || (camera.legacyIndex !== undefined
        && camera.legacyIndex !== null
        && (!Number.isInteger(camera.legacyIndex)
          || Number(camera.legacyIndex) < 0
          || Number(camera.legacyIndex) > 5))
      || typeof camera.zone !== "string"
      || !Number.isInteger(camera.order)
      || typeof camera.displayName !== "string"
      || typeof camera.iconUrl !== "string"
      || !camera.iconUrl.startsWith("/")
    ) {
      throw new Error("车型目录 v2 interactionCamera 字段非法");
    }
    const key = cameraKey(camera.cameraId as CatalogCameraId);
    if (cameraIds.has(key)) {
      throw new Error(`车型目录 v2 cameraId 重复：${String(camera.cameraId)}`);
    }
    cameraIds.add(key);
  }
  if (!isGameObjectPath(catalog.skeletalMeshPath)) {
    throw new Error("车型目录 v2 skeletalMeshPath 必须是 /Game/ 下的完整对象路径");
  }
  if (!isGameObjectPath(catalog.sequencePath)) {
    throw new Error("车型目录 v2 sequencePath 必须是 /Game/ 下的完整对象路径");
  }
  const surfaceIds = new Set(catalog.surfaces.map((surface) => surface.surfaceId));
  const binding = catalog.vehicleSurfaceBinding;
  if (!isRecord(binding)
    || binding.schemaVersion !== "1.0.0"
    || !["complete", "proxy"].includes(String(binding.capability))
    || !Array.isArray(binding.bindings)
    || binding.bindings.length === 0
    || !Array.isArray(binding.unsupportedSurfaceIds)) {
    throw new Error("车型目录 v2 vehicleSurfaceBinding 字段非法");
  }
  const covered = new Set<string>();
  const slots = new Set<string>();
  for (const item of binding.bindings) {
    if (!isRecord(item)
      || typeof item.surfaceId !== "string"
      || !surfaceIds.has(item.surfaceId)
      || covered.has(item.surfaceId)
      || !Array.isArray(item.materialSlotIds)
      || item.materialSlotIds.length === 0) {
      throw new Error("车型目录 v2 vehicleSurfaceBinding binding 非法");
    }
    covered.add(item.surfaceId);
    for (const slotId of item.materialSlotIds) {
      if (typeof slotId !== "string"
        || !/^[A-Za-z][A-Za-z0-9_]*$/.test(slotId)
        || slots.has(slotId)) {
        throw new Error("车型目录 v2 vehicleSurfaceBinding slot 非法或重复");
      }
      slots.add(slotId);
    }
  }
  for (const surfaceId of binding.unsupportedSurfaceIds) {
    if (typeof surfaceId !== "string"
      || !surfaceIds.has(surfaceId)
      || covered.has(surfaceId)) {
      throw new Error("车型目录 v2 vehicleSurfaceBinding 显式缺口非法");
    }
    covered.add(surfaceId);
  }
  if (covered.size !== catalog.selectionOrder.length
    || catalog.selectionOrder.some((surfaceId) => !covered.has(surfaceId))
    || (binding.capability === "complete" && binding.unsupportedSurfaceIds.length > 0)) {
    throw new Error("车型目录 v2 vehicleSurfaceBinding 必须完整覆盖 40 surface");
  }
  if (!Array.isArray(catalog.animations) || catalog.animations.length === 0) {
    throw new Error("车型目录 v2 animations 必须是非空数组");
  }
  const animationIds = new Set<string>();
  const animationKeys = new Set([
    "animationId",
    "displayName",
    "frameRate",
    "startFrame",
    "endFrame",
    "loopMode",
    "closeMode",
  ]);
  for (const animation of catalog.animations) {
    if (!isRecord(animation)
      || Object.keys(animation).some((key) => !animationKeys.has(key))
      || Object.keys(animation).length !== animationKeys.size
      || typeof animation.animationId !== "string"
      || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(animation.animationId)
      || animationIds.has(animation.animationId)
      || typeof animation.displayName !== "string"
      || animation.displayName.length === 0
      || typeof animation.frameRate !== "number"
      || !Number.isFinite(animation.frameRate)
      || animation.frameRate <= 0
      || !Number.isInteger(animation.startFrame)
      || !Number.isInteger(animation.endFrame)
      || animation.startFrame < 0
      || animation.endFrame <= animation.startFrame
      || !["none", "forward", "ping-pong"].includes(animation.loopMode)
      || !["reverse", "reset-to-start", "stop"].includes(animation.closeMode)) {
      throw new Error("车型目录 v2 animation 字段非法");
    }
    animationIds.add(animation.animationId);
  }
  for (const item of [
    ...catalog.categories,
    ...catalog.components,
    ...catalog.surfaces,
  ]) {
    if (item.ui !== undefined && !isRecord(item.ui)) {
      throw new Error("车型目录 v2 ui 必须是对象");
    }
    if (item.ui?.order !== undefined
      && (!Number.isInteger(item.ui.order) || Number(item.ui.order) < 0)) {
      throw new Error("车型目录 v2 ui.order 非法");
    }
    if (item.ui?.iconUrl !== undefined
      && item.ui.iconUrl !== null
      && (typeof item.ui.iconUrl !== "string" || !item.ui.iconUrl.startsWith("/"))) {
      throw new Error("车型目录 v2 ui.iconUrl 非法");
    }
    if (item.ui?.navigationMode !== undefined
      && (typeof item.ui.navigationMode !== "string"
        || !["tabs", "list", "none", "surfaces-as-components"].includes(
          item.ui.navigationMode,
        ))) {
      throw new Error("车型目录 v2 ui.navigationMode 非法");
    }
    const cameraId = item.ui?.cameraId;
    if (cameraId !== undefined && cameraId !== null) {
      if (!(typeof cameraId === "string" || Number.isInteger(cameraId))
        || !cameraIds.has(cameraKey(cameraId as CatalogCameraId))) {
        throw new Error(`车型目录 v2 ui.cameraId 不存在：${String(cameraId)}`);
      }
    }
    const animationId = item.ui?.animationId;
    if (animationId !== undefined && animationId !== null
      && (typeof animationId !== "string" || !animationIds.has(animationId))) {
      throw new Error(`车型目录 v2 ui.animationId 不存在：${String(animationId)}`);
    }
  }
  for (const family of catalog.materialFamilies) {
    if (family.ui !== undefined
      && (!isRecord(family.ui)
        || (family.ui.variantSort !== undefined
          && family.ui.variantSort !== "achromatic-then-rainbow")
        || (family.ui.defaultVariantId !== undefined
          && (typeof family.ui.defaultVariantId !== "string"
            || !/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(family.ui.defaultVariantId)))
        || Object.keys(family.ui)
          .some((key) => !["variantSort", "defaultVariantId"].includes(key)))) {
      throw new Error(`车型目录 v2 材料族 ${family.materialFamilyId} ui 非法`);
    }
  }
  const sortedFamilyIds = new Set(
    catalog.materialFamilies
      .filter((family) => family.ui?.variantSort === "achromatic-then-rainbow")
      .map((family) => family.materialFamilyId),
  );
  for (const variant of catalog.materialVariants) {
    const sortColorHex = variant.ui?.sortColorHex;
    if (variant.ui !== undefined
      && (!isRecord(variant.ui)
        || Object.keys(variant.ui).some((key) => key !== "sortColorHex")
        || (sortColorHex !== undefined
          && (typeof sortColorHex !== "string"
            || !/^#[0-9a-fA-F]{6}$/.test(sortColorHex))))) {
      throw new Error(`车型目录 v2 材料色卡 ${variant.variantId} ui 非法`);
    }
    if (sortedFamilyIds.has(variant.materialFamilyId)
      && (typeof sortColorHex !== "string" || !/^#[0-9a-fA-F]{6}$/.test(sortColorHex))) {
      throw new Error(`车型目录 v2 材料色卡 ${variant.variantId} 缺少合法 ui.sortColorHex`);
    }
  }
  const materialVariants = new Map(
    catalog.materialVariants.map((variant) => [variant.variantId, variant]),
  );
  for (const family of catalog.materialFamilies) {
    if (family.ui?.defaultVariantId === undefined) continue;
    if (materialVariants.get(family.ui.defaultVariantId)?.materialFamilyId
      !== family.materialFamilyId) {
      throw new Error(`车型目录 v2 材料族 ${family.materialFamilyId} 默认色号非法`);
    }
  }
  for (const option of catalog.options) {
    const ui = option.ui;
    if (ui === undefined) continue;
    if (!isRecord(ui)
      || (ui.order !== undefined && (!Number.isInteger(ui.order) || Number(ui.order) < 0))
      || (ui.iconUrl !== undefined
        && ui.iconUrl !== null
        && (typeof ui.iconUrl !== "string" || !ui.iconUrl.startsWith("/")))
      || (ui.control !== undefined
        && !["swatch", "thumbnail", "color-picker", "material-variant", "material-strip"]
          .includes(String(ui.control)))
      || (ui.sortColorHex !== undefined
        && (typeof ui.sortColorHex !== "string"
          || !/^#[0-9a-fA-F]{6}$/.test(ui.sortColorHex)))) {
      throw new Error(`车型目录 v2 选项 ${option.optionId} ui 非法`);
    }
    if (ui.defaultParameters !== undefined) {
      if (!isRecord(ui.defaultParameters)) {
        throw new Error(`车型目录 v2 选项 ${option.optionId} ui.defaultParameters 非法`);
      }
      for (const [key, parameter] of Object.entries(ui.defaultParameters)) {
        if (!PAINT_KEYS.includes(key as typeof PAINT_KEYS[number])
          || (key === "colorHex"
            ? typeof parameter !== "string" || !/^#[0-9a-fA-F]{6}$/.test(parameter)
            : typeof parameter !== "number"
              || !Number.isFinite(parameter)
              || parameter < 0
              || parameter > 1)) {
          throw new Error(`车型目录 v2 选项 ${option.optionId} ui.defaultParameters 非法`);
        }
      }
    }
  }
}

function digest24(value: string): string {
  return createHash("sha256").update(value, "utf8").digest("hex").slice(0, 24);
}

export function validateCatalogSelections(
  value: unknown,
  data: AutomotiveCatalogData,
): CatalogSelections {
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

  const selections: CatalogSelections = {};
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
    const option = data.options.get(optionId);
    if (option?.availability?.status === "disabled") {
      throw new RequestError(
        400,
        "OPTION_UNAVAILABLE",
        `${surfaceId} 所选 option 暂不可选：${option.availability.reason}`,
      );
    }
    if (!Object.entries(option?.requiresSelections ?? {}).every(
      ([requiredSurfaceId, requiredOptionId]) =>
        selections[requiredSurfaceId] === requiredOptionId
    )) {
      throw new RequestError(
        400,
        "SELECTION_REQUIREMENTS_NOT_MET",
        `${surfaceId} 所选 option 的 requiresSelections 不满足`,
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

export function validateCatalogCustomizations(
  value: unknown,
  selections: CatalogSelections,
  data: AutomotiveCatalogData,
): CatalogCustomizations {
  if (value === undefined) return {};
  if (!isRecord(value)) {
    throw new RequestError(400, "INVALID_CUSTOMIZATIONS", "customizations 必须是对象");
  }

  const result: CatalogCustomizations = {};
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
  catalog: AutomotiveCatalog,
  customizations: CatalogCustomizations,
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

export function deriveVehicleConfiguration(
  value: unknown,
  data: AutomotiveCatalogData,
  customizationValue?: unknown,
): VehicleConfiguration {
  const selections = validateCatalogSelections(value, data);
  const customizations = validateCatalogCustomizations(
    customizationValue,
    selections,
    data,
  );
  const catalog = data.catalog;
  const header = [
    `schemaVersion=${CATALOG_SCHEMA_VERSION}`,
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
    schemaVersion: CATALOG_SCHEMA_VERSION,
    catalogVersion: catalog.catalogVersion,
    vehicleId: catalog.vehicle.vehicleId,
    configurationId: `cfg-${digest24(canonicalInput)}`,
    renderKey:
      `${catalog.vehicle.vehicleId}__${catalog.catalogVersion}__render-${digest24(canonicalRenderInput)}`,
    selections,
    customizations,
  };
}

export function buildVehiclePriceResult(
  configuration: VehicleConfiguration,
  data: AutomotiveCatalogData,
): VehiclePriceResult {
  return {
    schemaVersion: CATALOG_SCHEMA_VERSION,
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

export function assertCatalogVersion(
  catalogVersion: unknown,
  vehicleId: unknown,
  data: AutomotiveCatalogData,
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
      "客户端 catalogVersion 与当前车型目录 v2 冲突",
    );
  }
  if (vehicleId !== data.catalog.vehicle.vehicleId) {
    throw new RequestError(404, "VEHICLE_NOT_FOUND", "车型不存在");
  }
}

export function loadAutomotiveCatalog(contractRoot = defaultContractRoot()): AutomotiveCatalogData {
  const path = resolve(
    contractRoot,
    "fixtures/sc01.catalog.draft.v2.json",
  );
  const catalog = JSON.parse(readFileSync(path, "utf8")) as AutomotiveCatalog;
  if (
    catalog.schemaVersion !== CATALOG_SCHEMA_VERSION ||
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
      "车型目录 v2 必须包含确认基础价、显式标配和完整顺序的 2.0.0 draft",
    );
  }

  const options = new Map<string, CatalogOption>();
  const materialVariants = new Map<string, CatalogMaterialVariant>();
  const optionIdsBySurface = new Map<string, Set<string>>();
  for (const surfaceId of catalog.selectionOrder) {
    optionIdsBySurface.set(surfaceId, new Set());
  }
  for (const option of catalog.options) {
    if (options.has(option.optionId)) {
      throw new Error(`车型目录 v2 optionId 重复：${option.optionId}`);
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
        option.availability !== undefined
        && (
          option.availability.status !== "disabled"
          || typeof option.availability.reason !== "string"
          || option.availability.reason.trim().length === 0
        )
      ) ||
      (
        option.parameters.color?.mode === "variant"
        && (
          option.materialFamilyId === null
          || option.pricing.unitPriceMinor === null
        )
      )
    ) {
      throw new Error(`车型目录 v2 选项 ${option.optionId} 价格状态非法`);
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
        `车型目录 v2 旧 optionId 迁移非法：${legacyOptionId} -> ${String(optionId)}`,
      );
    }
  }
  validateCatalogUi(catalog);
  for (const variant of catalog.materialVariants) {
    if (materialVariants.has(variant.variantId)) {
      throw new Error(`车型目录 v2 variantId 重复：${variant.variantId}`);
    }
    materialVariants.set(variant.variantId, variant);
  }
  for (const surfaceId of catalog.selectionOrder) {
    if (optionIdsBySurface.get(surfaceId)?.size === 0) {
      throw new Error(`车型目录 v2 必选表面 ${surfaceId} 没有选项`);
    }
  }
  return { catalog, options, materialVariants, optionIdsBySurface };
}
