import { createHash } from "node:crypto";
import { accessSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { RequestError } from "./data.js";

export const SC01_SCHEMA_VERSION = "2.0.0";

export interface Sc01Pricing {
  unitPriceMinor: null;
  quantity: number | null;
  pricingUnit: string;
  isStandard: boolean;
  status: "unconfirmed";
  quotable: false;
}

export interface Sc01Option {
  optionId: string;
  surfaceId: string;
  renderRelevant: boolean;
  pricing: Sc01Pricing;
  [key: string]: unknown;
}

export interface Sc01Catalog {
  schemaVersion: "2.0.0";
  catalogVersion: string;
  lifecycle: "draft";
  currency: "CNY";
  vehicle: {
    vehicleId: string;
    basePriceMinor: null;
    priceStatus: "unconfirmed";
    quotable: false;
    [key: string]: unknown;
  };
  selectionOrder: string[];
  surfaces: Array<{ surfaceId: string; required: boolean; [key: string]: unknown }>;
  options: Sc01Option[];
  [key: string]: unknown;
}

export type Sc01Selections = Record<string, string>;

export interface Sc01Configuration {
  schemaVersion: "2.0.0";
  catalogVersion: string;
  vehicleId: string;
  configurationId: string;
  renderKey: string;
  selections: Sc01Selections;
}

export interface Sc01PriceResult {
  schemaVersion: "2.0.0";
  catalogVersion: string;
  vehicleId: string;
  configurationId: string;
  currency: "CNY";
  basePriceMinor: null;
  lineItems: Array<{
    surfaceId: string;
    optionId: string;
    unitPriceMinor: null;
    quantity: number | null;
    subtotalMinor: null;
    priceStatus: "unconfirmed";
  }>;
  totalPriceMinor: null;
  quoteAllowed: false;
  blockingReasons: [
    "BASE_PRICE_UNCONFIRMED",
    "OPTION_PRICE_UNCONFIRMED",
    "PRICE_UNCONFIRMED",
  ];
}

export interface Sc01V2Data {
  catalog: Sc01Catalog;
  options: ReadonlyMap<string, Sc01Option>;
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
  if (
    actual.length !== expected.length ||
    !actual.every((surfaceId) => expected.includes(surfaceId))
  ) {
    throw new RequestError(
      400,
      "INVALID_SELECTIONS",
      "selections 必须恰好包含 selectionOrder 中的全部表面",
    );
  }

  const selections: Sc01Selections = {};
  for (const surfaceId of expected) {
    const optionId = value[surfaceId];
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

export function deriveSc01Configuration(
  value: unknown,
  data: Sc01V2Data,
): Sc01Configuration {
  const selections = validateSc01Selections(value, data);
  const catalog = data.catalog;
  const header = [
    `schemaVersion=${SC01_SCHEMA_VERSION}`,
    `catalogVersion=${catalog.catalogVersion}`,
    `vehicleId=${catalog.vehicle.vehicleId}`,
  ];
  const canonicalInput = [
    ...header,
    ...catalog.selectionOrder.map(
      (surfaceId) => `${surfaceId}=${selections[surfaceId]}`,
    ),
  ].join("\n");
  const canonicalRenderInput = [
    ...header,
    ...catalog.selectionOrder.flatMap((surfaceId) => {
      const optionId = selections[surfaceId]!;
      return data.options.get(optionId)?.renderRelevant === true
        ? [`${surfaceId}=${optionId}`]
        : [];
    }),
  ].join("\n");

  return {
    schemaVersion: SC01_SCHEMA_VERSION,
    catalogVersion: catalog.catalogVersion,
    vehicleId: catalog.vehicle.vehicleId,
    configurationId: `cfg-${digest24(canonicalInput)}`,
    renderKey:
      `${catalog.vehicle.vehicleId}__${catalog.catalogVersion}__render-${digest24(canonicalRenderInput)}`,
    selections,
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
    basePriceMinor: null,
    lineItems: data.catalog.selectionOrder.map((surfaceId) => {
      const optionId = configuration.selections[surfaceId]!;
      const option = data.options.get(optionId);
      if (!option) {
        throw new RequestError(
          500,
          "CONTRACT_INCONSISTENT",
          `catalog 缺少选项 ${optionId}`,
        );
      }
      return {
        surfaceId,
        optionId,
        unitPriceMinor: null,
        quantity: option.pricing.quantity,
        subtotalMinor: null,
        priceStatus: "unconfirmed",
      };
    }),
    totalPriceMinor: null,
    quoteAllowed: false,
    blockingReasons: [
      "BASE_PRICE_UNCONFIRMED",
      "OPTION_PRICE_UNCONFIRMED",
      "PRICE_UNCONFIRMED",
    ],
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
    catalog.vehicle.basePriceMinor !== null ||
    catalog.vehicle.quotable !== false
  ) {
    throw new Error("SC01 v2 catalog 必须是禁止报价的 2.0.0 draft");
  }

  const options = new Map<string, Sc01Option>();
  const optionIdsBySurface = new Map<string, Set<string>>();
  for (const surfaceId of catalog.selectionOrder) {
    optionIdsBySurface.set(surfaceId, new Set());
  }
  for (const option of catalog.options) {
    if (options.has(option.optionId)) {
      throw new Error(`SC01 v2 optionId 重复：${option.optionId}`);
    }
    if (
      option.pricing.unitPriceMinor !== null ||
      option.pricing.status !== "unconfirmed" ||
      option.pricing.quotable !== false
    ) {
      throw new Error(`SC01 v2 选项 ${option.optionId} 价格状态非法`);
    }
    options.set(option.optionId, option);
    optionIdsBySurface.get(option.surfaceId)?.add(option.optionId);
  }
  for (const surfaceId of catalog.selectionOrder) {
    if (optionIdsBySurface.get(surfaceId)?.size === 0) {
      throw new Error(`SC01 v2 必选表面 ${surfaceId} 没有选项`);
    }
  }
  return { catalog, options, optionIdsBySurface };
}
