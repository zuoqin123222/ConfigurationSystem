import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { readFile, stat } from "node:fs/promises";
import { basename, dirname, extname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const stableId = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const engineVersion = /^5\.\d+(?:\.\d+)?$/;
const sha256 = /^[a-f0-9]{64}$/;
const primaryAssetId = /^[A-Za-z][A-Za-z0-9_]*:[a-z0-9]+(?:-[a-z0-9]+)*$/;
const pakName = /^[a-z0-9]+(?:-[a-z0-9]+)*\.pak$/;
const allowedRoot = "/Game/ContentPacks/";
const rootFields = new Set([
  "schemaVersion", "packId", "version", "catalogVersion", "engineVersion",
  "platform", "mountPoint", "pak", "primaryAssetIds"
]);
const pakFields = new Set(["fileName", "bytes", "sha256"]);

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function rejectExtraFields(value, allowed, path, errors) {
  if (!isObject(value)) return;
  for (const key of Object.keys(value)) {
    if (!allowed.has(key)) errors.push(`${path}.${key}: 不允许的字段`);
  }
}

export function validateContentPackManifest(manifest, expected = {}) {
  const errors = [];
  if (!isObject(manifest)) return { valid: false, errors: ["manifest: 必须是 object"] };

  rejectExtraFields(manifest, rootFields, "manifest", errors);
  if (manifest.schemaVersion !== "1.0.0") errors.push("schemaVersion: 仅支持 1.0.0");
  for (const field of ["packId", "version", "catalogVersion"]) {
    if (typeof manifest[field] !== "string" || !stableId.test(manifest[field])) {
      errors.push(`${field}: 必须是小写 kebab-case`);
    }
  }
  if (typeof manifest.engineVersion !== "string" || !engineVersion.test(manifest.engineVersion)) {
    errors.push("engineVersion: 必须是 UE 5.x 或 5.x.y");
  }
  if (manifest.platform !== "Win64") errors.push("platform: 必须是 Win64");

  const expectedMountPoint = `${allowedRoot}${manifest.packId}/`;
  if (manifest.mountPoint !== expectedMountPoint) {
    errors.push(`mountPoint: 必须严格等于 ${expectedMountPoint}`);
  }

  validatePakMetadata(manifest.pak, errors);
  validatePrimaryAssetIds(manifest.primaryAssetIds, errors);
  validateExpectations(manifest, expected, errors);
  return { valid: errors.length === 0, errors };
}

function validatePakMetadata(pak, errors) {
  if (!isObject(pak)) {
    errors.push("pak: 必须是 object");
    return;
  }
  rejectExtraFields(pak, pakFields, "pak", errors);
  if (typeof pak.fileName !== "string" || !pakName.test(pak.fileName)) {
    errors.push("pak.fileName: 必须是安全的小写 kebab-case .pak 文件名");
  }
  if (!Number.isSafeInteger(pak.bytes) || pak.bytes < 1 || pak.bytes > 10 * 1024 ** 3) {
    errors.push("pak.bytes: 必须是 1..10737418240 的整数");
  }
  if (typeof pak.sha256 !== "string" || !sha256.test(pak.sha256)) {
    errors.push("pak.sha256: 必须是 64 位小写十六进制");
  }
}

function validatePrimaryAssetIds(ids, errors) {
  if (!Array.isArray(ids) || ids.length === 0) {
    errors.push("primaryAssetIds: 必须是非空数组");
    return;
  }
  const seen = new Set();
  ids.forEach((id, index) => {
    if (typeof id !== "string" || !primaryAssetId.test(id)) {
      errors.push(`primaryAssetIds[${index}]: 格式必须为 Type:kebab-name`);
    }
    if (seen.has(id)) errors.push(`primaryAssetIds[${index}]: PrimaryAssetId 重复 (${id})`);
    seen.add(id);
  });
}

function validateExpectations(manifest, expected, errors) {
  const checks = [
    ["catalogVersion", expected.catalogVersion],
    ["engineVersion", expected.engineVersion],
    ["platform", expected.platform]
  ];
  for (const [field, value] of checks) {
    if (value !== undefined && manifest[field] !== value) {
      errors.push(`${field}: 期望 ${value}，实际 ${manifest[field]}`);
    }
  }
  if (expected.mountedPrimaryAssetIds) {
    const mounted = new Set(expected.mountedPrimaryAssetIds);
    for (const id of manifest.primaryAssetIds ?? []) {
      if (mounted.has(id)) errors.push(`primaryAssetIds: 与已挂载内容包冲突 (${id})`);
    }
  }
}

export async function computeFileSha256(filePath) {
  const hash = createHash("sha256");
  await new Promise((accept, reject) => {
    const stream = createReadStream(filePath);
    stream.on("data", (chunk) => hash.update(chunk));
    stream.on("error", reject);
    stream.on("end", accept);
  });
  return hash.digest("hex");
}

export async function validateContentPackFiles(manifestPath, pakPath, expected = {}) {
  const errors = [];
  let manifest;
  try {
    manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  } catch (error) {
    return { valid: false, errors: [`manifest: 无法读取有效 JSON (${error.message})`] };
  }

  errors.push(...validateContentPackManifest(manifest, expected).errors);
  const resolvedPak = resolve(pakPath);
  const declaredPak = resolve(dirname(resolve(manifestPath)), manifest.pak?.fileName ?? "");
  if (extname(resolvedPak).toLowerCase() !== ".pak") errors.push("pak: 扩展名必须是 .pak");
  if (resolvedPak !== declaredPak || basename(resolvedPak) !== manifest.pak?.fileName) {
    errors.push("pak: 必须与 manifest 同目录且文件名等于 pak.fileName");
  }

  try {
    const info = await stat(resolvedPak);
    if (!info.isFile()) {
      errors.push("pak: 路径不是普通文件");
    } else {
      if (info.size !== manifest.pak?.bytes) {
        errors.push(`pak.bytes: 声明 ${manifest.pak?.bytes}，实际 ${info.size}`);
      }
      const actualSha256 = await computeFileSha256(resolvedPak);
      if (actualSha256 !== manifest.pak?.sha256) {
        errors.push(`pak.sha256: 声明值与实际文件不匹配 (${actualSha256})`);
      }
    }
  } catch (error) {
    errors.push(`pak: 无法读取 (${error.message})`);
  }
  return { valid: errors.length === 0, errors, manifest };
}

async function main(args) {
  if (args.length < 1 || args.length > 2) {
    console.error("用法: node tools/validate-content-pack.mjs <manifest.json> [pack.pak]");
    process.exitCode = 2;
    return;
  }
  const manifestPath = resolve(args[0]);
  const result = args[1]
    ? await validateContentPackFiles(manifestPath, resolve(args[1]))
    : validateContentPackManifest(JSON.parse(await readFile(manifestPath, "utf8")));
  if (!result.valid) {
    console.error(`内容包验证失败（${result.errors.length} 项）：`);
    for (const error of result.errors) console.error(`- ${error}`);
    process.exitCode = 1;
  } else {
    console.log("内容包 manifest 验证通过。");
  }
}

const isMain = process.argv[1]
  && resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  main(process.argv.slice(2)).catch((error) => {
    console.error(error.message);
    process.exitCode = 2;
  });
}
