import { accessSync, readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const PARTITIONS = ["paint", "wheel", "interior", "frame"] as const;
export type Partition = (typeof PARTITIONS)[number];
export type Selections = Record<Partition, string>;

interface CatalogOption {
  optionId: string;
  priceDeltaMinor: number;
}

interface CatalogPart {
  partId: Partition;
  options: CatalogOption[];
}

export interface Catalog {
  catalogVersion: string;
  vehicle: {
    vehicleId: string;
    basePriceMinor: number;
  };
  parts: CatalogPart[];
  renderViews: Array<{ renderViewId: string }>;
  [key: string]: unknown;
}

export interface PublishedConfiguration {
  configurationKey: string;
  selections: Selections;
  totalPriceMinor: number;
}

interface Publication {
  publicationVersion: string;
  catalogVersion: string;
  vehicleId: string;
  configurationKeyOrder: Partition[];
  renderViewIds: string[];
  configurations: PublishedConfiguration[];
}

export class RequestError extends Error {
  constructor(
    public readonly statusCode: number,
    public readonly code: string,
    message: string,
  ) {
    super(message);
  }
}

export interface ContractData {
  catalog: Catalog;
  publication: Publication;
  configurations: ReadonlyMap<string, PublishedConfiguration>;
  options: ReadonlyMap<Partition, ReadonlyMap<string, CatalogOption>>;
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
      accessSync(resolve(candidate, "fixtures/catalog.mvp.json"));
      return candidate;
    } catch {
      // 兼容源码运行、编译产物运行以及仓库根目录运行三种位置。
    }
  }
  throw new Error("无法定位根目录 contracts fixtures");
}

function readJson<T>(path: string): T {
  return JSON.parse(readFileSync(path, "utf8")) as T;
}

export function canonicalKey(selections: Selections): string {
  return PARTITIONS.map((partition) => selections[partition]).join("__");
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

export function validateSelections(
  value: unknown,
  data: ContractData,
): Selections {
  if (!isRecord(value)) {
    throw new RequestError(400, "INVALID_SELECTIONS", "selections 必须是对象");
  }

  const keys = Object.keys(value);
  if (
    keys.length !== PARTITIONS.length ||
    !keys.every((key) => PARTITIONS.includes(key as Partition))
  ) {
    throw new RequestError(
      400,
      "INVALID_PARTITIONS",
      "必须且只能提供 paint、wheel、interior、frame 四个分区",
    );
  }

  const selections = {} as Selections;
  for (const partition of PARTITIONS) {
    const optionId = value[partition];
    if (
      typeof optionId !== "string" ||
      !data.options.get(partition)?.has(optionId)
    ) {
      throw new RequestError(
        400,
        "INVALID_OPTION",
        `${partition} 包含未知或不属于该分区的 optionId`,
      );
    }
    selections[partition] = optionId;
  }
  return selections;
}

export function calculatePrice(
  selections: Selections,
  data: ContractData,
): number {
  return PARTITIONS.reduce((total, partition) => {
    const option = data.options.get(partition)?.get(selections[partition]);
    if (!option) {
      throw new RequestError(400, "INVALID_OPTION", `${partition} 选项不存在`);
    }
    return total + option.priceDeltaMinor;
  }, data.catalog.vehicle.basePriceMinor);
}

export function resolveConfiguration(
  value: unknown,
  data: ContractData,
): PublishedConfiguration {
  const selections = validateSelections(value, data);
  const key = canonicalKey(selections);
  const configuration = data.configurations.get(key);
  if (!configuration) {
    throw new RequestError(404, "CONFIGURATION_NOT_FOUND", "发布配置不存在");
  }

  const calculatedPrice = calculatePrice(selections, data);
  if (
    configuration.configurationKey !== key ||
    configuration.totalPriceMinor !== calculatedPrice
  ) {
    throw new RequestError(
      500,
      "CONTRACT_INCONSISTENT",
      "发布配置的 canonical key 或价格与 catalog 不一致",
    );
  }
  return configuration;
}

export function loadContracts(contractRoot = defaultContractRoot()): ContractData {
  const catalog = readJson<Catalog>(
    resolve(contractRoot, "fixtures/catalog.mvp.json"),
  );
  const publication = readJson<Publication>(
    resolve(contractRoot, "fixtures/published-configurations.mvp.json"),
  );

  const options = new Map<Partition, Map<string, CatalogOption>>();
  for (const partition of PARTITIONS) {
    const part = catalog.parts.find((item) => item.partId === partition);
    if (!part) {
      throw new Error(`catalog 缺少 ${partition} 分区`);
    }
    options.set(
      partition,
      new Map(part.options.map((option) => [option.optionId, option])),
    );
  }

  const configurations = new Map(
    publication.configurations.map((configuration) => [
      configuration.configurationKey,
      configuration,
    ]),
  );
  const data = { catalog, publication, configurations, options };

  // 启动时校验 fixture，避免把损坏的 canonical key 或价格暴露给客户端。
  for (const configuration of publication.configurations) {
    const resolved = resolveConfiguration(configuration.selections, data);
    if (resolved !== configuration) {
      throw new Error(`发布配置键重复：${configuration.configurationKey}`);
    }
  }
  return data;
}
