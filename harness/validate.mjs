import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, statSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const errors = [];
const warnings = [];

function fail(message) {
  errors.push(message);
}

function warn(message) {
  warnings.push(message);
}

function absolute(relativePath) {
  return resolve(root, relativePath.replaceAll("/", "\\"));
}

function requireFile(relativePath) {
  const path = absolute(relativePath);
  if (!existsSync(path) || !statSync(path).isFile()) {
    fail(`缺少 Harness 必需文件：${relativePath}`);
    return null;
  }
  return path;
}

function readJson(relativePath) {
  const path = requireFile(relativePath);
  if (!path) {
    return null;
  }
  try {
    return JSON.parse(readFileSync(path, "utf8"));
  } catch (error) {
    fail(`${relativePath} 不是有效 JSON：${error.message}`);
    return null;
  }
}

function expectEqual(actual, expected, label) {
  if (actual !== expected) {
    fail(`${label} 漂移：期望 ${JSON.stringify(expected)}，实际 ${JSON.stringify(actual)}`);
  }
}

function optionById(catalog, optionId) {
  const option = catalog.options?.find((entry) => entry.optionId === optionId);
  if (!option) {
    fail(`SC01 catalog 缺少关键 option：${optionId}`);
  }
  return option;
}

function validateMarkdownLinks(relativePath) {
  const path = requireFile(relativePath);
  if (!path) {
    return;
  }
  const text = readFileSync(path, "utf8");
  const linkPattern = /\[[^\]]*]\(([^)]+)\)/g;
  for (const match of text.matchAll(linkPattern)) {
    const target = match[1].trim();
    if (
      target.startsWith("http://") ||
      target.startsWith("https://") ||
      target.startsWith("#") ||
      target.startsWith("computer://")
    ) {
      continue;
    }
    const withoutAnchor = target.split("#", 1)[0];
    if (!withoutAnchor) {
      continue;
    }
    const decoded = decodeURIComponent(withoutAnchor);
    const resolved = resolve(dirname(path), decoded.replaceAll("/", "\\"));
    if (!existsSync(resolved)) {
      fail(`${relativePath} 包含失效相对链接：${target}`);
    }
  }
}

const requiredFiles = [
  "AGENTS.md",
  "harness/AGENTS.md",
  "harness/PROJECT.md",
  "harness/UE.md",
  "docs/AI_WORKFLOW.md",
  "docs/ARCHITECTURE.md",
  "docs/DECISIONS.md",
  "docs/PRODUCT_CONTEXT_SC01.md",
  "docs/REFERENCE_ASSET_POLICY.md",
  "contracts/README.md",
  "contracts/fixtures/sc01.catalog.draft.v2.json",
  "source/clients/ue/ConfigurationSystem.uproject",
  "source/clients/web/package.json",
  "source/server/package.json",
  "source/clients/web/public/sc01/option-icons/rainbow.svg",
  "tools/release.ps1",
  "docs/technical-probes/UNIFIED_RELEASE_PIPELINE.md",
];

for (const relativePath of requiredFiles) {
  requireFile(relativePath);
}

for (const relativePath of [
  "docs/HARNESS.md",
  "tools/validate-harness.mjs",
]) {
  if (existsSync(absolute(relativePath))) {
    fail(`Harness 文件必须集中在 harness/，请移除旧入口：${relativePath}`);
  }
}

for (const relativePath of [
  "harness/AGENTS.md",
  "harness/PROJECT.md",
  "harness/UE.md",
  "README.md",
  "CONTRIBUTING.md",
]) {
  validateMarkdownLinks(relativePath);
}

const rootAgentsPath = requireFile("AGENTS.md");
if (rootAgentsPath) {
  const rootAgents = readFileSync(rootAgentsPath, "utf8");
  for (const requiredText of [
    "harness/AGENTS.md",
    "tools/release.ps1",
    "-DryRun",
    "禁止用临时 `RunUAT`",
    "构建期间发生变化",
  ]) {
    if (!rootAgents.includes(requiredText)) {
      fail(`AGENTS.md 缺少启动门禁：${requiredText}`);
    }
  }
}

const agentsPath = requireFile("harness/AGENTS.md");
if (agentsPath) {
  const agents = readFileSync(agentsPath, "utf8");
  for (const requiredText of [
    "PROJECT.md",
    "UE.md",
    "git worktree list",
    "node harness/validate.mjs",
    "不确定的产品、资产或授权信息不得写入 Harness",
    "阶段验证通过后自动继续",
    "占位资源",
    "发布硬门禁",
    "tools/release.ps1",
    "-DryRun",
    "不得依赖",
  ]) {
    if (!agents.includes(requiredText)) {
      fail(`harness/AGENTS.md 缺少必需约束：${requiredText}`);
    }
  }
}

const harnessPath = requireFile("harness/PROJECT.md");
if (harnessPath) {
  const harness = readFileSync(harnessPath, "utf8");
  for (const requiredText of [
    "22980000",
    "正式模型尚未到位",
    "A 柱默认",
    "仪表台回中标",
    "座椅回中标",
    "高光原色碳纤维",
    "source-manifest.json",
    "UE 管理员模式",
  ]) {
    if (!harness.includes(requiredText)) {
      fail(`harness/PROJECT.md 缺少当前确定事实：${requiredText}`);
    }
  }
}

const ueHarnessPath = requireFile("harness/UE.md");
if (ueHarnessPath) {
  const ueHarness = readFileSync(ueHarnessPath, "utf8");
  for (const requiredText of [
    "DCC_VEHICLE_MODELING_EXPORT_GUIDE.md",
    "VEHICLE_ASSET_REQUIREMENTS.md",
    "PACKAGING_BOUNDARY_PROBE.md",
    "BAKE_RELEASE_VALIDATION.md",
    "Content/Configurator/_ImportStaging/",
    "BuildCookRun",
    "当前 active 版本不变",
    "车内禁止平移",
    "受限 Web/UE Bridge",
    "M_SC01_*",
    "tools/release.ps1",
    "-DryRun",
    "不要另写临时打包命令",
  ]) {
    if (!ueHarness.includes(requiredText)) {
      fail(`harness/UE.md 缺少 UE/资产/打包路由：${requiredText}`);
    }
  }
}

const catalog = readJson("contracts/fixtures/sc01.catalog.draft.v2.json");
if (catalog) {
  expectEqual(catalog.schemaVersion, "2.0.0", "SC01 schemaVersion");
  expectEqual(catalog.catalogVersion, "sc01-draft-20261007", "SC01 catalogVersion");
  expectEqual(catalog.lifecycle, "draft", "SC01 lifecycle");
  expectEqual(catalog.vehicle?.vehicleId, "sc01", "SC01 vehicleId");
  expectEqual(catalog.vehicle?.displayName, "SC01", "SC01 displayName");
  expectEqual(catalog.vehicle?.basePriceMinor, 22980000, "SC01 基础价");
  expectEqual(catalog.vehicle?.priceStatus, "confirmed", "SC01 基础价状态");
  expectEqual(catalog.vehicle?.quotable, false, "SC01 正式报价门禁");
  expectEqual(catalog.categories?.length, 4, "SC01 阶段数量");
  expectEqual(catalog.components?.length, 17, "SC01 部件数量");
  expectEqual(catalog.surfaces?.length, 40, "SC01 surface 数量");
  expectEqual(catalog.materialFamilies?.length, 17, "SC01 材料族数量");
  expectEqual(catalog.options?.length, 175, "SC01 option 数量");
  expectEqual(catalog.selectionOrder?.length, 40, "SC01 selectionOrder 数量");

  const aPillar = optionById(catalog, "a-pillar-woven");
  if (aPillar) {
    expectEqual(aPillar.displayName, "织布", "A 柱默认名称");
    expectEqual(aPillar.materialFamilyId, "woven-fabric", "A 柱默认材料族");
    expectEqual(aPillar.parameters?.color?.value, "#111111", "A 柱默认颜色");
    expectEqual(aPillar.pricing?.isStandard, true, "A 柱默认标配");
  }

  const roof = optionById(catalog, "roof-woven-standard");
  if (roof) {
    expectEqual(roof.displayName, "织布", "车顶默认名称");
    expectEqual(roof.materialFamilyId, "woven-fabric", "车顶默认材料族");
    expectEqual(roof.parameters?.color?.value, "#111111", "车顶默认颜色");
    expectEqual(roof.pricing?.isStandard, true, "车顶默认标配");
  }

  const ipDefault = optionById(catalog, "ip-center-mark-uncovered-black");
  if (ipDefault) {
    expectEqual(ipDefault.displayName, "无包覆(黑)", "仪表台回中标默认名称");
    expectEqual(ipDefault.pricing?.unitPriceMinor, 0, "仪表台回中标默认价格");
    expectEqual(ipDefault.pricing?.isStandard, true, "仪表台回中标默认标配");
  }
  for (const optionId of [
    "ip-center-mark-ultrasuede",
    "ip-center-mark-alcantara",
    "ip-center-mark-microfiber",
    "ip-center-mark-leather",
  ]) {
    const option = optionById(catalog, optionId);
    if (option) {
      expectEqual(option.pricing?.unitPriceMinor, 10000, `${optionId} 单价`);
    }
  }

  for (const optionId of [
    "seat-headrest-mark-ultrasuede",
    "seat-headrest-mark-alcantara",
    "seat-headrest-mark-microfiber",
    "seat-headrest-mark-leather",
  ]) {
    const option = optionById(catalog, optionId);
    if (option) {
      expectEqual(option.pricing?.unitPriceMinor, 0, `${optionId} 单价`);
    }
  }

  const seatShellDefault = optionById(catalog, "seat-shell-carbon-original");
  if (seatShellDefault) {
    expectEqual(seatShellDefault.displayName, "高光原色碳纤维", "座椅背板默认名称");
    expectEqual(seatShellDefault.finish, "gloss", "座椅背板默认表面");
    expectEqual(seatShellDefault.pricing?.isStandard, true, "座椅背板默认标配");
  }

  const seatShellCustom = optionById(catalog, "seat-shell-custom");
  if (seatShellCustom) {
    expectEqual(seatShellCustom.displayName, "自定义颜色", "座椅背板自定义名称");
    expectEqual(seatShellCustom.parameters?.color?.mode, "custom", "座椅背板取色模式");
    expectEqual(
      seatShellCustom.ui?.iconUrl,
      "/sc01/option-icons/rainbow.svg",
      "座椅背板自定义图标",
    );
    expectEqual(seatShellCustom.ui?.defaultParameters?.roughness, 0.18, "座椅背板默认亮面");
  }
}

const rainbowPath = requireFile("source/clients/web/public/sc01/option-icons/rainbow.svg");
if (rainbowPath) {
  const rainbow = readFileSync(rainbowPath, "utf8");
  if (!rainbow.includes("<rect") || !rainbow.includes('preserveAspectRatio="none"')) {
    fail("rainbow.svg 必须使用可撑满卡片的矩形图形");
  }
  if (rainbow.includes("<circle")) {
    fail("rainbow.svg 不得退回圆环或圆形图标");
  }
}

try {
  const tracked = execFileSync(
    "git",
    [
      "ls-files",
      "package/**",
      "source/**/Binaries/**",
      "source/**/Intermediate/**",
      "source/**/Saved/**",
      "source/**/node_modules/**",
      "source/**/dist/**",
      "*.log",
      "*.tmp",
    ],
    { cwd: root, encoding: "utf8" },
  )
    .split(/\r?\n/)
    .filter(Boolean);
  if (tracked.length > 0) {
    fail(`检测到不应受 Git 跟踪的生成物：${tracked.join(", ")}`);
  }
} catch (error) {
  warn(`无法执行 Git 生成物检查：${error.message}`);
}

try {
  const worktrees = execFileSync("git", ["worktree", "list", "--porcelain"], {
    cwd: root,
    encoding: "utf8",
  });
  const count = worktrees.split(/\r?\nworktree /).length;
  if (count > 1) {
    warn(`当前仓库有 ${count} 个工作树；开始任务前必须确认目标分支和提交归属`);
  }
} catch (error) {
  warn(`无法读取 Git worktree：${error.message}`);
}

for (const warning of warnings) {
  console.warn(`WARN: ${warning}`);
}

if (errors.length > 0) {
  for (const error of errors) {
    console.error(`ERROR: ${error}`);
  }
  console.error(`Harness 校验失败：${errors.length} 个错误，${warnings.length} 个警告`);
  process.exitCode = 1;
} else {
  console.log(`Harness 校验通过：${requiredFiles.length} 个入口文件，SC01 当前事实一致`);
  if (warnings.length > 0) {
    console.log(`附带 ${warnings.length} 个非阻断警告`);
  }
}
