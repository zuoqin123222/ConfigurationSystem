import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import test from "node:test";
import {
  buildPriceResult,
  deriveConfigurationIdentity,
  validateCatalog,
  validateConfiguration,
  validateCropManifest,
  validatePriceResult,
  validateSourceReferences,
  validateSc01Fixtures
} from "./validate-sc01-v2.mjs";

const root = resolve(import.meta.dirname, "..");
const fixture = (name) =>
  readFile(resolve(root, "contracts", "fixtures", name), "utf8").then(JSON.parse);

test("SC01 v2 草案 catalog、正反配置、价格结果与黄金向量聚合通过", async () => {
  const result = await validateSc01Fixtures(root);
  assert.deepEqual(result, { optionCount: 154, vectorCount: 2 });
});

test("catalog 使用显式 defaultSelections 消除多标配项的顺序歧义", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  assert.equal(catalog.defaultSelections["exterior-body-cover"], "body-cover-red");
  assert.equal(catalog.defaultSelections["door-middle"], "door-middle-ultrasuede-black");
  assert.equal(Object.hasOwn(catalog.defaultSelections, "lower-skirt"), false);

  const missingDefault = structuredClone(catalog);
  delete missingDefault.defaultSelections["wheel-material"];
  assert.throws(
    () => validateCatalog(missingDefault),
    /defaultSelections 必须恰好覆盖全部必选 surface/
  );

  const nonStandardDefault = structuredClone(catalog);
  nonStandardDefault.defaultSelections["wheel-material"] = "wheel-magnesium-alloy";
  assert.throws(
    () => validateCatalog(nonStandardDefault),
    /defaultSelections 必须引用同 surface 的标配 option/
  );
});

test("configurationId 与 renderKey 不受 selections 对象属性顺序影响", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");
  const first = valid.selections;
  const reordered = Object.fromEntries(Object.entries(first).reverse());
  assert.deepEqual(
    deriveConfigurationIdentity(catalog, first),
    deriveConfigurationIdentity(catalog, reordered)
  );
});

test("非渲染选项改变 configurationId，但复用同一 renderKey", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");
  for (const option of catalog.options) {
    if (option.surfaceId === "steering-wheel-skin") option.renderRelevant = false;
  }
  const base = valid.selections;
  const changed = {
    ...base,
    "steering-wheel-skin": "steering-skin-alcantara"
  };
  const first = deriveConfigurationIdentity(catalog, base);
  const second = deriveConfigurationIdentity(catalog, changed);
  assert.notEqual(first.configurationId, second.configurationId);
  assert.equal(first.renderKey, second.renderKey);
  assert.equal(first.canonicalRenderInput, second.canonicalRenderInput);
});

test("拒绝缺选、跨 surface 选项、未知选项与伪造稳定身份", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");
  const missingSelection = structuredClone(valid.selections);
  delete missingSelection["steering-wheel-skin"];
  const cases = [
    {
      ...valid,
      selections: missingSelection
    },
    {
      ...valid,
      selections: {
        ...valid.selections,
        "exterior-body-cover": "wheel-aluminum-alloy"
      }
    },
    {
      ...valid,
      selections: {
        ...valid.selections,
        "wheel-material": "wheel-not-known"
      }
    },
    { ...valid, configurationId: "cfg-000000000000000000000000" },
    { ...valid, renderKey: "sc01__forged__render-95c9a67d78364696f55a08dd" }
  ];
  for (const value of cases) {
    assert.throws(() => validateConfiguration(value, catalog), /SC01 v2 契约校验失败/);
  }
});

test("拒绝价格状态与金额不一致或开放报价", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");

  const priced = structuredClone(catalog);
  priced.options[0].pricing.unitPriceMinor = null;
  assert.throws(() => validateCatalog(priced), /价格状态与金额不一致/);

  const result = buildPriceResult(valid, catalog);
  result.quoteAllowed = true;
  assert.throws(
    () => validatePriceResult(result, valid, catalog),
    /必须包含基础价、选装明细、参考总价并明确禁止报价/
  );
});

test("材料 variant 只允许应用到声明 variant 色彩能力的 option", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");
  const unsupported = structuredClone(valid);
  unsupported.selections["embroidered-logo"] = "embroidered-logo-custom";
  unsupported.customizations = {
    "embroidered-logo": { materialVariantId: "microfiber-p16-np-3048" }
  };

  assert.throws(
    () => validateConfiguration(unsupported, catalog),
    /不支持材料色卡/
  );
});

test("价格结果包含基础价、已选选装明细与参考总价", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");
  const result = buildPriceResult(valid, catalog);

  assert.equal(result.basePriceMinor, 22980000);
  assert.equal(result.totalPriceMinor, 24552800);
  assert.equal(result.quoteAllowed, false);
  assert.equal(result.lineItems.length, 38);
  assert.ok(result.lineItems.every(
    (line) => Number.isInteger(line.unitPriceMinor)
      && Number.isInteger(line.subtotalMinor)
      && line.priceStatus === "confirmed"
  ));
  assert.ok(result.lineItems.some((line) => line.subtotalMinor > 0));
  assert.ok(result.blockingReasons.includes("PRICE_UNCONFIRMED"));
});

test("目录全量覆盖区域、表面、材料色卡和关键车漆定价", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  assert.equal(catalog.regions.length, 4);
  assert.deepEqual(
    catalog.categories.map((category) => category.displayName),
    ["外饰", "内饰", "性能", "个性化"]
  );
  assert.equal(catalog.surfaces.length, 38);
  assert.equal(catalog.selectionOrder.length, 38);
  assert.equal(catalog.surfaces.filter((surface) => !surface.required).length, 8);
  assert.ok(catalog.surfaces.every((surface) =>
    surface.required === catalog.options.some(
      (option) => option.surfaceId === surface.surfaceId && option.pricing.isStandard
    )
  ));
  assert.equal(catalog.materialVariants.length, 352);
  assert.equal(catalog.assetManifest, "/sc01/crop-manifest.json");

  const byId = new Map(catalog.options.map((option) => [option.optionId, option]));
  assert.equal(byId.get("body-cover-red").pricing.unitPriceMinor, 0);
  assert.equal(byId.get("body-cover-silver").pricing.unitPriceMinor, 0);
  assert.equal(byId.get("body-cover-custom").pricing.unitPriceMinor, 960000);
  assert.deepEqual(byId.get("body-cover-custom").parameters, {
    color: { mode: "custom", value: null, required: true },
    material: { materialFamilyId: "paint", variantId: null }
  });
  assert.ok(catalog.materialVariants.some((variant) => variant.reviewRequired));
  assert.deepEqual(
    catalog.materialFamilies
      .filter((family) => ["ultrasuede", "alcantara", "leather", "microfiber", "woven-wool"]
        .includes(family.materialFamilyId))
      .map((family) => family.displayName),
    ["奥司维", "Alcantara", "牛皮", "超纤皮", "织物羊毛"]
  );
  assert.ok(catalog.options.every((option) => !option.displayName.includes("%用户选择")));
  assert.equal(catalog.optionIdAliases["door-middle-leather-72"], "door-middle-leather");
  assert.equal(byId.get("engine-cover-silver").pricing.isStandard, true);
  assert.equal(byId.get("engine-cover-ppg-custom").parameters.color.mode, "custom");
  assert.equal(byId.get("a-pillar-woven").materialFamilyId, "woven-fabric");
  assert.equal(byId.get("a-pillar-woven").parameters.color.mode, "fixed");
  assert.equal(byId.get("roof-woven-standard").materialFamilyId, "woven-fabric");
  assert.equal(catalog.defaultSelections["ip-center-mark"], "ip-center-mark-uncovered-black");
  assert.equal(byId.get("ip-center-mark-uncovered-black").displayName, "无包覆(黑)");
  assert.ok([
    "ip-center-mark-ultrasuede",
    "ip-center-mark-alcantara",
    "ip-center-mark-microfiber",
    "ip-center-mark-leather"
  ].every((optionId) =>
    byId.get(optionId).parameters.color.mode === "variant"
      && byId.get(optionId).pricing.unitPriceMinor === 10000
  ));
  assert.ok([
    "seat-headrest-mark-ultrasuede",
    "seat-headrest-mark-alcantara",
    "seat-headrest-mark-microfiber",
    "seat-headrest-mark-leather"
  ].every((optionId) =>
    byId.get(optionId).parameters.color.mode === "variant"
      && byId.get(optionId).pricing.unitPriceMinor === 0
  ));
  assert.equal(
    catalog.surfaces.find((surface) => surface.surfaceId === "seat-shell-back").displayName,
    "背板"
  );
  assert.equal(byId.get("seat-shell-carbon-original").displayName, "高光原色碳纤维");
  assert.equal(byId.get("seat-shell-custom").displayName, "自定义取色");
  assert.equal(byId.get("seat-shell-custom").parameters.color.mode, "custom");
  assert.deepEqual(
    catalog.components
      .filter((component) => component.categoryId === "personalization")
      .map((component) => component.displayName),
    ["饰件", "缝线与徽标", "操控与脚垫"]
  );
  assert.deepEqual(
    catalog.options
      .filter((option) => option.surfaceId === "wheel-color")
      .map((option) => option.displayName),
    ["亮银色", "黑色", "深灰色", "碳纤维", "金色", "古铜色"]
  );
});

test("每个有备选项的 surface 变化都会改变 configurationId，且仅非渲染项复用 renderKey", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");
  const baseline = deriveConfigurationIdentity(catalog, valid.selections);

  for (const surfaceId of catalog.selectionOrder) {
    const alternative = catalog.options.find(
      (option) => option.surfaceId === surfaceId
        && option.optionId !== valid.selections[surfaceId]
    );
    if (!alternative) continue;
    const changed = deriveConfigurationIdentity(catalog, {
      ...valid.selections,
      [surfaceId]: alternative.optionId
    });
    assert.notEqual(changed.configurationId, baseline.configurationId, surfaceId);
    if (alternative.renderRelevant) {
      assert.notEqual(changed.renderKey, baseline.renderKey, surfaceId);
    }
  }
});

test("crop manifest、catalog URL 与 352 张 512 方形 WebP 一致", async () => {
  const publicRoot = resolve(root, "source", "clients", "web", "public");
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const manifest = JSON.parse(await readFile(
    resolve(publicRoot, "sc01", "crop-manifest.json"),
    "utf8"
  ));
  const result = await validateCropManifest(catalog, manifest, publicRoot);

  assert.equal(result.itemCount, 352);
  assert.equal(result.totalBytes, manifest.totalBytes);
  assert.ok(result.totalBytes < 64 * 1024 * 1024);
  assert.equal(manifest.schemaVersion, "2.0.0");
  assert.deepEqual(manifest.target, {
    width: 512,
    height: 512,
    format: "webp",
    quality: 90,
    method: 6,
    replaceable: true
  });
  assert.ok(manifest.items.some((item) => item.reviewRequired));
  assert.ok(manifest.items.every(
    (item) => item.output.endsWith(".webp")
      && item.sourcePage.file.endsWith(".png")
      && item.crop.width === item.crop.height
  ));
});

test("crop manifest 拒绝被篡改的输出哈希", async () => {
  const publicRoot = resolve(root, "source", "clients", "web", "public");
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const manifest = JSON.parse(await readFile(
    resolve(publicRoot, "sc01", "crop-manifest.json"),
    "utf8"
  ));
  manifest.items[0].sha256 = "0".repeat(64);

  await assert.rejects(
    validateCropManifest(catalog, manifest, publicRoot),
    /sha256 不匹配/
  );
});

test("拒绝未知来源文档和越界页码", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const manifest = JSON.parse(await readFile(
    resolve(root, "docs", "product-data", "sc01", "source-manifest.json"),
    "utf8"
  ));

  const unknownDocument = structuredClone(catalog);
  unknownDocument.options[0].sourceRefs[0].documentId = "unknown-document";
  assert.throws(
    () => validateSourceReferences(unknownDocument, manifest),
    /未知来源 documentId/
  );

  const invalidPage = structuredClone(catalog);
  invalidPage.options[0].sourceRefs[0].page = 999;
  assert.throws(
    () => validateSourceReferences(invalidPage, manifest),
    /页码越界/
  );
});
