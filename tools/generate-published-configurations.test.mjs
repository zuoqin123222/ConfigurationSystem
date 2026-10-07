import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import test from "node:test";
import {
  enumerateValidConfigurations,
  estimateV2Scale,
  generatePublishedConfigurations,
  generateV2Coverage,
  generateV2Plan,
  shardV2Coverage
} from "./generate-published-configurations.mjs";

const catalog = {
  schemaVersion: "1.0.0",
  catalogVersion: "catalog-v2",
  vehicle: { vehicleId: "car", basePriceMinor: 1000 },
  parts: [
    {
      partId: "paint",
      displayOrder: 0,
      options: [
        { optionId: "paint-red", priceDeltaMinor: 0 },
        { optionId: "paint-blue", priceDeltaMinor: 100 },
        { optionId: "paint-green", priceDeltaMinor: 200 }
      ]
    },
    {
      partId: "wheel",
      displayOrder: 1,
      options: [
        { optionId: "wheel-a", priceDeltaMinor: 0 },
        { optionId: "wheel-b", priceDeltaMinor: 50 }
      ]
    }
  ],
  renderViews: [
    { renderViewId: "front" },
    { renderViewId: "side" }
  ]
};

test("枚举每个分区的全部有效选项笛卡尔积，而不是截取少量颜色", () => {
  const result = enumerateValidConfigurations(catalog);
  assert.deepEqual(result.order, ["paint", "wheel"]);
  assert.equal(result.configurations.length, 6);
  assert.deepEqual(
    result.configurations.map((configuration) => configuration.configurationKey),
    [
      "paint-red__wheel-a",
      "paint-red__wheel-b",
      "paint-blue__wheel-a",
      "paint-blue__wheel-b",
      "paint-green__wheel-a",
      "paint-green__wheel-b"
    ]
  );
  assert.equal(result.configurations.at(-1).totalPriceMinor, 1250);
});

test("发布任务数由全部组合乘全部视角动态计算", () => {
  const published = generatePublishedConfigurations(catalog, "release-2");
  assert.equal(published.configurations.length, 6);
  assert.equal(published.expectedRenderCount, 12);
  assert.deepEqual(published.renderViewIds, ["front", "side"]);
});

test("空分区和无视角目录被明确拒绝", () => {
  assert.throws(
    () => enumerateValidConfigurations({ ...catalog, parts: [] }),
    /每个分区/
  );
  assert.throws(
    () => generatePublishedConfigurations({ ...catalog, renderViews: [] }, "release-2"),
    /RenderView/
  );
});

const v2Catalog = {
  schemaVersion: "2.0.0",
  catalogVersion: "catalog-v2",
  vehicle: { vehicleId: "car" },
  selectionOrder: ["paint", "wheel", "trim"],
  defaultSelections: {
    paint: "paint-red",
    wheel: "wheel-a"
  },
  options: [
    {
      optionId: "paint-red",
      surfaceId: "paint",
      renderRelevant: true,
      ui: { control: "swatch" }
    },
    {
      optionId: "paint-blue",
      surfaceId: "paint",
      renderRelevant: true,
      ui: { control: "swatch" }
    },
    {
      optionId: "paint-pearl",
      surfaceId: "paint",
      renderRelevant: true,
      ui: { control: "swatch" }
    },
    {
      optionId: "paint-custom",
      surfaceId: "paint",
      renderRelevant: true,
      ui: { control: "color-picker" },
      parameters: { color: { mode: "custom" } }
    },
    { optionId: "wheel-a", surfaceId: "wheel", renderRelevant: true },
    {
      optionId: "wheel-b",
      surfaceId: "wheel",
      materialFamilyId: "wheel-finish",
      renderRelevant: true,
      parameters: { color: { mode: "variant" } },
      pricing: { isStandard: false, unitPriceMinor: 100 }
    },
    { optionId: "trim-none", surfaceId: "trim", renderRelevant: false },
    { optionId: "trim-carbon", surfaceId: "trim", renderRelevant: true },
    {
      optionId: "trim-gray",
      surfaceId: "trim",
      renderRelevant: true,
      availability: { status: "disabled", reason: "暂不可选" }
    }
  ],
  materialVariants: [
    { variantId: "wheel-black", materialFamilyId: "wheel-finish" },
    { variantId: "wheel-gold", materialFamilyId: "wheel-finish" },
    { variantId: "unused-blue", materialFamilyId: "unused-family" }
  ]
};

test("v2 只估算 BigInt 全空间，不直接展开笛卡尔积", () => {
  const scale = estimateV2Scale(v2Catalog, {
    viewCount: 4,
    secondsPerRender: 10
  });
  assert.deepEqual(scale, {
    configurationCount: "16",
    renderCount: "64",
    viewCount: 4,
    secondsPerRender: 10,
    estimatedSeconds: "640",
    estimatedYears: "0"
  });
  assert.throws(() => enumerateValidConfigurations(v2Catalog), /v2 请使用/);
});

test("v2 coverage 覆盖 Bake 选项但排除 color-picker，普通 paint 色卡不受影响", () => {
  const coverage = generateV2Coverage(v2Catalog);
  const covered = new Set(
    coverage.configurations.flatMap(({ selections }) => Object.values(selections))
  );
  for (const option of v2Catalog.options.filter(
    ({ renderRelevant, availability, ui }) =>
      renderRelevant
      && availability?.status !== "disabled"
      && ui?.control !== "color-picker"
  )) {
    assert.ok(covered.has(option.optionId), `${option.optionId} 应被 coverage 覆盖`);
  }
  assert.equal(covered.has("paint-custom"), false);
  assert.equal(covered.has("trim-gray"), false);
  assert.equal(covered.has("paint-pearl"), true);
  assert.equal(coverage.renderRelevantOptionCount, 6);
  assert.equal(coverage.coveredRenderRelevantOptionCount, 6);
  assert.equal(coverage.excludedColorPickerOptionCount, 1);
  assert.equal(coverage.availableMaterialVariantCount, 2);
  assert.equal(coverage.coveredMaterialVariantCount, 2);
  assert.equal(coverage.configurations.length, 7);
  assert.ok(
    coverage.configurations.some(({ selections, customizations }) =>
      selections.paint === "paint-red"
      && selections.wheel === "wheel-a"
      && !Object.hasOwn(selections, "trim")
      && Object.keys(customizations).length === 0
    ),
    "coverage 必须包含与 Web 初始状态一致且不强选可选 surface 的基线配置"
  );
  assert.deepEqual(
    new Set(
      coverage.configurations.flatMap(({ customizations }) =>
        Object.values(customizations).map(({ materialVariantId }) => materialVariantId)
      )
    ),
    new Set(["wheel-black", "wheel-gold"])
  );
  for (const configuration of coverage.configurations) {
    assert.equal(configuration.configurationKey, configuration.renderKey);
  }
});

test("v2 shard 确定性拆分 coverage，合并后不重不漏", () => {
  const coverage = generateV2Coverage(v2Catalog).configurations;
  const shards = [0, 1].flatMap((index) => shardV2Coverage(coverage, index, 2));
  assert.equal(shards.length, coverage.length);
  assert.deepEqual(
    new Set(shards.map(({ renderKey }) => renderKey)),
    new Set(coverage.map(({ renderKey }) => renderKey))
  );
  const plan = generateV2Plan(v2Catalog, "release-v2", {
    mode: "shard",
    shardIndex: 1,
    shardCount: 2,
    viewCount: 4,
    secondsPerRender: 10
  });
  assert.deepEqual(plan.shard, { index: 1, count: 2 });
  assert.deepEqual(plan.renderViewIds, ["front", "front-left", "side", "rear-right"]);
  assert.ok(
    plan.configurations.every(({ configurationKey, renderKey }) =>
      configurationKey === renderKey
    )
  );
  assert.equal(plan.expectedRenderCount, plan.configurations.length * 4);
  assert.equal(plan.estimatedSeconds, String(plan.expectedRenderCount * 10));
  assert.throws(() => shardV2Coverage(coverage, 2, 2), /shardIndex/);
});

test("SC01 coverage 规模排除五个 color-picker 且保留普通车漆色卡", async () => {
  const sc01 = JSON.parse(await readFile(resolve(
    import.meta.dirname,
    "../contracts/fixtures/sc01.catalog.draft.v2.json"
  ), "utf8"));
  const coverage = generateV2Coverage(sc01);
  const plan = generateV2Plan(sc01, "sc01-v2");
  const covered = new Set(
    coverage.configurations.flatMap(({ selections }) => Object.values(selections))
  );
  const colorPickerIds = sc01.options
    .filter((option) => option.ui?.control === "color-picker")
    .map((option) => option.optionId);

  assert.deepEqual(colorPickerIds, [
    "body-cover-custom",
    "engine-cover-ppg-custom",
    "seat-shell-custom",
    "interior-painted-spray",
    "center-panel-trim-custom"
  ]);
  assert.ok(sc01.options
    .filter((option) => colorPickerIds.includes(option.optionId))
    .every((option) =>
      option.renderRelevant === true
      && option.parameters?.color?.mode === "custom"
    ));
  assert.ok(colorPickerIds.every((optionId) => !covered.has(optionId)));
  assert.ok(covered.has("body-cover-red"));
  assert.ok(covered.has("body-cover-silver"));
  assert.equal(coverage.renderRelevantOptionCount, 168);
  assert.equal(coverage.excludedColorPickerOptionCount, 5);
  assert.equal(coverage.availableMaterialVariantCount, 352);
  assert.equal(coverage.coveredMaterialVariantCount, 352);
  assert.equal(coverage.configurations.length, 486);
  assert.equal(plan.expectedRenderCount, 1944);
});
