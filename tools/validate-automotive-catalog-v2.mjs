import { createHash } from "node:crypto";
import { readdir, readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const CONFIGURATION_ID = /^cfg-[a-f0-9]{24}$/;
const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  throw new Error(`车型目录 v2 契约校验失败：${message}`);
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

function assertAllowedKeys(value, required, optional, label) {
  check(isRecord(value), `${label} 必须是 object`);
  const actual = Object.keys(value);
  check(required.every((key) => Object.hasOwn(value, key)), `${label} 缺少必填字段`);
  check(
    actual.every((key) => required.includes(key) || optional.includes(key)),
    `${label} 包含未知字段`
  );
}

function validateNodeUi(ui, cameras, animations, label) {
  if (ui === undefined) return;
  check(isRecord(ui), `${label}.ui 必须是 object`);
  check(
    ui.order === undefined || (Number.isInteger(ui.order) && ui.order >= 0),
    `${label}.ui.order 非法`
  );
  check(
    ui.iconUrl === undefined || ui.iconUrl === null
      || (typeof ui.iconUrl === "string" && ui.iconUrl.startsWith("/")),
    `${label}.ui.iconUrl 非法`
  );
  if (ui.cameraId !== undefined && ui.cameraId !== null) {
    check(
      cameras.has(`${typeof ui.cameraId}:${ui.cameraId}`),
      `${ui.cameraId} 引用了未知 interactionCamera`
    );
  }
  if (ui.animationId !== undefined && ui.animationId !== null) {
    check(
      typeof ui.animationId === "string" && animations.has(ui.animationId),
      `${ui.animationId} 引用了未知 animation`
    );
  }
  check(
    ui.navigationMode === undefined
      || ["tabs", "list", "none", "surfaces-as-components"].includes(ui.navigationMode),
    `${label}.ui.navigationMode 非法`
  );
  check(
    ui.layout === undefined || ["single", "stack", "grid"].includes(ui.layout),
    `${label}.ui.layout 非法`
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

function readWebpSize(image, label) {
  check(
    image.length >= 30
      && image.toString("ascii", 0, 4) === "RIFF"
      && image.toString("ascii", 8, 12) === "WEBP",
    `${label} 必须是 WebP`
  );
  const chunk = image.toString("ascii", 12, 16);
  if (chunk === "VP8X") {
    return {
      width: 1 + image.readUIntLE(24, 3),
      height: 1 + image.readUIntLE(27, 3)
    };
  }
  if (chunk === "VP8L") {
    check(image[20] === 0x2f, `${label} VP8L 头非法`);
    const bits = image.readUInt32LE(21);
    return {
      width: 1 + (bits & 0x3fff),
      height: 1 + ((bits >> 14) & 0x3fff)
    };
  }
  check(
    chunk === "VP8 "
      && image[23] === 0x9d
      && image[24] === 0x01
      && image[25] === 0x2a,
    `${label} VP8 头非法`
  );
  return {
    width: image.readUInt16LE(26) & 0x3fff,
    height: image.readUInt16LE(28) & 0x3fff
  };
}

const PAINT_KEYS = [
  "colorHex",
  "metallic",
  "roughness",
  "clearCoat",
  "orangePeel",
  "flakeIntensity"
];

function isGameObjectPath(value) {
  return typeof value === "string"
    && /^\/Game\/(?:[A-Za-z0-9_-]+\/)*[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/.test(value);
}

function canonicalCustomizationLines(catalog, customizations = {}, renderSurfaces) {
  return catalog.selectionOrder.flatMap((surfaceId) => {
    if (renderSurfaces && !renderSurfaces.has(surfaceId)) return [];
    const customization = customizations[surfaceId];
    if (!customization) return [];
    if (typeof customization.materialVariantId === "string") {
      return [`customizations.${surfaceId}.materialVariantId=${customization.materialVariantId}`];
    }
    return PAINT_KEYS.map(
      (key) => `customizations.${surfaceId}.${key}=${customization[key]}`
    );
  });
}

export function canonicalConfigurationInput(catalog, selections, customizations = {}) {
  return [
    "schemaVersion=2.0.0",
    `catalogVersion=${catalog.catalogVersion}`,
    `vehicleId=${catalog.vehicle.vehicleId}`,
    ...catalog.selectionOrder.flatMap(
      (surfaceId) => selections[surfaceId] ? [`${surfaceId}=${selections[surfaceId]}`] : []
    ),
    ...canonicalCustomizationLines(catalog, customizations)
  ].join("\n");
}

export function canonicalRenderInput(catalog, selections, customizations = {}) {
  const options = new Map(catalog.options.map((option) => [option.optionId, option]));
  const renderSurfaces = new Set();
  return [
    "schemaVersion=2.0.0",
    `catalogVersion=${catalog.catalogVersion}`,
    `vehicleId=${catalog.vehicle.vehicleId}`,
    ...catalog.selectionOrder.flatMap((surfaceId) => {
      const optionId = selections[surfaceId];
      if (options.get(optionId)?.renderRelevant !== true) return [];
      renderSurfaces.add(surfaceId);
      return [`${surfaceId}=${optionId}`];
    }),
    ...canonicalCustomizationLines(catalog, customizations, renderSurfaces)
  ].join("\n");
}

export function deriveConfigurationIdentity(catalog, selections, customizations = {}) {
  const canonicalInput = canonicalConfigurationInput(catalog, selections, customizations);
  const digest = createHash("sha256").update(canonicalInput, "utf8").digest("hex");
  const configurationId = `cfg-${digest.slice(0, 24)}`;
  const canonicalRender = canonicalRenderInput(catalog, selections, customizations);
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
  assertAllowedKeys(catalog, [
    "schemaVersion",
    "catalogVersion",
    "lifecycle",
    "currency",
    "vehicle",
    "selectionOrder",
    "defaultSelections",
    "optionIdAliases",
    "skeletalMeshPath",
    "sequencePath",
    "animations",
    "regions",
    "categories",
    "components",
    "surfaces",
    "materialFamilies",
    "materialVariants",
    "assetManifest",
    "options"
  ], ["interactionCameras"], "catalog");
  check(catalog.schemaVersion === "2.0.0", "catalog.schemaVersion 必须为 2.0.0");
  check(catalog.lifecycle === "draft", "车型目录 v2 当前仅允许 draft");
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
  check(catalog.vehicle.basePriceMinor === 22980000, "SC01 基础价必须为 22980000 分");
  check(catalog.vehicle.priceStatus === "confirmed", "基础价状态必须为 confirmed");
  check(catalog.vehicle.quotable === false, "草案车型必须禁止报价");
  check(isGameObjectPath(catalog.skeletalMeshPath),
    "catalog.skeletalMeshPath 必须是 /Game/ 下的完整对象路径");
  check(isGameObjectPath(catalog.sequencePath),
    "catalog.sequencePath 必须是 /Game/ 下的完整对象路径");

  const regions = uniqueIndex(catalog.regions, "regionId", "regions");
  const categories = uniqueIndex(catalog.categories, "categoryId", "categories");
  const components = uniqueIndex(catalog.components, "componentId", "components");
  const surfaces = uniqueIndex(catalog.surfaces, "surfaceId", "surfaces");
  const materialFamilies = uniqueIndex(
    catalog.materialFamilies,
    "materialFamilyId",
    "materialFamilies"
  );
  const materialVariants = uniqueIndex(
    catalog.materialVariants,
    "variantId",
    "materialVariants"
  );
  const options = uniqueIndex(catalog.options, "optionId", "options");
  const cameras = new Map();
  for (const camera of catalog.interactionCameras ?? []) {
    check(isRecord(camera), "interactionCamera 必须是 object");
    assertAllowedKeys(
      camera,
      ["cameraId", "zone", "order", "displayName", "iconUrl"],
      ["legacyIndex"],
      "interactionCamera"
    );
    check(
      (typeof camera.cameraId === "string" && ID.test(camera.cameraId))
        || (Number.isInteger(camera.cameraId) && camera.cameraId >= 0),
      "interactionCamera.cameraId 非法"
    );
    check(
      camera.legacyIndex === undefined
        || camera.legacyIndex === null
        || (Number.isInteger(camera.legacyIndex)
          && camera.legacyIndex >= 0
          && camera.legacyIndex <= 5),
      "interactionCamera.legacyIndex 非法"
    );
    const key = `${typeof camera.cameraId}:${camera.cameraId}`;
    check(!cameras.has(key), `interactionCameras 存在重复 cameraId：${camera.cameraId}`);
    check(typeof camera.zone === "string" && ID.test(camera.zone), "interactionCamera.zone 非法");
    check(Number.isInteger(camera.order) && camera.order >= 0, "interactionCamera.order 非法");
    check(typeof camera.displayName === "string" && camera.displayName.length > 0, "interactionCamera.displayName 非法");
    check(typeof camera.iconUrl === "string" && camera.iconUrl.startsWith("/"), "interactionCamera.iconUrl 非法");
    cameras.set(key, camera);
  }
  const animations = uniqueIndex(catalog.animations, "animationId", "animations");
  for (const animation of animations.values()) {
    assertExactKeys(animation, [
      "animationId",
      "displayName",
      "frameRate",
      "startFrame",
      "endFrame",
      "loopMode",
      "closeMode"
    ], `animation ${animation.animationId}`);
    check(typeof animation.displayName === "string" && animation.displayName.length > 0,
      `${animation.animationId}.displayName 非法`);
    check(typeof animation.frameRate === "number" && Number.isFinite(animation.frameRate)
      && animation.frameRate > 0, `${animation.animationId}.frameRate 非法`);
    check(Number.isInteger(animation.startFrame) && animation.startFrame >= 0,
      `${animation.animationId}.startFrame 非法`);
    check(Number.isInteger(animation.endFrame) && animation.endFrame > animation.startFrame,
      `${animation.animationId}.endFrame 非法`);
    check(["none", "forward", "ping-pong"].includes(animation.loopMode),
      `${animation.animationId}.loopMode 非法`);
    check(["reverse", "reset-to-start", "stop"].includes(animation.closeMode),
      `${animation.animationId}.closeMode 非法`);
  }
  check(isRecord(catalog.optionIdAliases), "optionIdAliases 必须是 object");
  for (const [legacyOptionId, optionId] of Object.entries(catalog.optionIdAliases)) {
    check(ID.test(legacyOptionId), `旧 optionId 非法：${legacyOptionId}`);
    check(!options.has(legacyOptionId), `旧 optionId 不得继续作为 option：${legacyOptionId}`);
    check(options.has(optionId), `旧 optionId ${legacyOptionId} 的迁移目标不存在`);
  }

  check(
    Array.isArray(catalog.selectionOrder)
      && catalog.selectionOrder.length === catalog.surfaces.length
      && new Set(catalog.selectionOrder).size === catalog.selectionOrder.length,
    "selectionOrder 必须按文档顺序完整覆盖所有 surfaceId"
  );
  for (const surfaceId of catalog.selectionOrder) {
    check(surfaces.has(surfaceId), `${surfaceId} 必须存在`);
  }
  check(isRecord(catalog.defaultSelections), "defaultSelections 必须是 object");
  for (const component of components.values()) {
    check(categories.has(component.categoryId), `${component.componentId} 引用了未知 categoryId`);
  }
  for (const category of categories.values()) {
    check(regions.has(category.regionId), `${category.categoryId} 引用了未知 regionId`);
  }
  for (const surface of surfaces.values()) {
    check(components.has(surface.componentId), `${surface.surfaceId} 引用了未知 componentId`);
  }
  for (const family of materialFamilies.values()) {
    if (family.ui === undefined) continue;
    assertAllowedKeys(
      family.ui,
      [],
      ["variantSort", "defaultVariantId"],
      `${family.materialFamilyId}.ui`
    );
    check(
      family.ui.variantSort === undefined
        || family.ui.variantSort === "achromatic-then-rainbow",
      `${family.materialFamilyId}.ui.variantSort 非法`
    );
    check(
      family.ui.defaultVariantId === undefined || ID.test(family.ui.defaultVariantId),
      `${family.materialFamilyId}.ui.defaultVariantId 非法`
    );
  }
  const sortedFamilyIds = new Set(
    [...materialFamilies.values()]
      .filter((family) => family.ui?.variantSort === "achromatic-then-rainbow")
      .map((family) => family.materialFamilyId)
  );
  for (const variant of materialVariants.values()) {
    if (variant.ui !== undefined) {
      assertAllowedKeys(variant.ui, [], ["sortColorHex"], `${variant.variantId}.ui`);
      check(
        variant.ui.sortColorHex === undefined
          || /^#[0-9a-fA-F]{6}$/.test(variant.ui.sortColorHex),
        `${variant.variantId}.ui.sortColorHex 非法`
      );
    }
    check(
      !sortedFamilyIds.has(variant.materialFamilyId)
        || /^#[0-9a-fA-F]{6}$/.test(variant.ui?.sortColorHex ?? ""),
      `${variant.variantId} 缺少合法 ui.sortColorHex`
    );
  }
  for (const family of materialFamilies.values()) {
    if (family.ui?.defaultVariantId === undefined) continue;
    check(
      materialVariants.get(family.ui.defaultVariantId)?.materialFamilyId
        === family.materialFamilyId,
      `${family.materialFamilyId}.ui.defaultVariantId 必须引用同材料族 variant`
    );
  }
  for (const item of [
    ...categories.values(),
    ...components.values(),
    ...surfaces.values()
  ]) {
    validateNodeUi(
      item.ui,
      cameras,
      animations,
      item.categoryId ?? item.componentId ?? item.surfaceId
    );
  }

  const optionIdsBySurface = new Map(
    [...surfaces.keys()].map((surfaceId) => [surfaceId, new Set()])
  );
  for (const option of options.values()) {
    check(surfaces.has(option.surfaceId), `${option.optionId} 引用了未知 surfaceId`);
    if (option.availability !== undefined) {
      assertExactKeys(
        option.availability,
        ["status", "reason"],
        `${option.optionId}.availability`
      );
      check(
        option.availability.status === "disabled"
          && typeof option.availability.reason === "string"
          && option.availability.reason.trim().length > 0,
        `${option.optionId}.availability 非法`
      );
    }
    if (option.requiresSelections !== undefined) {
      check(
        isRecord(option.requiresSelections)
          && Object.keys(option.requiresSelections).length > 0,
        `${option.optionId}.requiresSelections 必须是非空 object`
      );
      for (const [surfaceId, optionId] of Object.entries(option.requiresSelections)) {
        const requiredOption = options.get(optionId);
        check(
          surfaces.has(surfaceId) && requiredOption?.surfaceId === surfaceId,
          `${option.optionId}.requiresSelections 引用了未知或跨面的 option`
        );
        check(
          catalog.selectionOrder.indexOf(surfaceId)
            < catalog.selectionOrder.indexOf(option.surfaceId),
          `${option.optionId}.requiresSelections 只能依赖更早的 surface`
        );
      }
    }
    check(
      option.materialFamilyId === null || materialFamilies.has(option.materialFamilyId),
      `${option.optionId} 引用了未知 materialFamilyId`
    );
    check(isRecord(option.pricing), `${option.optionId}.pricing 必须是 object`);
    check(
      option.pricing.unitPriceMinor === null
        || (Number.isInteger(option.pricing.unitPriceMinor) && option.pricing.unitPriceMinor >= 0),
      `${option.optionId} unitPriceMinor 必须为 null 或非负整数`
    );
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
    if (option.ui !== undefined) {
      check(isRecord(option.ui), `${option.optionId}.ui 必须是 object`);
      check(
        option.ui.order === undefined
          || (Number.isInteger(option.ui.order) && option.ui.order >= 0),
        `${option.optionId}.ui.order 非法`
      );
      check(
        option.ui.iconUrl === undefined || option.ui.iconUrl === null
          || (typeof option.ui.iconUrl === "string" && option.ui.iconUrl.startsWith("/")),
        `${option.optionId}.ui.iconUrl 非法`
      );
      check(
        option.ui.control === undefined
          || ["swatch", "thumbnail", "color-picker", "material-variant", "material-strip"]
            .includes(option.ui.control),
        `${option.optionId}.ui.control 非法`
      );
      if (option.ui.defaultParameters !== undefined) {
        check(isRecord(option.ui.defaultParameters), `${option.optionId}.ui.defaultParameters 非法`);
        for (const [key, value] of Object.entries(option.ui.defaultParameters)) {
          check(PAINT_KEYS.includes(key), `${option.optionId}.ui.defaultParameters.${key} 未知`);
          check(
            key === "colorHex"
              ? typeof value === "string" && /^#[0-9A-Fa-f]{6}$/.test(value)
              : typeof value === "number" && value >= 0 && value <= 1,
            `${option.optionId}.ui.defaultParameters.${key} 非法`
          );
        }
      }
    }
    if (option.parameters?.color?.mode === "variant") {
      check(
        option.materialFamilyId !== null
          && option.pricing.unitPriceMinor !== null,
        `${option.optionId} 色卡能力只允许用于已定价且具有材料族的 option`
      );
    }
    check(
      option.pricing.status === (option.pricing.unitPriceMinor === null ? "unconfirmed" : "confirmed"),
      `${option.optionId} 价格状态与金额不一致`
    );
    check(option.pricing.quotable === false, `${option.optionId} 必须禁止报价`);
    optionIdsBySurface.get(option.surfaceId).add(option.optionId);
  }
  for (const variant of materialVariants.values()) {
    check(
      materialFamilies.has(variant.materialFamilyId),
      `${variant.variantId} 引用了未知 materialFamilyId`
    );
    check(
      typeof variant.thumbnailUrl === "string" && variant.thumbnailUrl.startsWith("/"),
      `${variant.variantId} thumbnailUrl 非法`
    );
    check(typeof variant.reviewRequired === "boolean", `${variant.variantId} reviewRequired 必须为 boolean`);
  }
  for (const surfaceId of catalog.selectionOrder) {
    check(optionIdsBySurface.get(surfaceId)?.size > 0, `${surfaceId} 至少需要一个选项`);
    const hasStandard = catalog.options.some(
      (option) => option.surfaceId === surfaceId && option.pricing.isStandard
    );
    check(
      surfaces.get(surfaceId).required === hasStandard,
      `${surfaceId} 的 required 必须与显式标配一致`
    );
    const hasDefault = Object.hasOwn(catalog.defaultSelections, surfaceId);
    check(
      hasDefault === surfaces.get(surfaceId).required,
      "defaultSelections 必须恰好覆盖全部必选 surface"
    );
    if (hasDefault) {
      const defaultOption = options.get(catalog.defaultSelections[surfaceId]);
      check(
        defaultOption?.surfaceId === surfaceId && defaultOption.pricing.isStandard,
        `${surfaceId} 的 defaultSelections 必须引用同 surface 的标配 option`
      );
    }
  }
  check(
    Object.keys(catalog.defaultSelections).every((surfaceId) => surfaces.has(surfaceId)),
    "defaultSelections 包含未知 surfaceId"
  );

  return {
    regions,
    categories,
    components,
    surfaces,
    materialFamilies,
    materialVariants,
    options,
    optionIdsBySurface
  };
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
    "selections",
    "customizations"
  ], "configuration");
  check(configuration.schemaVersion === "2.0.0", "configuration.schemaVersion 必须为 2.0.0");
  check(configuration.catalogVersion === catalog.catalogVersion, "catalogVersion 不匹配");
  check(configuration.vehicleId === catalog.vehicle.vehicleId, "vehicleId 不匹配");
  check(isRecord(configuration.selections), "selections 必须是 object");
  check(
    Object.keys(configuration.selections).every((surfaceId) => indexes.surfaces.has(surfaceId)),
    "selections 包含未知 surfaceId"
  );

  for (const surfaceId of catalog.selectionOrder) {
    const optionId = configuration.selections[surfaceId];
    if (optionId === undefined && indexes.surfaces.get(surfaceId).required === false) continue;
    check(
      indexes.optionIdsBySurface.get(surfaceId)?.has(optionId),
      `${surfaceId} 引用了未知或跨面的 optionId：${optionId}`
    );
    const option = indexes.options.get(optionId);
    check(
      option.availability?.status !== "disabled",
      `${surfaceId} 所选 option 暂不可选`
    );
    check(
      Object.entries(option.requiresSelections ?? {}).every(
        ([requiredSurfaceId, requiredOptionId]) =>
          configuration.selections[requiredSurfaceId] === requiredOptionId
      ),
      `${surfaceId} 所选 option 的 requiresSelections 不满足`
    );
  }

  check(isRecord(configuration.customizations), "customizations 必须是 object");
  for (const [surfaceId, customization] of Object.entries(configuration.customizations)) {
    check(catalog.selectionOrder.includes(surfaceId), `customizations 包含未知 surfaceId：${surfaceId}`);
    check(isRecord(customization), `${surfaceId} customization 必须是 object`);
    const option = indexes.options.get(configuration.selections[surfaceId]);
    if (Object.hasOwn(customization, "materialVariantId")) {
      assertExactKeys(customization, ["materialVariantId"], `${surfaceId} customization`);
      const variant = indexes.materialVariants.get(customization.materialVariantId);
      check(variant, `${surfaceId} 引用了未知 materialVariantId`);
      check(
        option?.parameters?.color?.mode === "variant"
          && option.pricing.isStandard === false
          && option.pricing.unitPriceMinor !== null,
        `${surfaceId} 所选 option 不支持材料色卡`
      );
      check(
        option.materialFamilyId === variant.materialFamilyId,
        `${surfaceId} 的材料色卡与所选选项材料族不匹配`
      );
    } else {
      assertExactKeys(customization, PAINT_KEYS, `${surfaceId} customization`);
      check(option.parameters?.color?.mode === "custom", `${surfaceId} 不支持自定义色`);
      check(/^#[0-9A-F]{6}$/.test(customization.colorHex), `${surfaceId}.colorHex 非法`);
      for (const key of PAINT_KEYS.slice(1)) {
        check(
          typeof customization[key] === "number"
            && Number.isFinite(customization[key])
            && customization[key] >= 0
            && customization[key] <= 1,
          `${surfaceId}.${key} 必须在 0 到 1 之间`
        );
      }
    }
  }

  const expected = deriveConfigurationIdentity(
    catalog,
    configuration.selections,
    configuration.customizations
  );
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
    basePriceMinor: catalog.vehicle.basePriceMinor,
    lineItems: catalog.surfaces.flatMap(({ surfaceId }) => {
      const optionId = configuration.selections[surfaceId];
      if (!optionId) return [];
      const option = options.get(optionId);
      return [{
        surfaceId,
        optionId: option.optionId,
        unitPriceMinor: option.pricing.unitPriceMinor,
        quantity: option.pricing.quantity,
        subtotalMinor: option.pricing.unitPriceMinor === null
          ? null
          : option.pricing.unitPriceMinor * option.pricing.quantity,
        priceStatus: option.pricing.status
      }];
    }),
    totalPriceMinor: catalog.vehicle.basePriceMinor
      + catalog.selectionOrder.reduce((total, surfaceId) => {
        const option = options.get(configuration.selections[surfaceId]);
        return total + (
          option?.pricing.unitPriceMinor === null || option?.pricing.unitPriceMinor === undefined
            ? 0
            : option.pricing.unitPriceMinor * (option.pricing.quantity ?? 1)
        );
      }, 0),
    quoteAllowed: false,
    blockingReasons: ["PRICE_UNCONFIRMED"]
  };
}

export function validatePriceResult(result, configuration, catalog) {
  const expected = buildPriceResult(configuration, catalog);
  check(
    JSON.stringify(result) === JSON.stringify(expected),
    "price-result 必须包含基础价、选装明细、参考总价并明确禁止报价"
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
    "regions",
    "categories",
    "components",
    "surfaces",
    "materialFamilies",
    "materialVariants",
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

export async function validateCropManifest(catalog, manifest, publicRoot) {
  check(isRecord(manifest), "crop manifest 根节点必须是 object");
  assertExactKeys(manifest, [
    "schemaVersion",
    "vehicleId",
    "generatedFrom",
    "generator",
    "target",
    "itemCount",
    "totalBytes",
    "items"
  ], "crop manifest");
  check(manifest.schemaVersion === "2.0.0", "crop manifest schemaVersion 必须为 2.0.0");
  check(manifest.vehicleId === catalog.vehicle.vehicleId, "crop manifest vehicleId 不匹配");
  check(
    [
      "tools/generate-catalog-thumbnails.py",
      "tools/generate-sc01-thumbnails.py"
    ].includes(manifest.generator),
    "crop manifest generator 非法"
  );
  check(
    isRecord(manifest.target)
      && manifest.target.width === 512
      && manifest.target.height === 512
      && manifest.target.format === "webp"
      && Number.isInteger(manifest.target.quality)
      && manifest.target.quality >= 80
      && manifest.target.quality <= 100
      && manifest.target.method === 6
      && manifest.target.replaceable === true,
    "crop manifest target 必须是 512x512 高质量 WebP"
  );
  check(
    Array.isArray(manifest.items)
      && manifest.itemCount === manifest.items.length
      && manifest.itemCount === catalog.materialVariants.length,
    "crop manifest itemCount 必须与 catalog.materialVariants 一致"
  );

  const thumbnails = resolve(publicRoot, "sc01", "thumbnails");
  const files = await readdir(thumbnails);
  const expectedFiles = new Set();
  const variants = new Map(
    catalog.materialVariants.map((variant) => [variant.variantId, variant])
  );
  let totalBytes = 0;

  for (const item of manifest.items) {
    check(isRecord(item), "crop manifest item 必须是 object");
    const variant = variants.get(item.variantId);
    check(variant, `crop manifest 包含未知 variantId：${item.variantId}`);
    check(variant.thumbnailUrl === item.output, `${item.variantId} catalog URL 与 manifest 不一致`);
    check(
      typeof item.output === "string"
        && item.output === `/sc01/thumbnails/${item.variantId}.webp`,
      `${item.variantId} output 必须使用约定 WebP URL`
    );
    check(isRecord(item.sourcePage), `${item.variantId} 缺少 sourcePage`);
    check(
      typeof item.sourcePage.file === "string"
        && item.sourcePage.file.length > 0
        && Number.isInteger(item.sourcePage.width)
        && item.sourcePage.width > 0
        && Number.isInteger(item.sourcePage.height)
        && item.sourcePage.height > 0
        && /^[a-f0-9]{64}$/.test(item.sourcePage.sha256),
      `${item.variantId} sourcePage 非法`
    );
    check(isRecord(item.crop), `${item.variantId} 缺少 crop`);
    check(
      ["x", "y", "width", "height"].every(
        (key) => Number.isInteger(item.crop[key]) && item.crop[key] >= (key === "x" || key === "y" ? 0 : 1)
      )
        && item.crop.width === item.crop.height
        && item.crop.x + item.crop.width <= item.sourcePage.width
        && item.crop.y + item.crop.height <= item.sourcePage.height,
      `${item.variantId} crop 越界或不是正方形`
    );
    check(
      isRecord(item.outputSize)
        && item.outputSize.width === 512
        && item.outputSize.height === 512,
      `${item.variantId} outputSize 非法`
    );
    check(Number.isInteger(item.byteLength) && item.byteLength > 0, `${item.variantId} byteLength 非法`);
    check(/^[a-f0-9]{64}$/.test(item.sha256), `${item.variantId} sha256 非法`);
    check(item.replaceable === true, `${item.variantId} 必须可替换`);
    check(
      item.reviewRequired === variant.reviewRequired,
      `${item.variantId} reviewRequired 与 catalog 不一致`
    );

    const relative = item.output.slice(1);
    const image = await readFile(resolve(publicRoot, relative));
    check(image.length === item.byteLength, `${item.variantId} byteLength 不匹配`);
    check(
      createHash("sha256").update(image).digest("hex") === item.sha256,
      `${item.variantId} sha256 不匹配`
    );
    const dimensions = readWebpSize(image, item.variantId);
    check(
      dimensions.width === 512 && dimensions.height === 512,
      `${item.variantId} 必须是 512x512`
    );
    totalBytes += image.length;
    expectedFiles.add(`${item.variantId}.webp`);
  }

  check(
    files.length === expectedFiles.size && files.every((file) => expectedFiles.has(file)),
    "thumbnails 目录必须只包含 manifest 声明的 WebP"
  );
  check(totalBytes === manifest.totalBytes, "crop manifest totalBytes 不匹配");
  check(totalBytes < 64 * 1024 * 1024, "WebP 总体积必须低于 64 MiB");
  return { itemCount: manifest.itemCount, totalBytes };
}

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

export async function validateAutomotiveCatalogFixtures(base = root) {
  const fixtures = resolve(base, "contracts", "fixtures");
  const catalog = await readJson(resolve(fixtures, "sc01.catalog.draft.v2.json"));
  const valid = await readJson(resolve(fixtures, "sc01.configuration.valid.v2.json"));
  const invalid = await readJson(resolve(fixtures, "sc01.configuration.invalid.v2.json"));
  const priceResult = await readJson(resolve(fixtures, "sc01.price-result.v2.json"));
  const golden = await readJson(resolve(fixtures, "sc01.identity-golden.v2.json"));
  const sourceManifest = await readJson(
    resolve(base, "docs", "product-data", "sc01", "source-manifest.json")
  );
  const publicRoot = resolve(base, "source", "clients", "web", "public");
  const cropManifest = await readJson(resolve(publicRoot, "sc01", "crop-manifest.json"));

  validateCatalog(catalog);
  validateSourceReferences(catalog, sourceManifest);
  await validateCropManifest(catalog, cropManifest, publicRoot);
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
    assertExactKeys(vector.selections, catalog.selectionOrder, `黄金向量 ${index}.selections`);
    const derived = deriveConfigurationIdentity(
      catalog,
      vector.selections,
      vector.customizations ?? {}
    );
    check(
      derived.configurationId === vector.configurationId
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
    const result = await validateAutomotiveCatalogFixtures();
    console.log(
      `SC01 v2 契约验证通过：${result.optionCount} 个草案选项、`
      + `有效/无效配置、price-result 禁止报价、${result.vectorCount} 个稳定身份黄金向量。`
    );
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
