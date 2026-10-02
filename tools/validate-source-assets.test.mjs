import assert from "node:assert/strict";
import { mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";
import test from "node:test";
import {
  findForbiddenSourceAssets,
  isForbiddenSourceAsset,
  validateSourceAssets
} from "./validate-source-assets.mjs";

const repositoryRoot = resolve(import.meta.dirname, "..");

async function temporarySourceAssets(context) {
  const root = await mkdtemp(resolve(tmpdir(), "source-assets-gate-"));
  context.after(() => rm(root, { recursive: true, force: true }));
  return root;
}

test("允许 FBX、JSON、图片和授权文本进入 SourceAssets", async (context) => {
  const root = await temporarySourceAssets(context);
  await mkdir(resolve(root, "Vendor", "Vehicle"), { recursive: true });
  await Promise.all([
    writeFile(resolve(root, "Vendor", "Vehicle", "car.fbx"), "fbx"),
    writeFile(resolve(root, "Vendor", "Vehicle", "manifest.json"), "{}"),
    writeFile(resolve(root, "Vendor", "Vehicle", "preview.png"), "png"),
    writeFile(resolve(root, "Vendor", "Vehicle", "LICENSE.txt"), "license")
  ]);

  assert.deepEqual(await findForbiddenSourceAssets(root), []);
  assert.deepEqual((await validateSourceAssets(root)).errors, []);
});

test("递归拒绝 .uasset、.umap、.tps，扩展名大小写不敏感", async (context) => {
  const root = await temporarySourceAssets(context);
  await mkdir(resolve(root, "nested", "deep"), { recursive: true });
  await Promise.all([
    writeFile(resolve(root, "car.UASSET"), "asset"),
    writeFile(resolve(root, "nested", "showroom.umap"), "map"),
    writeFile(resolve(root, "nested", "deep", "atlas.TpS"), "sheet")
  ]);

  const result = await validateSourceAssets(root);
  assert.equal(result.valid, false);
  assert.deepEqual(result.violations, [
    "car.UASSET",
    "nested/deep/atlas.TpS",
    "nested/showroom.umap"
  ]);
  assert.equal(result.errors.length, 3);
});

test("只按完整扩展名拒绝，不误伤相似文件名", () => {
  assert.equal(isForbiddenSourceAsset("vehicle.uasset"), true);
  assert.equal(isForbiddenSourceAsset("vehicle.uasset.json"), false);
  assert.equal(isForbiddenSourceAsset("notes.tps.txt"), false);
});

test("缺失的 SourceAssets 目录返回可诊断失败", async (context) => {
  const root = await temporarySourceAssets(context);
  const result = await validateSourceAssets(resolve(root, "missing"));
  assert.equal(result.valid, false);
  assert.equal(result.violations.length, 0);
  assert.match(result.errors[0], /ENOENT|找不到/);
});

test("CLI 对合规目录返回 0，对违规目录返回 1", async (context) => {
  const root = await temporarySourceAssets(context);
  const validRoot = resolve(root, "valid");
  const invalidRoot = resolve(root, "invalid");
  await mkdir(validRoot);
  await mkdir(invalidRoot);
  await writeFile(resolve(validRoot, "car.fbx"), "fbx");
  await writeFile(resolve(invalidRoot, "level.umap"), "map");

  const script = resolve(import.meta.dirname, "validate-source-assets.mjs");
  const accepted = spawnSync(process.execPath, [script, validRoot], { encoding: "utf8" });
  const rejected = spawnSync(process.execPath, [script, invalidRoot], { encoding: "utf8" });
  assert.equal(accepted.status, 0, accepted.stderr);
  assert.match(accepted.stdout, /门禁通过/);
  assert.equal(rejected.status, 1);
  assert.match(rejected.stderr, /level\.umap/);
});

test("规范化清单示例冻结 Maya2025、FBX2020 和禁止覆盖策略", async () => {
  const schema = JSON.parse(await readFile(resolve(
    repositoryRoot,
    "contracts/schemas/reference-asset-normalization.schema.json"
  ), "utf8"));
  const example = JSON.parse(await readFile(resolve(
    repositoryRoot,
    "contracts/fixtures/reference-asset-normalization.example.json"
  ), "utf8"));

  assert.equal(schema.$schema, "https://json-schema.org/draft/2020-12/schema");
  assert.equal(schema.additionalProperties, false);
  assert.equal(example.normalization.mayaVersion, "2025");
  assert.equal(example.normalization.fbxFileVersion, "FBX202000");
  assert.equal(example.normalization.sceneUnit, "centimeter");
  assert.equal(example.normalization.upAxis, "+Z");
  assert.equal(example.output.overwrite, false);
  assert.notEqual(example.input.path, example.output.path);
  assert.ok(example.provenance.permittedUses.includes("normalize"));
  assert.ok(example.provenance.permittedUses.includes("unreal-import"));

  const relativeFbx = new RegExp(schema.$defs.relativeFbxPath.pattern);
  assert.equal(relativeFbx.test(example.input.path), true);
  assert.equal(relativeFbx.test("D:\\ProjectData\\sample.fbx"), false);
  assert.equal(relativeFbx.test("../sample.fbx"), false);
});
