import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { cp, mkdtemp, mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import {
  findReadyRender,
  publishBakeAtomically,
  validateBakeManifest,
} from "../src/bake.js";

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../..");
const validRoot = resolve(repositoryRoot, "contracts/fixtures/bake.valid");
const invalidRoot = resolve(repositoryRoot, "contracts/fixtures/bake.invalid");
const publishedPlanPath = resolve(
  repositoryRoot,
  "contracts/fixtures/published-configurations.mvp.json",
);

async function withChangedManifest(
  change: (manifest: any) => void,
): Promise<{ path: string; cleanup: () => Promise<void> }> {
  const directory = await mkdtemp(join(tmpdir(), "bake-manifest-test-"));
  const manifest = JSON.parse(
    await readFile(resolve(validRoot, "bake-manifest.json"), "utf8"),
  );
  change(manifest);
  const path = resolve(directory, "bake-manifest.json");
  await writeFile(path, JSON.stringify(manifest));
  return {
    path,
    cleanup: () => rm(directory, { recursive: true, force: true }),
  };
}

test("校验实际 bake fixture 的全部唯一 ready RGBA PNG", () => {
  const bake = validateBakeManifest(
    resolve(validRoot, "bake-manifest.json"),
    validRoot,
  );

  assert.equal(bake.renders.size, 64);
  assert.ok(
    findReadyRender(
      bake,
      "paint-silver__wheel-forged__interior-ivory__frame-red",
      "rear-right",
    ),
  );
});

test("render 数量与配置数量按 manifest 动态校验，不依赖历史 64/16 常量", async () => {
  const fixture = await withChangedManifest((manifest) => {
    const firstConfiguration = manifest.renders[0].configurationKey;
    manifest.renders = manifest.renders.filter(
      (render: any) => render.configurationKey === firstConfiguration,
    );
  });
  try {
    const bake = validateBakeManifest(fixture.path, validRoot);
    assert.equal(bake.renders.size, 4);
  } finally {
    await fixture.cleanup();
  }
});

test("manifest 与 published plan 的任务身份和视角集合必须完全一致", async (t) => {
  const bake = validateBakeManifest(
    resolve(validRoot, "bake-manifest.json"),
    validRoot,
    publishedPlanPath,
  );
  assert.equal(bake.renders.size, 64);

  const directory = await mkdtemp(join(tmpdir(), "bake-plan-test-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const plan = JSON.parse(await readFile(publishedPlanPath, "utf8"));
  plan.configurations[0].configurationKey =
    "paint-purple__wheel-sport__interior-dark__frame-black";
  const incompletePlanPath = resolve(directory, "published-configurations.json");
  await writeFile(incompletePlanPath, JSON.stringify(plan));
  assert.throws(
    () => validateBakeManifest(
      resolve(validRoot, "bake-manifest.json"),
      validRoot,
      incompletePlanPath,
    ),
    /render 集合与 published plan 不一致/,
  );
});

test("v2 manifest 使用 renderKey 作为图片身份且拒绝混用 v1 字段", async () => {
  const fixture = await withChangedManifest((manifest) => {
    const firstConfiguration = manifest.renders[0].configurationKey;
    manifest.schemaVersion = "2.0.0";
    manifest.renders = manifest.renders
      .filter((render: any) => render.configurationKey === firstConfiguration)
      .map((render: any) => {
        const { configurationKey, ...rest } = render;
        return { renderKey: configurationKey, ...rest };
      });
  });
  try {
    const bake = validateBakeManifest(fixture.path, validRoot);
    assert.equal(bake.manifest.schemaVersion, "2.0.0");
    assert.ok(findReadyRender(
      bake,
      "paint-red__wheel-sport__interior-dark__frame-black",
      "front",
    ));

    const manifest = JSON.parse(await readFile(fixture.path, "utf8"));
    manifest.renders[0].configurationKey = manifest.renders[0].renderKey;
    await writeFile(fixture.path, JSON.stringify(manifest));
    assert.throws(
      () => validateBakeManifest(fixture.path, validRoot),
      /不得包含 configurationKey/,
    );
  } finally {
    await fixture.cleanup();
  }
});

test("每个配置必须包含相同的完整 RenderView 集合", async () => {
  const fixture = await withChangedManifest((manifest) => {
    manifest.renders.splice(4, 1);
  });
  try {
    assert.throws(
      () => validateBakeManifest(fixture.path, validRoot),
      /完整 RenderView 集合/,
    );
  } finally {
    await fixture.cleanup();
  }
});

test("拒绝 SHA256 与实际 PNG 不一致的负向 fixture", () => {
  assert.throws(
    () =>
      validateBakeManifest(
        resolve(invalidRoot, "bake-manifest.json"),
        invalidRoot,
      ),
    /SHA256 不匹配/,
  );
});

test("拒绝重复组合、非 ready、非 canonical 路径和尺寸不符", async () => {
  const cases: Array<[(manifest: any) => void, RegExp]> = [
    [(manifest) => {
      manifest.renders[1].configurationKey = manifest.renders[0].configurationKey;
      manifest.renders[1].renderViewId = manifest.renders[0].renderViewId;
    }, /组合重复/],
    [(manifest) => { manifest.renders[0].status = "failed"; }, /必须为 ready/],
    [(manifest) => { manifest.renders[0].path = manifest.renders[1].path; }, /canonical/],
    [(manifest) => { manifest.renders[0].width = 3; }, /尺寸不匹配/],
  ];

  for (const [change, expected] of cases) {
    const fixture = await withChangedManifest(change);
    try {
      assert.throws(
        () => validateBakeManifest(fixture.path, validRoot),
        expected,
      );
    } finally {
      await fixture.cleanup();
    }
  }
});

test("拒绝不是 RGBA 的 PNG", async () => {
  const sourcePath = resolve(
    validRoot,
    "renders/mvp-v1/demo-car",
    "paint-red__wheel-sport__interior-dark__frame-black/front.png",
  );
  const bytes = Buffer.from(await readFile(sourcePath));
  bytes[25] = 2;
  const sha256 = createHash("sha256").update(bytes).digest("hex");
  const root = await mkdtemp(join(tmpdir(), "bake-rgb-test-"));
  const relativePath =
    "renders/mvp-v1/demo-car/paint-red__wheel-sport__interior-dark__frame-black/front.png";
  await mkdir(dirname(resolve(root, relativePath)), { recursive: true });
  await writeFile(resolve(root, relativePath), bytes);
  const fixture = await withChangedManifest((manifest) => {
    manifest.renders[0].sha256 = sha256;
  });
  try {
    assert.throws(
      () => validateBakeManifest(fixture.path, root),
      /8-bit RGBA/,
    );
  } finally {
    await fixture.cleanup();
    await rm(root, { recursive: true, force: true });
  }
});

test("原子发布成功后拒绝覆盖同一 publicationVersion", async (t) => {
  const destinationRoot = await mkdtemp(join(tmpdir(), "bake-publish-"));
  t.after(() => rm(destinationRoot, { recursive: true, force: true }));

  const published = await publishBakeAtomically(validRoot, destinationRoot);
  assert.equal(published, resolve(destinationRoot, "renders/mvp-v1"));
  assert.deepEqual(
    JSON.parse(
      await readFile(
        resolve(destinationRoot, "renders/active-publication.json"),
        "utf8",
      ),
    ),
    { publicationVersion: "mvp-v1" },
  );

  await assert.rejects(
    publishBakeAtomically(validRoot, destinationRoot),
    /拒绝覆盖已发布版本/,
  );
});

test("校验失败不会留下 publication 或临时目录", async (t) => {
  const sourceRoot = await mkdtemp(join(tmpdir(), "bake-invalid-source-"));
  const destinationRoot = await mkdtemp(join(tmpdir(), "bake-invalid-target-"));
  t.after(() => rm(sourceRoot, { recursive: true, force: true }));
  t.after(() => rm(destinationRoot, { recursive: true, force: true }));
  await cp(invalidRoot, sourceRoot, { recursive: true });
  await mkdir(resolve(destinationRoot, "renders"), { recursive: true });

  await assert.rejects(
    publishBakeAtomically(sourceRoot, destinationRoot),
    /SHA256 不匹配/,
  );
  assert.equal(existsSync(resolve(destinationRoot, "renders/mvp-v1")), false);
});
