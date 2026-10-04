import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import test from "node:test";
import { buildApp } from "../src/app.js";
import { ConfigurationStoreV2 } from "../src/configuration-store-v2.js";
import {
  buildSc01PriceResult,
  deriveSc01Configuration,
  loadSc01V2,
} from "../src/sc01-v2.js";

const selections: Record<string, string> = {
  "exterior-body-cover": "body-cover-red",
  "wheel-material": "wheel-aluminum-alloy",
  "wheel-style": "wheel-style-multispoke",
  "wheel-color": "wheel-color-bright-silver",
  "lower-skirt": "lower-skirt-aluminum",
  "front-caliper-color": "front-caliper-black",
  "rear-caliper-color": "rear-caliper-black",
  "engine-bay-cover": "engine-cover-ppg-custom",
  "steering-wheel-skin": "steering-skin-ultrasuede-black",
  "steering-wheel-addon": "steering-addon-eva",
  "steering-center-mark": "steering-center-standard",
  "seat-backrest": "seat-back-ultrasuede-black",
  "seat-bolster": "seat-bolster-microfiber-black",
  "seat-shell-back": "seat-shell-carbon-original",
  "seat-headrest-mark": "seat-headrest-mark-ultrasuede",
  "door-upper": "door-upper-microfiber-black",
  "door-middle": "door-middle-ultrasuede-black",
  "door-armrest": "door-armrest-microfiber-black",
  "door-armrest-skin": "door-armrest-skin-microfiber-black",
  "ip-wings": "ip-wings-microfiber-black",
  "ip-middle": "ip-middle-microfiber-black",
  "ip-instrument-cover": "ip-instrument-cover-microfiber-black",
  "ip-upper-trim": "ip-upper-trim-microfiber-black",
  "ip-lower-trim": "ip-lower-trim-microfiber-black",
  "ip-center-mark": "ip-center-mark-uncovered-black",
  "storage-soft-bag": "storage-soft-bag-microfiber-black",
  "console-armrest-cover": "console-armrest-cover-microfiber-black",
  "console-armrest-side": "console-armrest-side-microfiber-black",
  "handbrake": "handbrake-microfiber-black",
  "roof-surface": "roof-woven-standard",
  "a-pillar-surface": "a-pillar-woven",
  "interior-painted-parts": "interior-painted-spray",
  "door-sill": "door-sill-leather",
  "embroidered-logo": "embroidered-logo-standard",
  "center-panel-trim": "center-panel-trim-custom",
  "shift-knob": "shift-knob-stainless",
  "brake-handle": "brake-handle-flamed-blue",
  "pedal": "pedal-racing",
};

const changedSelections = {
  ...selections,
  "steering-wheel-skin": "steering-skin-alcantara",
};

const request = {
  catalogVersion: "sc01-draft-20260121",
  vehicleId: "sc01",
  selections,
};

test("GET /api/v2/catalog 返回 SC01 draft 分层目录", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const response = await app.inject({ method: "GET", url: "/api/v2/catalog" });
  assert.equal(response.statusCode, 200);
  assert.equal(response.json().vehicle.vehicleId, "sc01");
  assert.equal(response.json().vehicle.quotable, false);
  assert.equal(response.json().options.length, 154);
  assert.equal(response.json().surfaces.length, 38);
  assert.equal(response.json().selectionOrder.length, 38);
  assert.equal(response.json().categories.map(
    (category: { displayName: string }) => category.displayName,
  ).join(" > "), "外饰 > 内饰 > 性能 > 个性化");
  assert.equal(response.json().surfaces.filter(
    (surface: { required: boolean }) => !surface.required,
  ).length, 8);
  assert.equal(response.json().materialVariants.length, 352);
});

test("创建配置返回稳定身份、revision 与禁止报价价格明细", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "create-sc01-a" },
    payload: request,
  });
  assert.equal(response.statusCode, 201);
  assert.equal(response.headers.etag, '"1"');
  assert.equal(response.headers["idempotency-replayed"], "false");
  const body = response.json();
  assert.match(body.configurationId, /^cfg-[a-f0-9]{24}$/);
  assert.match(body.renderKey, /__render-[a-f0-9]{24}$/);
  assert.equal(body.revision, 1);
  assert.equal(body.priceResult.quoteAllowed, false);
  assert.equal(body.priceResult.basePriceMinor, 22_980_000);
  assert.equal(body.priceResult.totalPriceMinor, 24_552_800);
});

test("无显式标配的表面默认不选装，Server 计算参考总价", () => {
  const data = loadSc01V2();
  const standardSelections = Object.fromEntries(
    data.catalog.selectionOrder.flatMap((surfaceId) => {
      const standard = data.catalog.options.find(
        (option) => option.surfaceId === surfaceId && option.pricing.isStandard,
      );
      return standard ? [[surfaceId, standard.optionId]] : [];
    }),
  );

  assert.equal(data.catalog.vehicle.basePriceMinor, 22_980_000);
  assert.equal(data.catalog.surfaces.find(
    (surface) => surface.surfaceId === "lower-skirt",
  )?.required, false);
  assert.equal(Object.hasOwn(standardSelections, "lower-skirt"), false);

  const baseline = deriveSc01Configuration(standardSelections, data);
  const baselinePrice = buildSc01PriceResult(baseline, data);
  assert.equal(baselinePrice.totalPriceMinor, 22_980_000);
  assert.equal(baselinePrice.lineItems.some(
    (item) => item.surfaceId === "lower-skirt",
  ), false);

  const configured = deriveSc01Configuration({
    ...standardSelections,
    "lower-skirt": "lower-skirt-aluminum",
  }, data);
  assert.equal(
    buildSc01PriceResult(configured, data).totalPriceMinor,
    23_280_000,
  );
});

test("Idempotency-Key 支持重放并拒绝不同请求复用", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const first = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "same-key" },
    payload: request,
  });
  const replay = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "same-key" },
    payload: request,
  });
  const conflict = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "same-key" },
    payload: { ...request, selections: changedSelections },
  });
  assert.equal(first.statusCode, 201);
  assert.equal(replay.statusCode, 200);
  assert.equal(replay.headers["idempotency-replayed"], "true");
  assert.equal(replay.json().configurationId, first.json().configurationId);
  assert.equal(conflict.statusCode, 409);
  assert.equal(conflict.json().code, "IDEMPOTENCY_CONFLICT");
});

test("revision 冲突不覆盖，成功更新保留 createdAt 并迁移稳定 ID", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const created = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: request,
  });
  const original = created.json();
  const conflict = await app.inject({
    method: "PUT",
    url: `/api/v2/configurations/${original.configurationId}`,
    payload: { ...request, selections: changedSelections, revision: 99 },
  });
  assert.equal(conflict.statusCode, 409);
  assert.equal(conflict.json().code, "REVISION_CONFLICT");

  const updated = await app.inject({
    method: "PUT",
    url: `/api/v2/configurations/${original.configurationId}`,
    payload: { ...request, selections: changedSelections, revision: 1 },
  });
  assert.equal(updated.statusCode, 200);
  assert.equal(updated.json().revision, 2);
  assert.equal(updated.json().createdAt, original.createdAt);
  assert.notEqual(updated.json().configurationId, original.configurationId);

  const oldLookup = await app.inject({
    method: "GET",
    url: `/api/v2/configurations/${original.configurationId}`,
  });
  const newLookup = await app.inject({
    method: "GET",
    url: `/api/v2/configurations/${updated.json().configurationId}`,
  });
  assert.equal(oldLookup.statusCode, 404);
  assert.equal(newLookup.statusCode, 200);
});

test("任一有备选项的 surface 变化都会改变 configurationId", () => {
  const data = loadSc01V2();
  const baseline = deriveSc01Configuration(selections, data);
  for (const surfaceId of data.catalog.selectionOrder) {
    const alternative = data.catalog.options.find(
      (option) => option.surfaceId === surfaceId
        && option.optionId !== selections[surfaceId],
    );
    if (!alternative) continue;
    const changed = deriveSc01Configuration(
      { ...selections, [surfaceId]: alternative.optionId },
      data,
    );
    assert.notEqual(changed.configurationId, baseline.configurationId, surfaceId);
    if (alternative.renderRelevant) {
      assert.notEqual(changed.renderKey, baseline.renderKey, surfaceId);
    }
  }
});

test("只有非 renderRelevant surface 选择变化才复用 renderKey", () => {
  const loaded = loadSc01V2();
  const catalog = structuredClone(loaded.catalog);
  for (const option of catalog.options) {
    if (option.surfaceId === "steering-wheel-skin") option.renderRelevant = false;
  }
  const data = {
    catalog,
    options: new Map(catalog.options.map((option) => [option.optionId, option])),
    materialVariants: loaded.materialVariants,
    optionIdsBySurface: loaded.optionIdsBySurface,
  };
  const baseline = deriveSc01Configuration(selections, data);
  const changed = deriveSc01Configuration(changedSelections, data);
  assert.notEqual(changed.configurationId, baseline.configurationId);
  assert.equal(changed.renderKey, baseline.renderKey);
});

test("customizations 按 surface 与字段固定排序并进入 configurationId/renderKey", () => {
  const data = loadSc01V2();
  const paintSelections = {
    ...selections,
    "exterior-body-cover": "body-cover-custom",
    "steering-wheel-skin": "steering-skin-ultrasuede-custom",
  };
  const paint = {
    colorHex: "#123456",
    metallic: 0.2,
    roughness: 0.3,
    clearCoat: 0.8,
    orangePeel: 0.1,
    flakeIntensity: 0.4,
  };
  const variant = data.catalog.materialVariants.find(
    (item) => item.materialFamilyId === "ultrasuede",
  )!;
  const first = deriveSc01Configuration(paintSelections, data, {
    "steering-wheel-skin": { materialVariantId: variant.variantId },
    "exterior-body-cover": paint,
  });
  const reordered = deriveSc01Configuration(paintSelections, data, {
    "exterior-body-cover": Object.fromEntries(Object.entries(paint).reverse()),
    "steering-wheel-skin": { materialVariantId: variant.variantId },
  });
  const baseline = deriveSc01Configuration(paintSelections, data);

  assert.equal(first.configurationId, reordered.configurationId);
  assert.equal(first.renderKey, reordered.renderKey);
  assert.notEqual(first.configurationId, baseline.configurationId);
  assert.notEqual(first.renderKey, baseline.renderKey);
  assert.equal(
    (first.customizations["exterior-body-cover"] as { colorHex: string }).colorHex,
    "#123456",
  );
});

test("Server 拒绝材料族不匹配的 materialVariantId 与越界车漆参数", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const data = loadSc01V2();
  const alcantara = data.catalog.materialVariants.find(
    (item) => item.materialFamilyId === "alcantara",
  )!;
  const mismatch = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: {
      ...request,
      selections: {
        ...selections,
        "steering-wheel-skin": "steering-skin-ultrasuede-custom",
      },
      customizations: {
        "steering-wheel-skin": { materialVariantId: alcantara.variantId },
      },
    },
  });
  assert.equal(mismatch.statusCode, 400);
  assert.equal(mismatch.json().code, "MATERIAL_VARIANT_FAMILY_MISMATCH");

  const invalidPaint = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: {
      ...request,
      selections: {
        ...selections,
        "exterior-body-cover": "body-cover-custom",
      },
      customizations: {
        "exterior-body-cover": {
          colorHex: "#123456",
          metallic: 1.1,
          roughness: 0.3,
          clearCoat: 0.8,
          orangePeel: 0.1,
          flakeIntensity: 0.4,
        },
      },
    },
  });
  assert.equal(invalidPaint.statusCode, 400);
  assert.equal(invalidPaint.json().code, "INVALID_PAINT_CUSTOMIZATION");
});

test("Server 将未选可选 surface 的 customization 识别为 400 客户端错误", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const data = loadSc01V2();
  const leather = data.catalog.materialVariants.find(
    (item) => item.materialFamilyId === "leather",
  )!;
  const optionalOmitted = { ...selections };
  delete optionalOmitted["door-sill"];

  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: {
      ...request,
      selections: optionalOmitted,
      customizations: {
        "door-sill": { materialVariantId: leather.variantId },
      },
    },
  });

  assert.equal(response.statusCode, 400);
  assert.equal(response.json().code, "INVALID_CUSTOMIZATION");
});

test("Server 拒绝不具备 variant 色彩能力的同材料族 option", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const data = loadSc01V2();
  const microfiber = data.catalog.materialVariants.find(
    (item) => item.materialFamilyId === "microfiber",
  )!;

  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: {
      ...request,
      selections: {
        ...request.selections,
        "embroidered-logo": "embroidered-logo-custom",
      },
      customizations: {
        "embroidered-logo": { materialVariantId: microfiber.variantId },
      },
    },
  });

  assert.equal(response.statusCode, 400);
  assert.equal(response.json().code, "MATERIAL_VARIANT_NOT_SUPPORTED");
});

test("customizations 可随配置保存并恢复", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const payload = {
    ...request,
    selections: {
      ...selections,
      "exterior-body-cover": "body-cover-custom",
    },
    customizations: {
      "exterior-body-cover": {
        colorHex: "#336699",
        metallic: 0.45,
        roughness: 0.25,
        clearCoat: 0.9,
        orangePeel: 0.12,
        flakeIntensity: 0.3,
      },
    },
  };
  const created = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload,
  });
  assert.equal(created.statusCode, 201);
  const restored = await app.inject({
    method: "GET",
    url: `/api/v2/configurations/${created.json().configurationId}`,
  });
  assert.equal(restored.statusCode, 200);
  assert.deepEqual(restored.json().customizations, payload.customizations);
});

test("旧 optionId 迁移到规范 ID，且自定义色按 option 能力验证", () => {
  const data = loadSc01V2();
  const migrated = deriveSc01Configuration({
    ...selections,
    "steering-wheel-skin": "steering-skin-leather-user",
  }, data);
  assert.equal(
    migrated.selections["steering-wheel-skin"],
    "steering-skin-leather",
  );

  const paint = {
    colorHex: "#445566",
    metallic: 0.5,
    roughness: 0.2,
    clearCoat: 0.8,
    orangePeel: 0.1,
    flakeIntensity: 0.3,
  };
  const chassis = deriveSc01Configuration(selections, data, {
    "engine-bay-cover": paint,
  });
  assert.deepEqual(chassis.customizations["engine-bay-cover"], paint);
});

test("草案拒绝报价请求，render resolve 只返回投影标识", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const quote = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: { ...request, quoteRequested: true },
  });
  assert.equal(quote.statusCode, 422);
  assert.equal(quote.json().code, "PRICE_UNCONFIRMED");

  const render = await app.inject({
    method: "POST",
    url: "/api/v2/renders/resolve",
    payload: { ...request, renderViewId: "front-left" },
  });
  assert.equal(render.statusCode, 200);
  assert.match(render.json().renderKey, /__render-[a-f0-9]{24}$/);
  assert.equal(render.json().renderViewId, "front-left");
  assert.equal(Object.hasOwn(render.json(), "imageUrl"), false);
});

test("原子 JSON 快照在重启后恢复配置与幂等索引", async (t) => {
  const directory = await mkdtemp(resolve(tmpdir(), "sc01-v2-store-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const snapshotPath = resolve(directory, "configurations-v2.json");
  const data = loadSc01V2();
  const configuration = deriveSc01Configuration(selections, data);
  const priceResult = buildSc01PriceResult(configuration, data);
  const firstStore = new ConfigurationStoreV2(
    () => new Date("2026-10-03T00:00:00.000Z"),
    snapshotPath,
  );
  firstStore.create(configuration, priceResult, "persisted-key");

  const snapshot = JSON.parse(await readFile(snapshotPath, "utf8"));
  assert.equal(snapshot.schemaVersion, "1.0.0");
  assert.equal(snapshot.configurations.length, 1);
  assert.equal(snapshot.idempotency.length, 1);
  snapshot.idempotency.push({
    key: "stale-key",
    fingerprint: "stale",
    configurationId: "cfg-000000000000000000000000",
  });
  await writeFile(snapshotPath, `${JSON.stringify(snapshot, null, 2)}\n`, "utf8");

  const restored = new ConfigurationStoreV2(
    () => new Date("2026-10-03T01:00:00.000Z"),
    snapshotPath,
  );
  assert.equal(restored.get(configuration.configurationId)?.revision, 1);
  assert.equal(
    restored.create(configuration, priceResult, "persisted-key").replayed,
    true,
  );
});
