import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import test from "node:test";
import {
  buildPriceResult,
  deriveConfigurationIdentity,
  validateCatalog,
  validateConfiguration,
  validatePriceResult,
  validateSourceReferences,
  validateSc01Fixtures
} from "./validate-sc01-v2.mjs";

const root = resolve(import.meta.dirname, "..");
const fixture = (name) =>
  readFile(resolve(root, "contracts", "fixtures", name), "utf8").then(JSON.parse);

test("SC01 v2 草案 catalog、正反配置、价格结果与黄金向量聚合通过", async () => {
  const result = await validateSc01Fixtures(root);
  assert.deepEqual(result, { optionCount: 8, vectorCount: 2 });
});

test("configurationId 与 renderKey 不受 selections 对象属性顺序影响", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const first = {
    "exterior-body-cover": "body-cover-yellow",
    "wheel-material": "wheel-magnesium-alloy",
    "steering-wheel-skin": "steering-skin-alcantara"
  };
  const reordered = {
    "steering-wheel-skin": "steering-skin-alcantara",
    "wheel-material": "wheel-magnesium-alloy",
    "exterior-body-cover": "body-cover-yellow"
  };
  assert.deepEqual(
    deriveConfigurationIdentity(catalog, first),
    deriveConfigurationIdentity(catalog, reordered)
  );
});

test("非渲染选项改变 configurationId，但复用同一 renderKey", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  for (const option of catalog.options) {
    if (option.surfaceId === "steering-wheel-skin") option.renderRelevant = false;
  }
  const base = {
    "exterior-body-cover": "body-cover-red",
    "wheel-material": "wheel-aluminum-alloy",
    "steering-wheel-skin": "steering-skin-ultrasuede-black"
  };
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
  const cases = [
    {
      ...valid,
      selections: {
        "exterior-body-cover": "body-cover-red",
        "wheel-material": "wheel-aluminum-alloy"
      }
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

test("拒绝把来源表内未确认金额录入 catalog 或开放报价", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");

  const priced = structuredClone(catalog);
  priced.options[0].pricing.unitPriceMinor = 960000;
  assert.throws(() => validateCatalog(priced), /未确认价格必须为 null/);

  const result = buildPriceResult(valid, catalog);
  result.quoteAllowed = true;
  assert.throws(
    () => validatePriceResult(result, valid, catalog),
    /必须保持所有未知金额为 null，并明确禁止报价/
  );
});

test("价格结果的基础价、单价、小计和总价全部为 null", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const valid = await fixture("sc01.configuration.valid.v2.json");
  const result = buildPriceResult(valid, catalog);

  assert.equal(result.basePriceMinor, null);
  assert.equal(result.totalPriceMinor, null);
  assert.equal(result.quoteAllowed, false);
  assert.ok(result.lineItems.every(
    (line) => line.unitPriceMinor === null && line.subtotalMinor === null
  ));
  assert.ok(result.blockingReasons.includes("PRICE_UNCONFIRMED"));
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
