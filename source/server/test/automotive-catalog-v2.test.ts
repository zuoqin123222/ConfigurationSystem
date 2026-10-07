import assert from "node:assert/strict";
import { cp, mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import test from "node:test";
import { buildApp } from "../src/app.js";
import { ConfigurationStoreV2 } from "../src/configuration-store-v2.js";
import {
  buildVehiclePriceResult,
  deriveVehicleConfiguration,
  loadAutomotiveCatalog,
} from "../src/automotive-catalog-v2.js";

const selections: Record<string, string> = {
  "exterior-body-cover": "body-cover-red",
  "rear-wing": "rear-wing-none",
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
  "embroidered-logo": "embroidered-logo-black",
  "headrest-embroidery": "headrest-embroidery-none",
  "door-panel-embroidery": "door-panel-embroidery-none",
  "center-panel-trim": "center-panel-trim-custom",
  "nameplate": "nameplate-stainless",
  "pedal": "pedal-racing",
};

const changedSelections = {
  ...selections,
  "steering-wheel-skin": "steering-skin-alcantara",
};

const request = {
  catalogVersion: "sc01-draft-20261007",
  vehicleId: "sc01",
  selections,
};

async function createV2BakeRoot(): Promise<{
  root: string;
  renderKey: string;
  png: Buffer;
}> {
  const root = await mkdtemp(resolve(tmpdir(), "automotive-v2-bake-"));
  const data = loadAutomotiveCatalog();
  const renderKey = deriveVehicleConfiguration(selections, data).renderKey;
  const repositoryRoot = resolve(import.meta.dirname, "../../../..");
  const sourcePng = resolve(
    repositoryRoot,
    "contracts/fixtures/bake.valid/renders/mvp-v1/demo-car",
    "paint-red__wheel-sport__interior-dark__frame-black/front.png",
  );
  const sourceManifest = JSON.parse(await readFile(
    resolve(repositoryRoot, "contracts/fixtures/bake.valid/bake-manifest.json"),
    "utf8",
  ));
  const sourceRender = sourceManifest.renders[0];
  const views = ["front", "front-left", "side", "rear-right"];
  const renders = [];
  for (const renderViewId of views) {
    const relativePath =
      `renders/sc01-v2/sc01/${renderKey}/${renderViewId}.png`;
    const destination = resolve(root, relativePath);
    await mkdir(resolve(destination, ".."), { recursive: true });
    await cp(sourcePng, destination);
    renders.push({
      renderKey,
      renderViewId,
      path: relativePath,
      width: sourceRender.width,
      height: sourceRender.height,
      format: "png",
      colorSpace: "sRGB",
      alphaMode: "straight",
      coverageInverted: sourceRender.coverageInverted,
      normalizationRequired: sourceRender.normalizationRequired,
      glowRecoveredPixels: sourceRender.glowRecoveredPixels,
      sha256: sourceRender.sha256,
      status: "ready",
    });
  }
  await writeFile(resolve(root, "bake-manifest.json"), `${JSON.stringify({
    schemaVersion: "2.0.0",
    manifestVersion: "sc01-v2",
    catalogVersion: data.catalog.catalogVersion,
    publicationVersion: "sc01-v2",
    vehicleId: data.catalog.vehicle.vehicleId,
    generatedAt: "2026-10-05T00:00:00.000Z",
    renderer: {
      engineVersion: "5.8.0",
      mode: "path-tracing",
      samplesPerPixel: 512,
      outputWidth: 2044,
      outputHeight: 1328,
      denoiser: true,
    },
    alphaProcessing: {
      alphaMode: "straight",
      autoDetectCoverageInversion: true,
      clearTransparentRgb: true,
      glowPolicy: "synthetic-alpha",
    },
    renders,
  }, null, 2)}\n`);
  return { root, renderKey, png: await readFile(sourcePng) };
}

test("GET /api/v2/catalog 返回 SC01 draft 分层目录", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const response = await app.inject({ method: "GET", url: "/api/v2/catalog" });
  assert.equal(response.statusCode, 200);
  assert.equal(response.json().vehicle.vehicleId, "sc01");
  assert.equal(response.json().vehicle.quotable, false);
  assert.equal(response.json().options.length, 175);
  assert.equal(response.json().surfaces.length, 40);
  assert.equal(response.json().selectionOrder.length, 40);
  assert.equal(response.json().categories.map(
    (category: { displayName: string }) => category.displayName,
  ).join(" > "), "外饰 > 内饰 > 性能 > 个性化");
  assert.equal(response.json().surfaces.filter(
    (surface: { required: boolean }) => !surface.required,
  ).length, 4);
  assert.equal(response.json().defaultSelections.nameplate, "nameplate-none");
  assert.equal(response.json().materialVariants.length, 352);
  assert.deepEqual(
    response.json().interactionCameras.map(
      (camera: { cameraId: string }) => camera.cameraId,
    ),
    ["exterior", "wheel", "driver", "seat", "front-cabin"],
  );
  assert.deepEqual(
    response.json().interactionCameras.map(
      (camera: { legacyIndex: number | null }) => camera.legacyIndex,
    ),
    [0, 2, 4, null, 5],
  );
  assert.deepEqual(
    response.json().animations.map(
      (animation: { displayName: string }) => animation.displayName,
    ),
    ["开启机舱盖", "开启左车门", "开启右车门", "后盖往复", "车轮旋转"],
  );
  assert.deepEqual(
    [...new Set(response.json().animations.map(
      (animation: { loopMode: string }) => animation.loopMode,
    ))],
    ["none", "forward"],
  );
  assert.deepEqual(
    response.json().animations.map(
      (animation: { frameRate: number }) => animation.frameRate,
    ),
    [30, 30, 30, 30, 30],
  );
  assert.deepEqual(
    response.json().animations.slice(3).map(
      (animation: { closeMode: string }) => animation.closeMode,
    ),
    ["reverse", "stop"],
  );
  assert.equal(
    response.json().skeletalMeshPath,
    "/Game/Configurator/_ImportStaging/audi-a5-rigged-v2/automotive-configurator-audi-a5-rigged-v2.automotive-configurator-audi-a5-rigged-v2",
  );
  assert.equal(
    response.json().sequencePath,
    "/Game/Configurator/_ImportStaging/audi-a5-rigged-v2/automotive-configurator-audi-a5-rigged-v2_Anim.automotive-configurator-audi-a5-rigged-v2_Anim",
  );
  assert.equal(
    response.json().animations.some(
      (animation: Record<string, unknown>) => Object.hasOwn(animation, "sequencePath"),
    ),
    false,
  );
  assert.equal(
    response.json().components.find(
      (component: { componentId: string }) => component.componentId === "chassis",
    ).ui.animationId,
    "hood",
  );
  assert.equal(
    response.json().components.find(
      (component: { componentId: string }) => component.componentId === "wheel",
    ).ui.cameraId,
    "wheel",
  );
  assert.equal(
    response.json().categories.find(
      (category: { categoryId: string }) => category.categoryId === "personalization",
    ).ui.navigationMode,
    "surfaces-as-components",
  );
  assert.deepEqual(
    response.json().materialFamilies
      .filter((family: { ui?: { variantSort?: string } }) =>
        family.ui?.variantSort === "achromatic-then-rainbow")
      .map((family: { materialFamilyId: string }) => family.materialFamilyId),
    ["ultrasuede", "alcantara", "leather", "microfiber"],
  );
});

test("Server 对旧 catalogVersion 明确返回 409，不尝试迁移配置", async (t) => {
  const app = buildApp();
  t.after(() => app.close());

  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: {
      ...request,
      catalogVersion: "sc01-draft-20260121",
    },
  });

  assert.equal(response.statusCode, 409);
  assert.equal(response.json().code, "VERSION_CONFLICT");
});

test("Server 只接受顶层完整骨骼网格和 AnimSequence 对象路径并拒绝 clip 私有路径", async (t) => {
  const directory = await mkdtemp(resolve(tmpdir(), "automotive-v2-animation-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const fixtures = resolve(directory, "fixtures");
  await mkdir(fixtures);
  const catalog = structuredClone(loadAutomotiveCatalog().catalog);
  const firstClip = catalog.animations[0] as unknown as Record<string, unknown>;
  firstClip.sequencePath = "/Game/Invalid/PerClip.PerClip";
  await writeFile(
    resolve(fixtures, "sc01.catalog.draft.v2.json"),
    JSON.stringify(catalog),
    "utf8",
  );
  assert.throws(() => loadAutomotiveCatalog(directory), /animation 字段非法/);

  delete firstClip.sequencePath;
  catalog.sequencePath = "Invalid/FullSequence";
  await writeFile(
    resolve(fixtures, "sc01.catalog.draft.v2.json"),
    JSON.stringify(catalog),
    "utf8",
  );
  assert.throws(() => loadAutomotiveCatalog(directory), /sequencePath/);

  catalog.sequencePath =
    "/Game/Configurator/AuthorizedAudiA5/Animations/A_A5_FullVehicle.A_A5_FullVehicle";
  catalog.skeletalMeshPath = "/Game/Invalid/SK_Car";
  await writeFile(
    resolve(fixtures, "sc01.catalog.draft.v2.json"),
    JSON.stringify(catalog),
    "utf8",
  );
  assert.throws(() => loadAutomotiveCatalog(directory), /skeletalMeshPath/);
});

test("Server 拒绝启用色相排序但缺少 sortColorHex 的材料 variant", async (t) => {
  const directory = await mkdtemp(resolve(tmpdir(), "automotive-v2-catalog-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const fixtures = resolve(directory, "fixtures");
  await mkdir(fixtures);
  const catalog = structuredClone(loadAutomotiveCatalog().catalog);
  const variant = catalog.materialVariants.find(
    (item) => item.materialFamilyId === "ultrasuede",
  )!;
  delete variant.ui;
  await writeFile(
    resolve(fixtures, "sc01.catalog.draft.v2.json"),
    JSON.stringify(catalog),
    "utf8",
  );

  assert.throws(
    () => loadAutomotiveCatalog(directory),
    /缺少合法 ui\.sortColorHex/,
  );

  variant.ui = { sortColorHex: "#12345G" };
  await writeFile(
    resolve(fixtures, "sc01.catalog.draft.v2.json"),
    JSON.stringify(catalog),
    "utf8",
  );
  assert.throws(
    () => loadAutomotiveCatalog(directory),
    /ui 非法/,
  );
});

test("创建配置返回稳定身份、revision 与禁止报价价格明细", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "create-automotive-a" },
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
  assert.equal(body.priceResult.totalPriceMinor, 24_424_000);
});

test("无显式标配的表面默认不选装，Server 计算参考总价", () => {
  const data = loadAutomotiveCatalog();
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

  const baseline = deriveVehicleConfiguration(standardSelections, data);
  const baselinePrice = buildVehiclePriceResult(baseline, data);
  assert.equal(baselinePrice.totalPriceMinor, 22_980_000);
  assert.equal(baselinePrice.lineItems.some(
    (item) => item.surfaceId === "lower-skirt",
  ), false);

  const configured = deriveVehicleConfiguration({
    ...standardSelections,
    "lower-skirt": "lower-skirt-aluminum",
  }, data);
  assert.equal(
    buildVehiclePriceResult(configured, data).totalPriceMinor,
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
  const data = loadAutomotiveCatalog();
  const baseline = deriveVehicleConfiguration(selections, data);
  for (const surfaceId of data.catalog.selectionOrder) {
    const alternative = data.catalog.options.find(
      (option) => option.surfaceId === surfaceId
        && option.optionId !== selections[surfaceId]
        && option.availability?.status !== "disabled"
        && Object.entries(option.requiresSelections ?? {}).every(
          ([requiredSurfaceId, requiredOptionId]) =>
            selections[requiredSurfaceId] === requiredOptionId
        ),
    );
    if (!alternative) continue;
    const candidateSelections = {
      ...selections,
      ...alternative.requiresSelections,
      [surfaceId]: alternative.optionId,
    };
    for (const dependentSurfaceId of data.catalog.selectionOrder) {
      const selected = data.options.get(candidateSelections[dependentSurfaceId]!);
      const requirementsMet = Object.entries(selected?.requiresSelections ?? {}).every(
        ([requiredSurfaceId, requiredOptionId]) =>
          candidateSelections[requiredSurfaceId] === requiredOptionId
      );
      if (requirementsMet) continue;
      const standardFallback = data.catalog.options.find(
        (option) => option.surfaceId === dependentSurfaceId
          && option.pricing.isStandard
          && option.availability?.status !== "disabled"
          && Object.entries(option.requiresSelections ?? {}).every(
            ([requiredSurfaceId, requiredOptionId]) =>
              candidateSelections[requiredSurfaceId] === requiredOptionId
          ),
      );
      const dependentSurface = data.catalog.surfaces.find(
        (surface) => surface.surfaceId === dependentSurfaceId,
      );
      const fallback = standardFallback ?? (
        dependentSurface?.required
          ? data.catalog.options.find(
              (option) => option.surfaceId === dependentSurfaceId
                && option.availability?.status !== "disabled"
                && Object.entries(option.requiresSelections ?? {}).every(
                  ([requiredSurfaceId, requiredOptionId]) =>
                    candidateSelections[requiredSurfaceId] === requiredOptionId
                ),
            )
          : undefined
      );
      if (fallback) candidateSelections[dependentSurfaceId] = fallback.optionId;
    }
    const changed = deriveVehicleConfiguration(
      candidateSelections,
      data,
    );
    assert.notEqual(changed.configurationId, baseline.configurationId, surfaceId);
    if (alternative.renderRelevant) {
      assert.notEqual(changed.renderKey, baseline.renderKey, surfaceId);
    }
  }
});

test("只有非 renderRelevant surface 选择变化才复用 renderKey", () => {
  const loaded = loadAutomotiveCatalog();
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
  const baseline = deriveVehicleConfiguration(selections, data);
  const changed = deriveVehicleConfiguration(changedSelections, data);
  assert.notEqual(changed.configurationId, baseline.configurationId);
  assert.equal(changed.renderKey, baseline.renderKey);
});

test("customizations 按 surface 与字段固定排序并进入 configurationId/renderKey", () => {
  const data = loadAutomotiveCatalog();
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
  const first = deriveVehicleConfiguration(paintSelections, data, {
    "steering-wheel-skin": { materialVariantId: variant.variantId },
    "exterior-body-cover": paint,
  });
  const reordered = deriveVehicleConfiguration(paintSelections, data, {
    "exterior-body-cover": Object.fromEntries(Object.entries(paint).reverse()),
    "steering-wheel-skin": { materialVariantId: variant.variantId },
  });
  const baseline = deriveVehicleConfiguration(paintSelections, data);

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
  const data = loadAutomotiveCatalog();
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
  const data = loadAutomotiveCatalog();
  const leather = data.catalog.materialVariants.find(
    (item) => item.materialFamilyId === "leather",
  )!;
  const optionalOmitted = { ...selections };
  delete optionalOmitted["lower-skirt"];

  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: {
      ...request,
      selections: optionalOmitted,
      customizations: {
        "lower-skirt": { materialVariantId: leather.variantId },
      },
    },
  });

  assert.equal(response.statusCode, 400);
  assert.equal(response.json().code, "INVALID_CUSTOMIZATION");
});

test("Server 拒绝不具备 variant 色彩能力的同材料族 option", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const data = loadAutomotiveCatalog();
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
        "embroidered-logo": "embroidered-logo-red",
      },
      customizations: {
        "embroidered-logo": { materialVariantId: microfiber.variantId },
      },
    },
  });

  assert.equal(response.statusCode, 400);
  assert.equal(response.json().code, "MATERIAL_VARIANT_NOT_SUPPORTED");
});

test("Server 拒绝目录中标记为暂不可选的选项", async (t) => {
  const app = buildApp();
  t.after(() => app.close());

  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: {
      ...request,
      selections: {
        ...request.selections,
        "rear-wing": "rear-wing-gray",
      },
    },
  });

  assert.equal(response.statusCode, 400);
  assert.equal(response.json().code, "OPTION_UNAVAILABLE");
  assert.match(response.json().message, /暂不可选/);
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

test("同 surface 的旧 optionId 可迁移，且自定义色按 option 能力验证", () => {
  const data = loadAutomotiveCatalog();
  const migrated = deriveVehicleConfiguration({
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
  const chassis = deriveVehicleConfiguration(selections, data, {
    "engine-bay-cover": paint,
  });
  assert.deepEqual(chassis.customizations["engine-bay-cover"], paint);
});

test("旧配置缺少铭牌选择时归一化为稳定的免费无选项", () => {
  const data = loadAutomotiveCatalog();
  const legacySelections = { ...selections };
  delete legacySelections.nameplate;

  const migrated = deriveVehicleConfiguration(legacySelections, data);

  assert.equal(migrated.selections.nameplate, "nameplate-none");
});

test("Server 拒绝 requiresSelections 不满足及跨旧 surface 的 optionId", () => {
  const data = loadAutomotiveCatalog();
  assert.throws(
    () => deriveVehicleConfiguration({
      ...selections,
      "wheel-style": "wheel-style-magnesium-1",
    }, data),
    (error: unknown) => error instanceof Error
      && "code" in error
      && error.code === "SELECTION_REQUIREMENTS_NOT_MET",
  );
  assert.throws(
    () => deriveVehicleConfiguration({
      ...selections,
      "headrest-embroidery": "brake-handle-flamed-blue",
    }, data),
    (error: unknown) => error instanceof Error
      && "code" in error
      && error.code === "INVALID_OPTION",
  );
});

test("未配置 v2 bake 时草案 resolve 只返回投影标识", async (t) => {
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

test("独立 v2 bake manifest 按 renderKey 与视角解析并安全托管图片", async (t) => {
  const fixture = await createV2BakeRoot();
  t.after(() => rm(fixture.root, { recursive: true, force: true }));
  const app = buildApp({ v2BakeRoot: fixture.root, webRoot: false });
  t.after(() => app.close());

  const resolved = await app.inject({
    method: "POST",
    url: "/api/v2/renders/resolve",
    payload: { ...request, renderViewId: "front-left" },
  });
  assert.equal(resolved.statusCode, 200);
  assert.equal(resolved.json().renderKey, fixture.renderKey);
  assert.equal(
    resolved.json().imageUrl,
    `/assets/v2/renders/sc01-v2/sc01/${fixture.renderKey}/front-left.png`,
  );

  const image = await app.inject({
    method: "GET",
    url: resolved.json().imageUrl,
  });
  assert.equal(image.statusCode, 200);
  assert.equal(image.headers["content-type"], "image/png");
  assert.equal(
    image.headers["cache-control"],
    "public, max-age=31536000, immutable",
  );
  assert.equal(image.headers["x-content-type-options"], "nosniff");
  assert.deepEqual(image.rawPayload, fixture.png);

  for (const url of [
    `/assets/v2/renders/sc01-v2/sc01/not-a-render-key/front.png`,
    `/assets/v2/renders/sc01-v2/sc01/${fixture.renderKey}/unknown.png`,
    "/assets/v2/renders/sc01-v2/sc01/%2e%2e/front.png",
  ]) {
    const rejected = await app.inject({ method: "GET", url });
    assert.notEqual(rejected.statusCode, 200);
  }
});

test("配置 v2 bake 后 resolve 对未发布 renderKey 返回 404", async (t) => {
  const fixture = await createV2BakeRoot();
  t.after(() => rm(fixture.root, { recursive: true, force: true }));
  const app = buildApp({ v2BakeRoot: fixture.root, webRoot: false });
  t.after(() => app.close());

  const response = await app.inject({
    method: "POST",
    url: "/api/v2/renders/resolve",
    payload: {
      ...request,
      selections: changedSelections,
      renderViewId: "front-left",
    },
  });
  assert.equal(response.statusCode, 404);
  assert.equal(response.json().code, "RENDER_NOT_FOUND");
});

test("原子 JSON 快照在重启后恢复配置与幂等索引", async (t) => {
  const directory = await mkdtemp(resolve(tmpdir(), "automotive-v2-store-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const snapshotPath = resolve(directory, "configurations-v2.json");
  const data = loadAutomotiveCatalog();
  const configuration = deriveVehicleConfiguration(selections, data);
  const priceResult = buildVehiclePriceResult(configuration, data);
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
