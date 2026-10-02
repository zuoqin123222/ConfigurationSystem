import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import {
  computeFileSha256,
  validateContentPackFiles,
  validateContentPackManifest
} from "./validate-content-pack.mjs";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const fixtures = resolve(root, "contracts", "fixtures");

async function fixture(name) {
  return JSON.parse(await readFile(resolve(fixtures, name), "utf8"));
}

test("有效 fixture 通过结构与运行时期望", async () => {
  const manifest = await fixture("content-pack.valid.json");
  const result = validateContentPackManifest(manifest, {
    catalogVersion: "catalog-1",
    engineVersion: "5.8",
    platform: "Win64"
  });
  assert.deepEqual(result, { valid: true, errors: [] });
});

test("SC01 Provider v2 支持三类并固定 catalogVersion", async () => {
  const base = await fixture("content-pack.sc01-provider.valid.json");
  for (const providerType of ["material", "environment", "vehicle"]) {
    const manifest = structuredClone(base);
    manifest.providerType = providerType;
    const result = validateContentPackManifest(manifest, {
      catalogVersion: "sc01-draft-20260121",
      providerType
    });
    assert.deepEqual(result, { valid: true, errors: [] });
  }
});

test("v1 保持兼容但不能携带 v2 providerType", async () => {
  const manifest = await fixture("content-pack.valid.json");
  assert.equal(validateContentPackManifest(manifest).valid, true);
  manifest.providerType = "material";
  const result = validateContentPackManifest(manifest);
  assert.equal(result.valid, false);
  assert.match(result.errors.join("\n"), /1\.0\.0.*providerType/);
});

test("v2 拒绝缺失或未知 providerType", async () => {
  const manifest = await fixture("content-pack.sc01-provider.valid.json");
  delete manifest.providerType;
  const missing = validateContentPackManifest(manifest);
  assert.equal(missing.valid, false);
  assert.match(missing.errors.join("\n"), /providerType/);

  manifest.providerType = "audio";
  const unknown = validateContentPackManifest(manifest);
  assert.equal(unknown.valid, false);
  assert.match(unknown.errors.join("\n"), /providerType/);
});

test("无效 fixture 拒绝版本、平台、挂载点、pak 和重复资产", async () => {
  const result = validateContentPackManifest(
    await fixture("content-pack.invalid.json")
  );
  assert.equal(result.valid, false);
  assert.match(result.errors.join("\n"), /schemaVersion/);
  assert.match(result.errors.join("\n"), /mountPoint/);
  assert.match(result.errors.join("\n"), /PrimaryAssetId 重复/);
});

test("已挂载 PrimaryAssetId 冲突被拒绝", async () => {
  const manifest = await fixture("content-pack.valid.json");
  const result = validateContentPackManifest(manifest, {
    mountedPrimaryAssetIds: ["CarMaterialOption:paint-red"]
  });
  assert.equal(result.valid, false);
  assert.match(result.errors.join("\n"), /已挂载内容包冲突/);
});

test("文件字节数和 SHA-256 必须同时匹配", async (context) => {
  const directory = await mkdtemp(resolve(tmpdir(), "content-pack-"));
  context.after(() => rm(directory, { recursive: true, force: true }));
  const pakPath = resolve(directory, "demo-car-materials.pak");
  const manifestPath = resolve(directory, "manifest.json");
  const payload = Buffer.from("ConfigurationSystem test pak");
  await writeFile(pakPath, payload);

  const manifest = await fixture("content-pack.valid.json");
  manifest.pak.bytes = payload.length;
  manifest.pak.sha256 = await computeFileSha256(pakPath);
  await writeFile(manifestPath, JSON.stringify(manifest));
  assert.equal((await validateContentPackFiles(manifestPath, pakPath)).valid, true);

  await writeFile(pakPath, Buffer.from("tampered"));
  const rejected = await validateContentPackFiles(manifestPath, pakPath);
  assert.equal(rejected.valid, false);
  assert.match(rejected.errors.join("\n"), /pak.bytes/);
  assert.match(rejected.errors.join("\n"), /pak.sha256/);
});
