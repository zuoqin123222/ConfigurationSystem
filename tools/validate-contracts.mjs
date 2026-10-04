import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { validateSidecarFixtures } from "./validate-vehicle-sidecars.mjs";
import { validateContentPackManifest } from "./validate-content-pack.mjs";
import { validateSourceAssets } from "./validate-source-assets.mjs";
import {
  validateAutomotiveCatalogFixtures,
} from "./validate-automotive-catalog-v2.mjs";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const schemaDir = resolve(root, "contracts", "schemas");
const fixtureDir = resolve(root, "contracts", "fixtures");
const failures = [];
const schemas = new Map();

function check(condition, message) {
  if (!condition) failures.push(message);
}

async function readJson(path) {
  const text = await readFile(path, "utf8");
  return JSON.parse(text);
}

function sameArray(actual, expected) {
  return (
    Array.isArray(actual) &&
    actual.length === expected.length &&
    actual.every((value, index) => value === expected[index])
  );
}

function cartesian(optionIdsByPart, order, index = 0, current = {}, result = []) {
  if (index === order.length) {
    result.push({ ...current });
    return result;
  }
  const partId = order[index];
  for (const optionId of optionIdsByPart.get(partId) ?? []) {
    current[partId] = optionId;
    cartesian(optionIdsByPart, order, index + 1, current, result);
  }
  return result;
}

const schemaNames = [
  "catalog.schema.json",
  "published-configurations.schema.json",
  "bake-manifest.schema.json",
  "vehicle-model-sidecar.schema.json",
  "vehicle-animation-sidecar.schema.json",
  "content-pack-manifest.schema.json",
  "reference-asset-normalization.schema.json",
  "catalog.v2.schema.json",
  "configuration.v2.schema.json",
  "price-result.v2.schema.json"
];

for (const name of schemaNames) {
  try {
    const schema = await readJson(resolve(schemaDir, name));
    schemas.set(name, schema);
    check(
      schema.$schema === "https://json-schema.org/draft/2020-12/schema",
      `${name}: 必须声明 JSON Schema 2020-12`
    );
    check(schema.type === "object", `${name}: 根类型必须为 object`);
    check(Boolean(schema.$id), `${name}: 缺少 $id`);
  } catch (error) {
    failures.push(`${name}: 不是有效 JSON (${error.message})`);
  }
}

const bakeManifestSchema = schemas.get("bake-manifest.schema.json");
if (bakeManifestSchema) {
  const rootRequired = bakeManifestSchema.required ?? [];
  const renderer = bakeManifestSchema.properties?.renderer;
  const alphaProcessing = bakeManifestSchema.properties?.alphaProcessing;
  const render = bakeManifestSchema.$defs?.render;
  const readyRule = render?.allOf?.find(
    (rule) => rule.if?.properties?.status?.const === "ready"
  );

  check(rootRequired.includes("renderer"), "bake manifest 顶层必须要求 renderer");
  check(
    rootRequired.includes("alphaProcessing"),
    "bake manifest 顶层必须要求 alphaProcessing"
  );

  check(
    renderer?.type === "object" && renderer.additionalProperties === false,
    "renderer 必须是禁止额外字段的 object"
  );
  check(
    sameArray(renderer?.required, ["engineVersion", "mode", "samplesPerPixel"]),
    "renderer 必须要求 engineVersion、mode、samplesPerPixel"
  );
  check(
    renderer?.properties?.engineVersion?.type === "string" &&
      renderer.properties.engineVersion.minLength === 1,
    "renderer.engineVersion 必须是非空字符串"
  );
  check(
    renderer?.properties?.mode?.const === "path-tracing",
    "renderer.mode 必须固定为 path-tracing"
  );
  check(
    renderer?.properties?.samplesPerPixel?.type === "integer" &&
      renderer.properties.samplesPerPixel.minimum === 1,
    "renderer.samplesPerPixel 必须是正整数"
  );

  check(
    alphaProcessing?.type === "object" &&
      alphaProcessing.additionalProperties === false,
    "alphaProcessing 必须是禁止额外字段的 object"
  );
  check(
    sameArray(alphaProcessing?.required, [
      "alphaMode",
      "autoDetectCoverageInversion",
      "clearTransparentRgb",
      "glowPolicy"
    ]),
    "alphaProcessing 必须要求全部 P0-3 字段"
  );
  check(
    alphaProcessing?.properties?.alphaMode?.const === "straight",
    "alphaProcessing.alphaMode 必须固定为 straight"
  );
  check(
    alphaProcessing?.properties?.autoDetectCoverageInversion?.type === "boolean",
    "alphaProcessing.autoDetectCoverageInversion 必须是 boolean"
  );
  check(
    alphaProcessing?.properties?.clearTransparentRgb?.type === "boolean",
    "alphaProcessing.clearTransparentRgb 必须是 boolean"
  );
  check(
    sameArray(alphaProcessing?.properties?.glowPolicy?.enum, [
      "synthetic-alpha",
      "separate-layer"
    ]),
    "alphaProcessing.glowPolicy 枚举必须是 synthetic-alpha、separate-layer"
  );

  const readyRequired = readyRule?.then?.required ?? [];
  for (const field of [
    "coverageInverted",
    "normalizationRequired",
    "glowRecoveredPixels",
    "width",
    "height",
    "sha256"
  ]) {
    check(readyRequired.includes(field), `ready render 必须要求 ${field}`);
  }
  check(
    render?.properties?.coverageInverted?.type === "boolean",
    "render.coverageInverted 必须是 boolean"
  );
  check(
    render?.properties?.normalizationRequired?.type === "boolean",
    "render.normalizationRequired 必须是 boolean"
  );
  check(
    render?.properties?.glowRecoveredPixels?.type === "integer" &&
      render.properties.glowRecoveredPixels.minimum === 0,
    "render.glowRecoveredPixels 必须是非负整数"
  );
}

let catalog;
let published;
try {
  catalog = await readJson(resolve(fixtureDir, "catalog.mvp.json"));
} catch (error) {
  failures.push(`catalog.mvp.json: 不是有效 JSON (${error.message})`);
}
try {
  published = await readJson(
    resolve(fixtureDir, "published-configurations.mvp.json")
  );
} catch (error) {
  failures.push(
    `published-configurations.mvp.json: 不是有效 JSON (${error.message})`
  );
}

if (catalog && published) {
  const order = ["paint", "wheel", "interior", "frame"];
  const expectedViews = ["front", "front-left", "side", "rear-right"];
  const idPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
  const optionIds = new Set();
  const primaryAssetIds = new Set();
  const optionIdsByPart = new Map();
  const priceByOption = new Map();

  check(catalog.schemaVersion === "1.0.0", "catalog schemaVersion 应为 1.0.0");
  check(catalog.currency === "CNY", "MVP 币种应为 CNY");
  check(catalog.vehicle?.vehicleId === "demo-car", "vehicleId 应为 demo-car");
  check(Number.isInteger(catalog.vehicle?.basePriceMinor), "基础价格必须为整数分");
  check(Array.isArray(catalog.parts) && catalog.parts.length === 4, "catalog 必须有 4 个分区");
  check(
    sameArray(catalog.parts?.map((part) => part.partId), order),
    "catalog 分区必须按 paint、wheel、interior、frame 排列"
  );

  for (const part of catalog.parts ?? []) {
    check(Boolean(part.zhName), `${part.partId}: 缺少中文名`);
    check(part.options?.length === 2, `${part.partId}: 必须恰有 2 个选项`);
    optionIdsByPart.set(part.partId, []);
    for (const option of part.options ?? []) {
      check(idPattern.test(option.optionId), `${option.optionId}: optionId 格式非法`);
      check(
        option.optionId.startsWith(`${part.partId}-`),
        `${option.optionId}: 必须以所属 partId 开头`
      );
      check(!optionIds.has(option.optionId), `${option.optionId}: optionId 重复`);
      optionIds.add(option.optionId);
      optionIdsByPart.get(part.partId).push(option.optionId);
      check(Boolean(option.zhName), `${option.optionId}: 缺少中文名`);
      check(
        Number.isInteger(option.priceDeltaMinor) && option.priceDeltaMinor >= 0,
        `${option.optionId}: priceDeltaMinor 必须是非负整数`
      );
      priceByOption.set(option.optionId, option.priceDeltaMinor);
      check(
        option.previewImageUrl === `/assets/previews/${option.optionId}.png`,
        `${option.optionId}: 预览图路径不符合约定`
      );
      check(
        option.uePrimaryAssetId === `CarMaterialOption:${option.optionId}`,
        `${option.optionId}: UE PrimaryAssetId 不符合约定`
      );
      check(
        !primaryAssetIds.has(option.uePrimaryAssetId),
        `${option.uePrimaryAssetId}: UE PrimaryAssetId 重复`
      );
      primaryAssetIds.add(option.uePrimaryAssetId);
    }
  }

  check(catalog.templates?.length === 2, "catalog 必须有 2 个模板");
  const templateIds = new Set();
  for (const template of catalog.templates ?? []) {
    check(!templateIds.has(template.templateId), `${template.templateId}: templateId 重复`);
    templateIds.add(template.templateId);
    check(Boolean(template.zhName), `${template.templateId}: 缺少中文名`);
    check(
      sameArray(Object.keys(template.selections ?? {}), order),
      `${template.templateId}: 模板选择字段顺序错误`
    );
    for (const partId of order) {
      check(
        optionIdsByPart.get(partId)?.includes(template.selections?.[partId]),
        `${template.templateId}: ${partId} 引用了非法 optionId`
      );
    }
  }

  const catalogViews = catalog.renderViews?.map((view) => view.renderViewId);
  check(sameArray(catalogViews, expectedViews), "catalog 必须声明约定的 4 个渲染视角");
  check(
    sameArray(published.configurationKeyOrder, order),
    "configurationKeyOrder 必须是 paint__wheel__interior__frame"
  );
  check(
    sameArray(published.renderViewIds, expectedViews),
    "published fixture 必须声明约定的 4 个渲染视角"
  );
  check(published.catalogVersion === catalog.catalogVersion, "catalogVersion 引用不一致");
  check(published.vehicleId === catalog.vehicle.vehicleId, "vehicleId 引用不一致");

  const generated = cartesian(optionIdsByPart, order);
  const expectedKeys = new Set(
    generated.map((selection) => order.map((partId) => selection[partId]).join("__"))
  );
  const actualKeys = new Set();
  check(generated.length === 16, `catalog 笛卡尔积应为 16，实际为 ${generated.length}`);
  check(
    published.configurations?.length === 16,
    `published configurations 应为 16，实际为 ${published.configurations?.length ?? 0}`
  );

  for (const configuration of published.configurations ?? []) {
    const keys = Object.keys(configuration.selections ?? {});
    check(
      sameArray(keys, order),
      `${configuration.configurationKey}: selections 字段顺序错误`
    );
    const canonicalKey = order
      .map((partId) => configuration.selections?.[partId])
      .join("__");
    check(
      configuration.configurationKey === canonicalKey,
      `${configuration.configurationKey}: 非 canonical key`
    );
    check(
      expectedKeys.has(configuration.configurationKey),
      `${configuration.configurationKey}: 不属于 catalog 笛卡尔积`
    );
    check(
      !actualKeys.has(configuration.configurationKey),
      `${configuration.configurationKey}: 配置键重复`
    );
    actualKeys.add(configuration.configurationKey);
    for (const partId of order) {
      check(
        optionIdsByPart.get(partId)?.includes(configuration.selections?.[partId]),
        `${configuration.configurationKey}: ${partId} 引用了非法 optionId`
      );
    }
    const expectedPrice =
      catalog.vehicle.basePriceMinor +
      order.reduce(
        (sum, partId) => sum + (priceByOption.get(configuration.selections?.[partId]) ?? 0),
        0
      );
    check(
      configuration.totalPriceMinor === expectedPrice,
      `${configuration.configurationKey}: 总价应为 ${expectedPrice}`
    );
  }

  check(
    actualKeys.size === expectedKeys.size &&
      [...expectedKeys].every((key) => actualKeys.has(key)),
    "published fixture 未完整覆盖 16 个笛卡尔组合"
  );
  const expectedRenderPaths = new Set();
  for (const key of actualKeys) {
    for (const viewId of published.renderViewIds ?? []) {
      expectedRenderPaths.add(
        `renders/${published.publicationVersion}/${published.vehicleId}/${key}/${viewId}.png`
      );
    }
  }
  check(expectedRenderPaths.size === 64, `图片期望应为 64，实际为 ${expectedRenderPaths.size}`);
  check(
    published.expectedRenderCount === expectedRenderPaths.size,
    `expectedRenderCount 应为 ${expectedRenderPaths.size}`
  );
}

try {
  const sidecarResult = await validateSidecarFixtures();
  failures.push(...sidecarResult.failures);
} catch (error) {
  failures.push(`车辆 sidecar 验证器执行失败 (${error.message})`);
}

try {
  const validContentPack = await readJson(resolve(fixtureDir, "content-pack.valid.json"));
  const invalidContentPack = await readJson(resolve(fixtureDir, "content-pack.invalid.json"));
  const validResult = validateContentPackManifest(validContentPack, {
    catalogVersion: "catalog-1",
    engineVersion: "5.8",
    platform: "Win64"
  });
  const invalidResult = validateContentPackManifest(invalidContentPack);
  check(validResult.valid, `有效 content-pack fixture 被拒绝 (${validResult.errors.join("; ")})`);
  check(
    !invalidResult.valid
      && invalidResult.errors.some((error) => error.includes("PrimaryAssetId 重复"))
      && invalidResult.errors.some((error) => error.includes("mountPoint")),
    "无效 content-pack fixture 必须因挂载点与重复 PrimaryAssetId 被拒绝"
  );
} catch (error) {
  failures.push(`content-pack fixture 验证失败 (${error.message})`);
}

try {
  const referenceJob = await readJson(
    resolve(fixtureDir, "reference-asset-normalization.example.json")
  );
  check(referenceJob.schemaVersion === "1.0.0", "参考资产清单仅支持 schemaVersion 1.0.0");
  check(referenceJob.kind === "maya-fbx-normalization", "参考资产清单 kind 错误");
  check(
    referenceJob.normalization?.mayaVersion === "2025"
      && referenceJob.normalization?.sceneUnit === "centimeter"
      && referenceJob.normalization?.upAxis === "+Z"
      && referenceJob.normalization?.fbxFileVersion === "FBX202000"
      && referenceJob.normalization?.binary === true,
    "参考资产清单必须冻结 Maya 2025、厘米、Z Up 与二进制 FBX 2020"
  );
  check(
    referenceJob.output?.overwrite === false
      && referenceJob.input?.path !== referenceJob.output?.path,
    "参考资产清单必须禁止覆盖输入 FBX"
  );
  check(
    referenceJob.provenance?.permittedUses?.includes("normalize")
      && referenceJob.provenance?.permittedUses?.includes("unreal-import"),
    "参考资产清单必须具备规范化与 Unreal 导入授权"
  );
} catch (error) {
  failures.push(`参考资产规范化示例验证失败 (${error.message})`);
}

try {
  await validateAutomotiveCatalogFixtures(root);
} catch (error) {
  failures.push(`车型目录 v2 契约验证失败 (${error.message})`);
}

const sourceAssetsResult = await validateSourceAssets(
  resolve(root, "source", "clients", "ue", "SourceAssets")
);
failures.push(...sourceAssetsResult.errors);

if (failures.length > 0) {
  console.error(`契约验证失败（${failures.length} 项）：`);
  for (const failure of failures) console.error(`- ${failure}`);
  process.exitCode = 1;
} else {
  console.log("契约验证通过：10 个 Schema JSON；v1 保持 8 个选项、2 个模板、16 个组合、4 个视角与 64 个图片期望；SC01 v2 草案通过有效/无效配置、禁止报价 price-result 与 2 个稳定身份黄金向量；参考资产、content-pack、P0-3 与车辆 sidecar 验证通过。");
}
