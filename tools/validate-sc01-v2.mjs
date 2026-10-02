import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const CONFIGURATION_ID = /^cfg-[a-f0-9]{24}$/;
const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  throw new Error(`SC01 v2 契约校验失败：${message}`);
}

function check(condition, message) {
  if (!condition) fail(message);
}

function isRecord(value) {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function assertExactKeys(value, expected, label) {
  check(isRecord(value), `${label} 必须是 object`);
  const actual = Object.keys(value).sort();
  const wanted = [...expected].sort();
  check(
    actual.length === wanted.length
      && actual.every((key, index) => key === wanted[index]),
    `${label} 字段必须恰为 ${expected.join("、")}`
  );
}

function uniqueIndex(items, key, label) {
  check(Array.isArray(items) && items.length > 0, `${label} 必须是非空数组`);
  const result = new Map();
  for (const [index, item] of items.entries()) {
    check(isRecord(item), `${label}[${index}] 必须是 object`);
    const id = item[key];
    check(typeof id === "string" && ID.test(id), `${label}[${index}].${key} 非法`);
    check(!result.has(id), `${label} 存在重复 ${key}：${id}`);
    result.set(id, item);
  }
  return result;
}

export function canonicalConfigurationInput(catalog, selections) {
  return [
    "schemaVersion=2.0.0",
    `catalogVersion=${catalog.catalogVersion}`,
    `vehicleId=${catalog.vehicle.vehicleId}`,
    ...catalog.selectionOrder.map(
      (surfaceId) => `${surfaceId}=${selections[surfaceId]}`
    )
  ].join("\n");
}

export function canonicalRenderInput(catalog, selections) {
  const options = new Map(catalog.options.map((option) => [option.optionId, option]));
  return [
    "schemaVersion=2.0.0",
    `catalogVersion=${catalog.catalogVersion}`,
    `vehicleId=${catalog.vehicle.vehicleId}`,
    ...catalog.selectionOrder.flatMap((surfaceId) => {
      const optionId = selections[surfaceId];
      return options.get(optionId)?.renderRelevant === true
        ? [`${surfaceId}=${optionId}`]
        : [];
    })
  ].join("\n");
}

export function deriveConfigurationIdentity(catalog, selections) {
  const canonicalInput = canonicalConfigurationInput(catalog, selections);
  const digest = createHash("sha256").update(canonicalInput, "utf8").digest("hex");
  const configurationId = `cfg-${digest.slice(0, 24)}`;
  const canonicalRender = canonicalRenderInput(catalog, selections);
  const renderDigest = createHash("sha256").update(canonicalRender, "utf8").digest("hex");
  return {
    canonicalInput,
    canonicalRenderInput: canonicalRender,
    configurationId,
    renderKey:
      `${catalog.vehicle.vehicleId}__${catalog.catalogVersion}__render-${renderDigest.slice(0, 24)}`
  };
}

export function validateCatalog(catalog) {
  check(isRecord(catalog), "catalog 根节点必须是 object");
  assertExactKeys(catalog, [
    "schemaVersion",
    "catalogVersion",
    "lifecycle",
    "currency",
    "vehicle",
    "selectionOrder",
    "categories",
    "components",
    "surfaces",
    "materialFamilies",
    "options"
  ], "catalog");
  check(catalog.schemaVersion === "2.0.0", "catalog.schemaVersion 必须为 2.0.0");
  check(catalog.lifecycle === "draft", "SC01 v2 当前仅允许 draft");
  check(catalog.currency === "CNY", "currency 必须为 CNY");
  check(
    typeof catalog.catalogVersion === "string" && ID.test(catalog.catalogVersion),
    "catalogVersion 非法"
  );
  check(
    isRecord(catalog.vehicle)
      && catalog.vehicle.vehicleId === "sc01"
      && catalog.vehicle.displayName === "SC01",
    "vehicle 必须标识 SC01"
  );
  check(catalog.vehicle.basePriceMinor === null, "未确认基础价必须为 null");
  check(catalog.vehicle.priceStatus === "unconfirmed", "基础价状态必须为 unconfirmed");
  check(catalog.vehicle.quotable === false, "草案车型必须禁止报价");

  const categories = uniqueIndex(catalog.categories, "categoryId", "categories");
  const components = uniqueIndex(catalog.components, "componentId", "components");
  const surfaces = uniqueIndex(catalog.surfaces, "surfaceId", "surfaces");
  const materialFamilies = uniqueIndex(
    catalog.materialFamilies,
    "materialFamilyId",
    "materialFamilies"
  );
  const options = uniqueIndex(catalog.options, "optionId", "options");

  check(
    Array.isArray(catalog.selectionOrder)
      && catalog.selectionOrder.length > 0
      && new Set(catalog.selectionOrder).size === catalog.selectionOrder.length,
    "selectionOrder 必须是非空且唯一的 surfaceId 数组"
  );
  for (const surfaceId of catalog.selectionOrder) {
    check(surfaces.get(surfaceId)?.required === true, `${surfaceId} 必须存在且为必选面`);
  }
  for (const component of components.values()) {
    check(categories.has(component.categoryId), `${component.componentId} 引用了未知 categoryId`);
  }
  for (const surface of surfaces.values()) {
    check(components.has(surface.componentId), `${surface.surfaceId} 引用了未知 componentId`);
  }

  const optionIdsBySurface = new Map(
    [...surfaces.keys()].map((surfaceId) => [surfaceId, new Set()])
  );
  for (const option of options.values()) {
    check(surfaces.has(option.surfaceId), `${option.optionId} 引用了未知 surfaceId`);
    check(
      option.materialFamilyId === null || materialFamilies.has(option.materialFamilyId),
      `${option.optionId} 引用了未知 materialFamilyId`
    );
    check(isRecord(option.pricing), `${option.optionId}.pricing 必须是 object`);
    check(option.pricing.unitPriceMinor === null, `${option.optionId} 未确认价格必须为 null`);
    check(
      option.pricing.quantity === null
        || (Number.isInteger(option.pricing.quantity) && option.pricing.quantity > 0),
      `${option.optionId} quantity 必须为 null 或正整数`
    );
    check(
      [
        "per-vehicle",
        "per-piece",
        "per-pair",
        "per-seat",
        "per-set",
        "percentage"
      ].includes(option.pricing.pricingUnit),
      `${option.optionId} pricingUnit 非法`
    );
    check(typeof option.renderRelevant === "boolean", `${option.optionId} renderRelevant 必须为 boolean`);
    check(option.pricing.status === "unconfirmed", `${option.optionId} 价格状态必须未确认`);
    check(option.pricing.quotable === false, `${option.optionId} 必须禁止报价`);
    optionIdsBySurface.get(option.surfaceId).add(option.optionId);
  }
  for (const surfaceId of catalog.selectionOrder) {
    check(optionIdsBySurface.get(surfaceId)?.size > 0, `${surfaceId} 至少需要一个选项`);
  }

  return { categories, components, surfaces, materialFamilies, options, optionIdsBySurface };
}

export function validateConfiguration(configuration, catalog) {
  const indexes = validateCatalog(catalog);
  check(isRecord(configuration), "configuration 根节点必须是 object");
  assertExactKeys(configuration, [
    "schemaVersion",
    "catalogVersion",
    "vehicleId",
    "configurationId",
    "renderKey",
    "selections"
  ], "configuration");
  check(configuration.schemaVersion === "2.0.0", "configuration.schemaVersion 必须为 2.0.0");
  check(configuration.catalogVersion === catalog.catalogVersion, "catalogVersion 不匹配");
  check(configuration.vehicleId === catalog.vehicle.vehicleId, "vehicleId 不匹配");
  assertExactKeys(configuration.selections, catalog.selectionOrder, "selections");

  for (const surfaceId of catalog.selectionOrder) {
    const optionId = configuration.selections[surfaceId];
    check(
      indexes.optionIdsBySurface.get(surfaceId)?.has(optionId),
      `${surfaceId} 引用了未知或跨面的 optionId：${optionId}`
    );
  }

  const expected = deriveConfigurationIdentity(catalog, configuration.selections);
  check(CONFIGURATION_ID.test(configuration.configurationId), "configurationId 格式非法");
  check(
    configuration.configurationId === expected.configurationId,
    `configurationId 不稳定，期望 ${expected.configurationId}`
  );
  check(
    configuration.renderKey === expected.renderKey,
    `renderKey 不稳定，期望 ${expected.renderKey}`
  );
  return expected;
}

export function buildPriceResult(configuration, catalog) {
  validateConfiguration(configuration, catalog);
  const options = new Map(catalog.options.map((option) => [option.optionId, option]));
  return {
    schemaVersion: "2.0.0",
    catalogVersion: catalog.catalogVersion,
    vehicleId: catalog.vehicle.vehicleId,
    configurationId: configuration.configurationId,
    currency: catalog.currency,
    basePriceMinor: null,
    lineItems: catalog.selectionOrder.map((surfaceId) => {
      const option = options.get(configuration.selections[surfaceId]);
      return {
        surfaceId,
        optionId: option.optionId,
        unitPriceMinor: null,
        quantity: option.pricing.quantity,
        subtotalMinor: null,
        priceStatus: "unconfirmed"
      };
    }),
    totalPriceMinor: null,
    quoteAllowed: false,
    blockingReasons: [
      "BASE_PRICE_UNCONFIRMED",
      "OPTION_PRICE_UNCONFIRMED",
      "PRICE_UNCONFIRMED"
    ]
  };
}

export function validatePriceResult(result, configuration, catalog) {
  const expected = buildPriceResult(configuration, catalog);
  check(
    JSON.stringify(result) === JSON.stringify(expected),
    "price-result 必须保持所有未知金额为 null，并明确禁止报价"
  );
  return result;
}

export function validateSourceReferences(catalog, sourceManifest) {
  check(
    isRecord(sourceManifest) && Array.isArray(sourceManifest.documents),
    "source-manifest.documents 必须是数组"
  );
  const documents = new Map();
  for (const document of sourceManifest.documents) {
    check(
      isRecord(document)
        && typeof document.documentId === "string"
        && ID.test(document.documentId)
        && Number.isInteger(document.pages)
        && document.pages > 0,
      "source-manifest 文档项非法"
    );
    check(!documents.has(document.documentId), `documentId 重复：${document.documentId}`);
    documents.set(document.documentId, document);
  }

  for (const collectionName of [
    "categories",
    "components",
    "surfaces",
    "materialFamilies",
    "options"
  ]) {
    for (const item of catalog[collectionName]) {
      check(
        Array.isArray(item.sourceRefs) && item.sourceRefs.length > 0,
        `${collectionName} 的每个条目都必须有 sourceRefs`
      );
      for (const ref of item.sourceRefs) {
        const document = documents.get(ref.documentId);
        check(document, `未知来源 documentId：${ref.documentId}`);
        check(
          Number.isInteger(ref.page) && ref.page >= 1 && ref.page <= document.pages,
          `${ref.documentId} 页码越界：${ref.page}`
        );
        check(
          typeof ref.locator === "string" && ref.locator.trim().length > 0,
          `${ref.documentId} 缺少 locator`
        );
      }
    }
  }
}

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

export async function validateSc01Fixtures(base = root) {
  const fixtures = resolve(base, "contracts", "fixtures");
  const catalog = await readJson(resolve(fixtures, "sc01.catalog.draft.v2.json"));
  const valid = await readJson(resolve(fixtures, "sc01.configuration.valid.v2.json"));
  const invalid = await readJson(resolve(fixtures, "sc01.configuration.invalid.v2.json"));
  const priceResult = await readJson(resolve(fixtures, "sc01.price-result.v2.json"));
  const golden = await readJson(resolve(fixtures, "sc01.identity-golden.v2.json"));
  const sourceManifest = await readJson(
    resolve(base, "docs", "product-data", "sc01", "source-manifest.json")
  );

  validateCatalog(catalog);
  validateSourceReferences(catalog, sourceManifest);
  validateConfiguration(valid, catalog);
  validatePriceResult(priceResult, valid, catalog);

  let invalidRejected = false;
  try {
    validateConfiguration(invalid, catalog);
  } catch {
    invalidRejected = true;
  }
  check(invalidRejected, "无效 configuration fixture 必须被拒绝");

  check(golden.schemaVersion === "2.0.0", "黄金向量 schemaVersion 错误");
  for (const [index, vector] of golden.vectors.entries()) {
    const derived = deriveConfigurationIdentity(catalog, vector.selections);
    check(
      derived.canonicalInput === vector.canonicalInput
        && derived.canonicalRenderInput === vector.canonicalRenderInput
        && derived.configurationId === vector.configurationId
        && derived.renderKey === vector.renderKey,
      `黄金向量 ${index} 不匹配`
    );
  }
  return { optionCount: catalog.options.length, vectorCount: golden.vectors.length };
}

const isMain =
  process.argv[1]
  && import.meta.url === pathToFileURL(resolve(process.argv[1])).href;

if (isMain) {
  try {
    const result = await validateSc01Fixtures();
    console.log(
      `SC01 v2 契约验证通过：${result.optionCount} 个草案选项、`
      + `有效/无效配置、price-result 禁止报价、${result.vectorCount} 个稳定身份黄金向量。`
    );
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
