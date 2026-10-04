import { readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { deriveConfigurationIdentity } from "./validate-automotive-catalog-v2.mjs";

const DEFAULT_SECONDS_PER_RENDER = 30;
const DEFAULT_RENDER_VIEW_IDS = ["front", "front-left", "side", "rear-right"];
const DEFAULT_VIEW_COUNT = DEFAULT_RENDER_VIEW_IDS.length;

function orderedV1Parts(catalog) {
  const parts = [...(catalog.parts ?? [])].sort(
    (left, right) => left.displayOrder - right.displayOrder
  );
  if (parts.length === 0 || parts.some((part) => !part.options?.length)) {
    throw new Error("catalog 每个分区都必须至少包含一个选项");
  }
  return parts;
}

export function enumerateValidConfigurations(catalog) {
  if (catalog.schemaVersion !== "1.0.0") {
    throw new Error("exhaustive 仅支持 v1 catalog；v2 请使用 coverage、estimate 或 shard");
  }
  const parts = orderedV1Parts(catalog);
  const order = parts.map((part) => part.partId);
  const configurations = [];
  const visit = (index, selections, totalPriceMinor) => {
    if (index === parts.length) {
      configurations.push({
        configurationKey: order.map((partId) => selections[partId]).join("__"),
        selections: { ...selections },
        totalPriceMinor
      });
      return;
    }
    const part = parts[index];
    for (const option of part.options) {
      visit(
        index + 1,
        { ...selections, [part.partId]: option.optionId },
        totalPriceMinor + option.priceDeltaMinor
      );
    }
  };
  visit(0, {}, catalog.vehicle.basePriceMinor);
  return { order, configurations };
}

export function generatePublishedConfigurations(catalog, publicationVersion) {
  const { order, configurations } = enumerateValidConfigurations(catalog);
  const renderViewIds = (catalog.renderViews ?? []).map((view) => view.renderViewId);
  if (renderViewIds.length === 0) {
    throw new Error("catalog 必须至少声明一个 RenderView");
  }
  return {
    schemaVersion: "1.0.0",
    publicationVersion,
    catalogVersion: catalog.catalogVersion,
    vehicleId: catalog.vehicle.vehicleId,
    configurationKeyOrder: order,
    renderViewIds,
    expectedRenderCount: configurations.length * renderViewIds.length,
    configurations
  };
}

function indexV2Options(catalog) {
  if (catalog.schemaVersion !== "2.0.0") {
    throw new Error("coverage/estimate/shard 仅支持 v2 catalog");
  }
  const bySurface = new Map(catalog.selectionOrder.map((surfaceId) => [surfaceId, []]));
  for (const option of catalog.options ?? []) {
    if (!bySurface.has(option.surfaceId)) {
      throw new Error(`${option.optionId} 引用了 selectionOrder 外的 surface`);
    }
    bySurface.get(option.surfaceId).push(option);
  }
  if ([...bySurface.values()].some((options) => options.length === 0)) {
    throw new Error("v2 catalog 每个 surface 都必须至少包含一个选项");
  }
  return bySurface;
}

export function estimateV2Scale(
  catalog,
  { viewCount = DEFAULT_VIEW_COUNT, secondsPerRender = DEFAULT_SECONDS_PER_RENDER } = {}
) {
  if (!Number.isInteger(viewCount) || viewCount < 1) {
    throw new Error("viewCount 必须是正整数");
  }
  if (!Number.isFinite(secondsPerRender) || secondsPerRender <= 0) {
    throw new Error("secondsPerRender 必须是正数");
  }
  const bySurface = indexV2Options(catalog);
  let configurationCount = 1n;
  for (const options of bySurface.values()) {
    configurationCount *= BigInt(options.length);
  }
  const renderCount = configurationCount * BigInt(viewCount);
  const estimatedSeconds = renderCount * BigInt(Math.ceil(secondsPerRender));
  return {
    configurationCount: configurationCount.toString(),
    renderCount: renderCount.toString(),
    viewCount,
    secondsPerRender,
    estimatedSeconds: estimatedSeconds.toString(),
    estimatedYears: Number(estimatedSeconds / 31_556_952n).toLocaleString("en-US")
  };
}

function baselineV2Selections(catalog, bySurface) {
  return Object.fromEntries(catalog.selectionOrder.map((surfaceId) => [
    surfaceId,
    catalog.defaultSelections?.[surfaceId] ?? bySurface.get(surfaceId)[0].optionId
  ]));
}

export function generateV2Coverage(catalog) {
  const bySurface = indexV2Options(catalog);
  const baseline = baselineV2Selections(catalog, bySurface);
  const renderRelevant = (catalog.options ?? []).filter(
    (option) => option.renderRelevant === true
  );
  const uniqueSelections = new Map();
  for (const option of renderRelevant) {
    const selections = { ...baseline, [option.surfaceId]: option.optionId };
    const identity = deriveConfigurationIdentity(catalog, selections);
    uniqueSelections.set(identity.renderKey, {
      configurationKey: identity.renderKey,
      configurationId: identity.configurationId,
      renderKey: identity.renderKey,
      selections,
      customizations: {}
    });
  }
  const variantOptions = (catalog.options ?? []).filter(
    (option) =>
      option.renderRelevant === true
      && option.parameters?.color?.mode === "variant"
      && option.pricing?.isStandard === false
      && option.pricing?.unitPriceMinor !== null
  );
  const availableMaterialVariants = [];
  for (const variant of catalog.materialVariants ?? []) {
    const option = variantOptions.find(
      (candidate) => candidate.materialFamilyId === variant.materialFamilyId
    );
    if (!option) continue;
    availableMaterialVariants.push(variant);
    const selections = { ...baseline, [option.surfaceId]: option.optionId };
    const customizations = {
      [option.surfaceId]: { materialVariantId: variant.variantId }
    };
    const identity = deriveConfigurationIdentity(catalog, selections, customizations);
    uniqueSelections.set(identity.renderKey, {
      configurationKey: identity.renderKey,
      configurationId: identity.configurationId,
      renderKey: identity.renderKey,
      selections,
      customizations
    });
  }
  const configurations = [...uniqueSelections.values()];
  const covered = new Set(
    configurations.flatMap((configuration) => Object.values(configuration.selections))
  );
  const uncovered = renderRelevant.filter((option) => !covered.has(option.optionId));
  if (uncovered.length > 0) {
    throw new Error(`coverage 未覆盖 renderRelevant 选项：${uncovered.map((o) => o.optionId).join(", ")}`);
  }
  const coveredVariants = new Set(
    configurations.flatMap((configuration) =>
      Object.values(configuration.customizations)
        .map((customization) => customization.materialVariantId)
        .filter(Boolean)
    )
  );
  const uncoveredVariants = availableMaterialVariants.filter(
    (variant) => !coveredVariants.has(variant.variantId)
  );
  if (uncoveredVariants.length > 0) {
    throw new Error(
      `coverage 未覆盖可用 materialVariants：${uncoveredVariants.map((v) => v.variantId).join(", ")}`
    );
  }
  return {
    configurations,
    renderRelevantOptionCount: renderRelevant.length,
    coveredRenderRelevantOptionCount: renderRelevant.length,
    availableMaterialVariantCount: availableMaterialVariants.length,
    coveredMaterialVariantCount: coveredVariants.size
  };
}

export function shardV2Coverage(configurations, shardIndex, shardCount) {
  if (!Number.isInteger(shardCount) || shardCount < 1) {
    throw new Error("shardCount 必须是正整数");
  }
  if (!Number.isInteger(shardIndex) || shardIndex < 0 || shardIndex >= shardCount) {
    throw new Error("shardIndex 必须位于 [0, shardCount) 范围");
  }
  return configurations.filter((_, index) => index % shardCount === shardIndex);
}

export function generateV2Plan(
  catalog,
  publicationVersion,
  {
    mode = "coverage",
    shardIndex = 0,
    shardCount = 1,
    viewCount = DEFAULT_VIEW_COUNT,
    secondsPerRender = DEFAULT_SECONDS_PER_RENDER
  } = {}
) {
  if (!["coverage", "shard"].includes(mode)) {
    throw new Error("v2 输出模式必须是 coverage 或 shard");
  }
  const scale = estimateV2Scale(catalog, { viewCount, secondsPerRender });
  const coverage = generateV2Coverage(catalog);
  const configurations = mode === "shard"
    ? shardV2Coverage(coverage.configurations, shardIndex, shardCount)
    : coverage.configurations;
  const expectedRenderCount = configurations.length * viewCount;
  if (viewCount !== DEFAULT_RENDER_VIEW_IDS.length) {
    throw new Error(
      `v2 Bake 当前必须使用 ${DEFAULT_RENDER_VIEW_IDS.length} 个标准 RenderView`
    );
  }
  return {
    schemaVersion: "2.0.0",
    publicationVersion,
    catalogVersion: catalog.catalogVersion,
    vehicleId: catalog.vehicle.vehicleId,
    renderViewIds: [...DEFAULT_RENDER_VIEW_IDS],
    strategy: mode,
    shard: mode === "shard" ? { index: shardIndex, count: shardCount } : null,
    exhaustiveScale: scale,
    coverage: {
      renderRelevantOptionCount: coverage.renderRelevantOptionCount,
      coveredRenderRelevantOptionCount: coverage.coveredRenderRelevantOptionCount,
      availableMaterialVariantCount: coverage.availableMaterialVariantCount,
      coveredMaterialVariantCount: coverage.coveredMaterialVariantCount,
      totalConfigurationCount: coverage.configurations.length
    },
    expectedRenderCount,
    estimatedSeconds: String(Math.ceil(expectedRenderCount * secondsPerRender)),
    configurations
  };
}

function parseCli(args) {
  const positional = [];
  const options = {};
  for (let index = 0; index < args.length; index += 1) {
    const arg = args[index];
    if (!arg.startsWith("--")) {
      positional.push(arg);
      continue;
    }
    const [name, inlineValue] = arg.slice(2).split("=", 2);
    const value = inlineValue ?? args[++index];
    options[name] = value;
  }
  return { positional, options };
}

async function main() {
  const { positional, options } = parseCli(process.argv.slice(2));
  const catalogPath = resolve(positional[0] ?? "contracts/fixtures/catalog.mvp.json");
  const outputPath = resolve(
    positional[1] ?? "contracts/fixtures/published-configurations.mvp.json"
  );
  const catalog = JSON.parse(await readFile(catalogPath, "utf8"));
  const publicationVersion = positional[2] ?? catalog.catalogVersion;
  const defaultMode = catalog.schemaVersion === "1.0.0" ? "exhaustive" : "estimate";
  const mode = options.mode ?? defaultMode;
  const viewCount = Number(options.views ?? DEFAULT_VIEW_COUNT);
  const secondsPerRender = Number(options["seconds-per-render"] ?? DEFAULT_SECONDS_PER_RENDER);

  if (catalog.schemaVersion === "1.0.0") {
    if (mode !== "exhaustive") throw new Error("v1 catalog 仅支持 --mode exhaustive");
    const published = generatePublishedConfigurations(catalog, publicationVersion);
    await writeFile(outputPath, `${JSON.stringify(published, null, 2)}\n`, "utf8");
    console.log(
      `v1 exhaustive：${published.configurations.length} 个配置，`
        + `${published.expectedRenderCount} 个渲染任务：${outputPath}`
    );
    return;
  }

  const scale = estimateV2Scale(catalog, { viewCount, secondsPerRender });
  if (mode === "exhaustive") {
    throw new Error(
      `禁止直接展开 v2 的 ${scale.configurationCount} 个组合；`
        + "请使用 --mode estimate、coverage 或 shard"
    );
  }
  if (mode === "estimate") {
    console.log(JSON.stringify(scale, null, 2));
    return;
  }
  const [shardIndex, shardCount] = String(options.shard ?? "0/1")
    .split("/")
    .map(Number);
  const plan = generateV2Plan(catalog, publicationVersion, {
    mode,
    shardIndex,
    shardCount,
    viewCount,
    secondsPerRender
  });
  await writeFile(outputPath, `${JSON.stringify(plan, null, 2)}\n`, "utf8");
  console.log(
    `v2 ${mode}：全空间 ${scale.configurationCount} 个组合；输出 `
      + `${plan.configurations.length}/${plan.coverage.totalConfigurationCount} 个覆盖配置，`
      + `${plan.expectedRenderCount} 个渲染任务，预计 ${plan.estimatedSeconds} 秒：${outputPath}`
  );
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  main().catch((error) => {
    console.error(error instanceof Error ? error.message : String(error));
    process.exitCode = 1;
  });
}
