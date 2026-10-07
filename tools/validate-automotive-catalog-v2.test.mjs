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
  validateAutomotiveCatalogFixtures
} from "./validate-automotive-catalog-v2.mjs";

const root = resolve(import.meta.dirname, "..");
const fixture = (name) =>
  readFile(resolve(root, "contracts", "fixtures", name), "utf8").then(JSON.parse);

test("SC01 v2 草案 catalog、正反配置、价格结果与黄金向量聚合通过", async () => {
  const result = await validateAutomotiveCatalogFixtures(root);
  assert.deepEqual(result, { optionCount: 174, vectorCount: 2 });
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

test("catalog 声明五个语义交互镜头并校验层级 cameraId 引用", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  assert.deepEqual(
    catalog.interactionCameras.map((camera) => camera.cameraId),
    ["exterior", "wheel", "driver", "seat", "front-cabin"]
  );
  assert.deepEqual(
    catalog.interactionCameras.map((camera) => camera.legacyIndex),
    [0, 2, 4, null, 5]
  );
  assert.ok(catalog.interactionCameras.every((camera, index) =>
    camera.order === index
      && typeof camera.zone === "string"
      && camera.displayName.length > 0
      && camera.iconUrl.startsWith("/")
  ));
  assert.equal(
    catalog.components.find((component) => component.componentId === "wheel").ui.cameraId,
    "wheel"
  );

  const invalid = structuredClone(catalog);
  invalid.components[0].ui.cameraId = "missing-camera";
  assert.throws(() => validateCatalog(invalid), /未知 interactionCamera/);

  const invalidLegacyIndex = structuredClone(catalog);
  invalidLegacyIndex.interactionCameras[0].legacyIndex = 6;
  assert.throws(() => validateCatalog(invalidLegacyIndex), /legacyIndex 非法/);
});

test("SC01 文案修订不改变既有轮毂、内饰和性能稳定 ID", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const surfaces = new Map(catalog.surfaces.map((surface) => [surface.surfaceId, surface]));
  const options = new Map(catalog.options.map((option) => [option.optionId, option]));

  assert.deepEqual(
    ["wheel-material", "wheel-style", "wheel-color"].map((surfaceId) =>
      surfaces.get(surfaceId).displayName),
    ["轮毂材质", "轮毂造型", "轮毂颜色"]
  );
  assert.equal(options.get("wheel-style-multispoke").displayName, "多条幅轮毂");
  assert.equal(surfaces.get("steering-wheel-skin").displayName, "表皮");
  assert.equal(surfaces.get("seat-shell-back").displayName, "背板");
  assert.equal(surfaces.get("door-middle").displayName, "中面板");
  assert.equal(surfaces.get("ip-upper-trim").displayName, "上层软包");
  assert.equal(surfaces.get("roof-surface").displayName, "棚面");
  assert.equal(surfaces.get("steering-wheel-addon").displayName, "加粗(EVA海绵)");
  assert.equal(options.get("steering-addon-eva").displayName, "纯黑色");
  assert.equal(options.get("lower-skirt-aluminum").displayName, "铝合金");

  assert.ok(catalog.selectionOrder.includes("lower-skirt"));
  assert.equal(surfaces.get("lower-skirt").required, false);
  assert.equal(Object.hasOwn(catalog.defaultSelections, "lower-skirt"), false);
});

test("轮毂造型按材质声明 requiresSelections，镁合金八款均为 500 元", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const styles = catalog.options.filter((option) => option.surfaceId === "wheel-style");
  const aluminum = styles.filter((option) =>
    option.requiresSelections?.["wheel-material"] === "wheel-aluminum-alloy");
  const magnesium = styles.filter((option) =>
    option.requiresSelections?.["wheel-material"] === "wheel-magnesium-alloy");

  assert.deepEqual(aluminum.map((option) => option.displayName), ["多条幅轮毂"]);
  assert.deepEqual(
    magnesium.map((option) => option.displayName),
    ["款式1", "款式2", "款式3", "款式4", "款式5", "款式6", "款式7", "款式8"]
  );
  assert.ok(magnesium.every((option) =>
    option.pricing.unitPriceMinor === 50000 && option.pricing.status === "confirmed"));

  const incompatible = await fixture("sc01.configuration.valid.v2.json");
  incompatible.selections["wheel-material"] = "wheel-magnesium-alloy";
  assert.throws(
    () => validateConfiguration(incompatible, catalog),
    /requiresSelections 不满足/
  );
});

test("catalog 顶层声明完整骨骼网格与动画序列对象路径", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  assert.equal(
    catalog.skeletalMeshPath,
    "/Game/Configurator/_ImportStaging/audi-a5-rigged-v2/automotive-configurator-audi-a5-rigged-v2.automotive-configurator-audi-a5-rigged-v2"
  );
  assert.equal(
    catalog.sequencePath,
    "/Game/Configurator/_ImportStaging/audi-a5-rigged-v2/automotive-configurator-audi-a5-rigged-v2_Anim.automotive-configurator-audi-a5-rigged-v2_Anim"
  );

  const incompleteMesh = structuredClone(catalog);
  incompleteMesh.skeletalMeshPath = "/Game/Vehicle/SK_Car";
  assert.throws(() => validateCatalog(incompleteMesh), /skeletalMeshPath/);

  const incompleteSequence = structuredClone(catalog);
  incompleteSequence.sequencePath = "/Game/Vehicle/A_FullVehicle";
  assert.throws(() => validateCatalog(incompleteSequence), /sequencePath/);
});

test("catalog 代理车为 40 surface 提供唯一运行时目标且不伪装为正式资产", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const capability = catalog.vehicleSurfaceBinding;
  assert.equal(capability.capability, "proxy");
  assert.equal(capability.bindings.length, 40);
  assert.deepEqual(capability.unsupportedSurfaceIds, []);
  assert.deepEqual(
    capability.bindings.find((binding) => binding.surfaceId === "door-middle")
      ?.materialSlotIds,
    ["A5Proxy_DoorMiddle"]
  );
  assert.equal(
    new Set(capability.bindings.flatMap((binding) => binding.materialSlotIds)).size,
    40
  );
  assert.doesNotThrow(() => validateCatalog(catalog));

  const collision = structuredClone(catalog);
  collision.vehicleSurfaceBinding.bindings[1].materialSlotIds =
    collision.vehicleSurfaceBinding.bindings[0].materialSlotIds;
  assert.throws(() => validateCatalog(collision), /material slot 重复/);

  const implicitGap = structuredClone(catalog);
  implicitGap.vehicleSurfaceBinding.bindings.pop();
  assert.throws(() => validateCatalog(implicitGap), /覆盖全部 40 surface/);
});

test("catalog UI 扩展保持向后兼容并接受旧数字 cameraId", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  delete catalog.interactionCameras;
  for (const collection of [catalog.categories, catalog.components, catalog.surfaces]) {
    for (const item of collection) delete item.ui;
  }
  for (const option of catalog.options) delete option.ui;
  assert.doesNotThrow(() => validateCatalog(catalog));

  catalog.interactionCameras = [{
    cameraId: 4,
    legacyIndex: null,
    zone: "interior",
    order: 0,
    displayName: "旧驾驶位",
    iconUrl: "/camera-driver.svg"
  }];
  catalog.categories[0].ui = { cameraId: 4 };
  assert.doesNotThrow(() => validateCatalog(catalog));
});

test("启用色相排序的材料族要求每个 variant 提供合法 sortColorHex", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const sortedFamilyIds = new Set(
    catalog.materialFamilies
      .filter((family) => family.ui?.variantSort === "achromatic-then-rainbow")
      .map((family) => family.materialFamilyId)
  );
  const sortedVariants = catalog.materialVariants.filter(
    (variant) => sortedFamilyIds.has(variant.materialFamilyId)
  );
  assert.equal(sortedVariants.length, 336);
  assert.ok(sortedVariants.every((variant) =>
    /^#[0-9A-F]{6}$/.test(variant.ui?.sortColorHex ?? "")
  ));

  const missing = structuredClone(catalog);
  delete missing.materialVariants.find(
    (variant) => sortedFamilyIds.has(variant.materialFamilyId)
  ).ui;
  assert.throws(() => validateCatalog(missing), /缺少合法 ui\.sortColorHex/);

  const invalid = structuredClone(catalog);
  invalid.materialVariants.find(
    (variant) => sortedFamilyIds.has(variant.materialFamilyId)
  ).ui.sortColorHex = "#12345G";
  assert.throws(() => validateCatalog(invalid), /sortColorHex 非法/);
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
    assert.throws(() => validateConfiguration(value, catalog), /车型目录 v2 契约校验失败/);
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
  unsupported.selections["door-upper"] = "door-upper-microfiber-black";
  unsupported.customizations = {
    "door-upper": { materialVariantId: "microfiber-p16-np-3048" }
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
  assert.equal(result.totalPriceMinor, 24424000);
  assert.equal(result.quoteAllowed, false);
  assert.equal(result.lineItems.length, 40);
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
  assert.equal(catalog.surfaces.length, 40);
  assert.equal(catalog.selectionOrder.length, 40);
  assert.equal(catalog.surfaces.filter((surface) => !surface.required).length, 5);
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
  assert.equal(byId.get("seat-shell-custom").displayName, "自定义颜色");
  assert.equal(byId.get("seat-shell-custom").parameters.color.mode, "custom");
  assert.deepEqual(
    {
      displayName: byId.get("door-middle-microfiber").displayName,
      colorMode: byId.get("door-middle-microfiber").parameters.color.mode,
      unitPriceMinor: byId.get("door-middle-microfiber").pricing.unitPriceMinor,
      quantity: byId.get("door-middle-microfiber").pricing.quantity,
      isStandard: byId.get("door-middle-microfiber").pricing.isStandard,
      control: byId.get("door-middle-microfiber").ui.control
    },
    {
      displayName: "超纤皮（色彩拓展）",
      colorMode: "variant",
      unitPriceMinor: 40000,
      quantity: 2,
      isStandard: false,
      control: "material-strip"
    }
  );
  assert.ok(
    catalog.options
      .filter((option) => ["ultrasuede", "alcantara", "microfiber", "leather"]
        .includes(option.materialFamilyId)
        && option.parameters.color?.mode === "variant")
      .every((option) => option.ui?.control === "material-strip")
  );
  assert.ok(
    catalog.options
      .filter((option) => option.materialFamilyId === "woven-wool")
      .every((option) => option.ui?.control === "material-variant")
  );
  assert.deepEqual(
    catalog.components
      .filter((component) => component.categoryId === "personalization")
      .map((component) => component.displayName),
    ["饰件", "缝线与徽标", "操控与脚垫"]
  );
  assert.equal(
    catalog.categories.find((category) => category.categoryId === "personalization")
      .ui.navigationMode,
    "surfaces-as-components"
  );
  assert.deepEqual(
    catalog.materialFamilies
      .filter((family) => family.ui?.variantSort === "achromatic-then-rainbow")
      .map((family) => family.materialFamilyId),
    ["ultrasuede", "alcantara", "leather", "microfiber"]
  );
  assert.equal(
    catalog.materialFamilies.find((family) => family.materialFamilyId === "ultrasuede")
      .ui.defaultVariantId,
    "ultrasuede-p6-uf7"
  );
  assert.equal(
    catalog.materialFamilies.find((family) => family.materialFamilyId === "microfiber")
      .ui.defaultVariantId,
    "microfiber-p16-np-3048"
  );
  const microfiberVariants = catalog.materialVariants.filter(
    (variant) => variant.materialFamilyId === "microfiber"
  );
  assert.equal(microfiberVariants.length, 176);
  assert.ok(microfiberVariants.every(
    (variant) => !variant.displayName.includes("待复核")
      && !variant.sourceRefs[0].locator.includes("待复核")
  ));
  assert.deepEqual(
    microfiberVariants
      .filter((variant) => [
        "microfiber-p16-np-3048",
        "microfiber-p17-kj-p17-r01c1",
        "microfiber-p17-kj-p17-r30c2"
      ].includes(variant.variantId))
      .map((variant) => [variant.variantId, variant.displayName, variant.colorCode]),
    [
      ["microfiber-p16-np-3048", "NP-3048 暗夜黑", "NP-3048"],
      ["microfiber-p17-kj-p17-r01c1", "KJ-065 玉石白", "KJ-065"],
      ["microfiber-p17-kj-p17-r30c2", "KJ-008 曜夜黑", "KJ-008"]
    ]
  );
  assert.deepEqual(
    catalog.options
      .filter((option) => option.surfaceId === "wheel-color")
      .map((option) => option.displayName),
    ["亮银色", "哑光银", "黑色", "哑光黑", "枪灰", "哑光枪灰", "香槟金", "哑光香槟金", "古铜", "哑光古铜"]
  );
  assert.equal(byId.get("wheel-color-bright-silver").pricing.unitPriceMinor, 0);
  assert.ok(catalog.options
    .filter((option) => option.surfaceId === "wheel-color"
      && option.optionId !== "wheel-color-bright-silver")
    .every((option) => option.pricing.unitPriceMinor === 120000));
  assert.equal(byId.get("steering-addon-eva").colorCode, "#000000");
  assert.equal(byId.get("steering-addon-eva").thumbnailUrl, null);
  assert.equal(
    catalog.surfaces.find((surface) => surface.surfaceId === "interior-painted-parts").displayName,
    "内饰组件"
  );
  assert.equal(byId.get("interior-painted-spray").displayName, "黑色");
  assert.equal(byId.get("interior-painted-spray").parameters.color.mode, "custom");
  assert.equal(catalog.defaultSelections["door-sill"], "door-sill-none");
  assert.equal(byId.get("door-sill-none").displayName, "无口袋");
  assert.deepEqual(
    catalog.options.filter((option) => option.surfaceId === "embroidered-logo")
      .map((option) => [option.displayName, option.colorCode, option.pricing.unitPriceMinor]),
    [
      ["黑色", "#111111", 0],
      ["红色", "#D71920", 0],
      ["黄色", "#FFD400", 0],
      ["蓝色", "#1769E0", 0],
      ["绿色", "#159447", 0]
    ]
  );
  assert.equal(
    catalog.surfaces.find((surface) => surface.surfaceId === "embroidered-logo").displayName,
    "缝线"
  );
  assert.equal(
    catalog.surfaces.find((surface) => surface.surfaceId === "center-panel-trim").displayName,
    "中板缝线"
  );
  assert.equal(
    catalog.surfaces.find((surface) => surface.surfaceId === "door-panel-embroidery").displayName,
    "中板刺绣"
  );
  assert.deepEqual(
    catalog.options.filter((option) => option.surfaceId === "nameplate")
      .map((option) => option.displayName),
    ["铜", "不锈钢", "碳纤维"]
  );
  assert.equal(catalog.surfaces.some((surface) => surface.surfaceId === "brake-handle"), false);
  assert.deepEqual(
    ["headrest-embroidery", "door-panel-embroidery"].map((surfaceId) =>
      catalog.options.filter((option) => option.surfaceId === surfaceId)
        .map((option) => [option.displayName, option.pricing.unitPriceMinor])),
    [
      [["无刺绣", 0], ["头枕刺绣", 128800]],
      [["无刺绣", 0], ["中板刺绣", 168800]]
    ]
  );
  assert.equal(
    byId.get("headrest-embroidery-custom").thumbnailUrl,
    "/sc01/interior-parts/headrest-embroidery.webp"
  );
  assert.equal(
    byId.get("door-panel-embroidery-custom").thumbnailUrl,
    "/sc01/interior-parts/door-panel-embroidery.webp"
  );
  assert.deepEqual(
    catalog.options.filter((option) => option.surfaceId === "nameplate")
      .map((option) => option.thumbnailUrl),
    [
      "/sc01/interior-parts/nameplate-copper-preview.webp",
      "/sc01/interior-parts/nameplate-stainless-preview.webp",
      "/sc01/interior-parts/nameplate-carbon-preview.webp"
    ]
  );
  assert.equal(
    catalog.optionIdAliases["brake-handle-flamed-blue"],
    undefined
  );
  assert.equal(
    catalog.optionIdAliases["brake-handle-door-panel"],
    undefined
  );
  assert.deepEqual(
    catalog.options.filter((option) => option.surfaceId === "center-panel-trim")
      .map((option) => [option.displayName, option.pricing.unitPriceMinor, option.pricing.quantity]),
    [["黑色", 0, 1], ["自定义颜色", 30000, 2]]
  );
  assert.deepEqual(
    catalog.options.filter((option) => option.surfaceId === "pedal")
      .map((option) => option.displayName),
    ["豪车毯", "短绒毛", "豪车毯+金属板"]
  );
  assert.deepEqual(byId.get("rear-wing-gray").availability, {
    status: "disabled",
    reason: "暂不可选"
  });
});

test("禁用选项必须携带原因且不能进入有效配置", async () => {
  const catalog = await fixture("sc01.catalog.draft.v2.json");
  const invalidAvailability = structuredClone(catalog);
  invalidAvailability.options.find(
    (option) => option.optionId === "rear-wing-gray"
  ).availability.reason = "";
  assert.throws(() => validateCatalog(invalidAvailability), /availability 非法/);

  const configuration = await fixture("sc01.configuration.valid.v2.json");
  configuration.selections["rear-wing"] = "rear-wing-gray";
  assert.throws(
    () => validateConfiguration(configuration, catalog),
    /暂不可选/
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
