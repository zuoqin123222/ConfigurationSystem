import { createHash } from "node:crypto";
import {
  cp,
  mkdir,
  rename,
  rm,
  writeFile,
} from "node:fs/promises";
import {
  existsSync,
  readFileSync,
  realpathSync,
  statSync,
} from "node:fs";
import { dirname, isAbsolute, relative, resolve, sep } from "node:path";
import { inflateSync } from "node:zlib";

export interface BakeRender {
  configurationKey: string;
  renderViewId: string;
  path: string;
  width: number;
  height: number;
  format: "png";
  colorSpace: "sRGB";
  alphaMode: "straight";
  coverageInverted: boolean;
  normalizationRequired: boolean;
  glowRecoveredPixels: number;
  sha256: string;
  status: "ready";
}

export interface BakeManifest {
  schemaVersion: "1.0.0";
  manifestVersion: string;
  catalogVersion: string;
  publicationVersion: string;
  vehicleId: string;
  generatedAt: string;
  renderer: {
    engineVersion: string;
    mode: "path-tracing";
    samplesPerPixel: number;
  };
  alphaProcessing: {
    alphaMode: "straight";
    autoDetectCoverageInversion: boolean;
    clearTransparentRgb: boolean;
    glowPolicy: "synthetic-alpha" | "separate-layer";
  };
  renders: BakeRender[];
}

export interface ValidatedBakeManifest {
  manifest: BakeManifest;
  manifestPath: string;
  assetRoot: string;
  renders: ReadonlyMap<string, BakeRender>;
}

const ID = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const CONFIGURATION_KEY =
  /^paint-[a-z0-9-]+__wheel-[a-z0-9-]+__interior-[a-z0-9-]+__frame-[a-z0-9-]+$/;
const VIEWS = new Set(["front", "front-left", "side", "rear-right"]);
const PNG_SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(`bake manifest 校验失败：${message}`);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function renderKey(configurationKey: string, renderViewId: string): string {
  return `${configurationKey}\0${renderViewId}`;
}

function safeAssetPath(assetRoot: string, manifestPath: string): string {
  assert(!isAbsolute(manifestPath), `图片路径不能是绝对路径：${manifestPath}`);
  const root = realpathSync(assetRoot);
  const candidate = realpathSync(resolve(root, manifestPath));
  const fromRoot = relative(root, candidate);
  assert(
    fromRoot !== "" &&
      fromRoot !== ".." &&
      !fromRoot.startsWith(`..${sep}`) &&
      !isAbsolute(fromRoot),
    `图片路径越界：${manifestPath}`,
  );
  assert(statSync(candidate).isFile(), `图片不是文件：${manifestPath}`);
  return candidate;
}

function paeth(left: number, up: number, upperLeft: number): number {
  const estimate = left + up - upperLeft;
  const leftDistance = Math.abs(estimate - left);
  const upDistance = Math.abs(estimate - up);
  const upperLeftDistance = Math.abs(estimate - upperLeft);
  if (leftDistance <= upDistance && leftDistance <= upperLeftDistance) return left;
  return upDistance <= upperLeftDistance ? up : upperLeft;
}

function inspectPng(bytes: Buffer): {
  width: number;
  height: number;
  hasTransparentPixel: boolean;
  hasVisiblePixel: boolean;
} {
  assert(bytes.subarray(0, 8).equals(PNG_SIGNATURE), "文件不是 PNG");
  let offset = 8;
  let width = 0;
  let height = 0;
  let colorType = -1;
  let bitDepth = -1;
  let interlace = -1;
  const compressed: Buffer[] = [];

  while (offset + 12 <= bytes.length) {
    const length = bytes.readUInt32BE(offset);
    const type = bytes.toString("ascii", offset + 4, offset + 8);
    const dataStart = offset + 8;
    const dataEnd = dataStart + length;
    assert(dataEnd + 4 <= bytes.length, "PNG chunk 被截断");
    if (type === "IHDR") {
      width = bytes.readUInt32BE(dataStart);
      height = bytes.readUInt32BE(dataStart + 4);
      bitDepth = bytes[dataStart + 8] ?? -1;
      colorType = bytes[dataStart + 9] ?? -1;
      interlace = bytes[dataStart + 12] ?? -1;
    } else if (type === "IDAT") {
      compressed.push(bytes.subarray(dataStart, dataEnd));
    }
    offset = dataEnd + 4;
    if (type === "IEND") break;
  }

  assert(width > 0 && height > 0, "PNG 缺少有效 IHDR");
  assert(bitDepth === 8 && colorType === 6, "PNG 必须是 8-bit RGBA");
  assert(interlace === 0, "PNG 必须使用非交错编码");
  assert(compressed.length > 0, "PNG 缺少 IDAT");

  const raw = inflateSync(Buffer.concat(compressed));
  const stride = width * 4;
  assert(raw.length === height * (stride + 1), "PNG 解压尺寸不一致");
  let previous = Buffer.alloc(stride);
  let cursor = 0;
  let hasTransparentPixel = false;
  let hasVisiblePixel = false;

  for (let y = 0; y < height; y += 1) {
    const filter = raw[cursor++]!;
    const row = Buffer.alloc(stride);
    for (let x = 0; x < stride; x += 1) {
      const encoded = raw[cursor++]!;
      const left = x >= 4 ? row[x - 4]! : 0;
      const up = previous[x]!;
      const upperLeft = x >= 4 ? previous[x - 4]! : 0;
      const predictor =
        filter === 0 ? 0 :
        filter === 1 ? left :
        filter === 2 ? up :
        filter === 3 ? Math.floor((left + up) / 2) :
        filter === 4 ? paeth(left, up, upperLeft) :
        -1;
      assert(predictor >= 0, `PNG 使用未知过滤器 ${filter}`);
      row[x] = (encoded + predictor) & 0xff;
    }
    for (let x = 3; x < stride; x += 4) {
      hasTransparentPixel ||= row[x]! < 255;
      hasVisiblePixel ||= row[x]! > 0;
    }
    previous = row;
  }
  return { width, height, hasTransparentPixel, hasVisiblePixel };
}

export function validateBakeManifest(
  manifestPath: string,
  assetRoot = dirname(manifestPath),
): ValidatedBakeManifest {
  const parsed: unknown = JSON.parse(readFileSync(manifestPath, "utf8"));
  assert(isRecord(parsed), "根节点必须是对象");
  assert(parsed.schemaVersion === "1.0.0", "schemaVersion 必须为 1.0.0");
  for (const field of ["manifestVersion", "catalogVersion", "publicationVersion", "vehicleId"]) {
    assert(typeof parsed[field] === "string" && ID.test(parsed[field]), `${field} 非法`);
  }
  assert(Array.isArray(parsed.renders), "renders 必须是数组");
  assert(parsed.renders.length === 64, "必须恰好包含 64 个 render");

  const manifest = parsed as unknown as BakeManifest;
  const entries = new Map<string, BakeRender>();
  for (const [index, render] of manifest.renders.entries()) {
    assert(isRecord(render), `renders[${index}] 必须是对象`);
    assert(render.status === "ready", `renders[${index}] 必须为 ready`);
    assert(typeof render.configurationKey === "string" && CONFIGURATION_KEY.test(render.configurationKey), `renders[${index}] configurationKey 非法`);
    assert(typeof render.renderViewId === "string" && VIEWS.has(render.renderViewId), `renders[${index}] renderViewId 非法`);
    const key = renderKey(render.configurationKey, render.renderViewId);
    assert(!entries.has(key), `组合重复：${render.configurationKey}/${render.renderViewId}`);
    const expectedPath = `renders/${manifest.publicationVersion}/${manifest.vehicleId}/${render.configurationKey}/${render.renderViewId}.png`;
    assert(render.path === expectedPath, `路径不符合 canonical 规则：${render.path}`);
    assert(render.format === "png" && render.colorSpace === "sRGB" && render.alphaMode === "straight", `renders[${index}] 图片元数据非法`);
    assert(
      typeof render.coverageInverted === "boolean" &&
        typeof render.normalizationRequired === "boolean",
      `renders[${index}] Alpha 处理元数据非法`,
    );
    assert(Number.isInteger(render.width) && render.width > 0 && Number.isInteger(render.height) && render.height > 0, `renders[${index}] 尺寸非法`);
    assert(typeof render.sha256 === "string" && /^[a-f0-9]{64}$/.test(render.sha256), `renders[${index}] SHA256 非法`);

    const bytes = readFileSync(safeAssetPath(assetRoot, render.path));
    const png = inspectPng(bytes);
    assert(png.width === render.width && png.height === render.height, `尺寸不匹配：${render.path}`);
    assert(png.hasTransparentPixel && png.hasVisiblePixel, `Alpha 必须同时包含透明与可见像素：${render.path}`);
    assert(createHash("sha256").update(bytes).digest("hex") === render.sha256, `SHA256 不匹配：${render.path}`);
    entries.set(key, render as unknown as BakeRender);
  }
  assert(new Set(manifest.renders.map((render) => render.configurationKey)).size === 16, "必须包含 16 个唯一配置");
  return {
    manifest,
    manifestPath: realpathSync(manifestPath),
    assetRoot: realpathSync(assetRoot),
    renders: entries,
  };
}

export function findReadyRender(
  bake: ValidatedBakeManifest,
  configurationKey: string,
  renderViewId: string,
): BakeRender | undefined {
  return bake.renders.get(renderKey(configurationKey, renderViewId));
}

export async function publishBakeAtomically(
  sourceRoot: string,
  destinationRoot: string,
): Promise<string> {
  const sourceManifestPath = resolve(sourceRoot, "bake-manifest.json");
  const validated = validateBakeManifest(sourceManifestPath, sourceRoot);
  const version = validated.manifest.publicationVersion;
  const destination = resolve(destinationRoot, "renders", version);
  if (existsSync(destination)) {
    throw new Error(`拒绝覆盖已发布版本：${version}`);
  }

  await mkdir(resolve(destinationRoot, "renders"), { recursive: true });
  const staging = resolve(destinationRoot, "renders", `.${version}.${process.pid}.${Date.now()}.tmp`);
  try {
    await mkdir(staging);
    await cp(
      resolve(sourceRoot, "renders", version),
      staging,
      { recursive: true, errorOnExist: true, force: false },
    );
    await writeFile(
      resolve(staging, "bake-manifest.json"),
      `${JSON.stringify(validated.manifest, null, 2)}\n`,
      { flag: "wx" },
    );
    await rename(staging, destination);
    return destination;
  } catch (error) {
    await rm(staging, { recursive: true, force: true });
    throw error;
  }
}
