import assert from "node:assert/strict";
import {
  cp,
  mkdtemp,
  mkdir,
  readFile,
  rename,
  rm,
  writeFile,
} from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { buildApp, type BuildAppOptions } from "../src/app.js";
import {
  publishBakeAtomically,
  validateBakeManifest,
} from "../src/bake.js";
import {
  loadContracts,
  type ContractData,
  type PublishedConfiguration,
} from "../src/data.js";

const validSelections = {
  paint: "paint-silver",
  wheel: "wheel-forged",
  interior: "interior-ivory",
  frame: "frame-red",
};
const validKey =
  "paint-silver__wheel-forged__interior-ivory__frame-red";
const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../../../..");
const validBakeRoot = resolve(repositoryRoot, "contracts/fixtures/bake.valid");
const webRoot = resolve(repositoryRoot, "package/clients/web");
const validResolveRequest = {
  catalogVersion: "mvp-v1",
  publicationVersion: "mvp-v1",
  vehicleId: "demo-car",
  selections: validSelections,
  renderViewId: "front",
};
const fixtureData = loadContracts();
const fixtureBake = validateBakeManifest(
  resolve(validBakeRoot, "bake-manifest.json"),
  validBakeRoot,
);

function buildFixtureApp(options: BuildAppOptions = {}) {
  return buildApp({
    data: fixtureData,
    bake: fixtureBake,
    bakeRoot: validBakeRoot,
    ...options,
  });
}

function copyData(
  transform?: (configuration: PublishedConfiguration) => PublishedConfiguration,
): ContractData {
  const source = loadContracts();
  const configurations = new Map(
    [...source.configurations].map(([key, configuration]) => [
      key,
      transform?.(configuration) ?? configuration,
    ]),
  );
  return { ...source, configurations };
}

async function createVersionedBakeSource(
  publicationVersion: string,
): Promise<string> {
  const sourceRoot = await mkdtemp(join(tmpdir(), "active-publication-source-"));
  await cp(validBakeRoot, sourceRoot, { recursive: true });
  await rename(
    resolve(sourceRoot, "renders/mvp-v1"),
    resolve(sourceRoot, `renders/${publicationVersion}`),
  );
  const manifestPath = resolve(sourceRoot, "bake-manifest.json");
  const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  manifest.publicationVersion = publicationVersion;
  for (const render of manifest.renders) {
    render.path = render.path.replace(
      "renders/mvp-v1/",
      `renders/${publicationVersion}/`,
    );
  }
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  return sourceRoot;
}

test("GET /health 返回 fixture 版本", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  const response = await app.inject({ method: "GET", url: "/health" });

  assert.equal(response.statusCode, 200);
  assert.deepEqual(response.json(), {
    status: "ok",
    catalogVersion: "mvp-v1",
    publicationVersion: "mvp-v1",
  });
});

test("发布激活后重启会从指针读取活动 publicationVersion", async (t) => {
  const publicationVersion = "mvp-v2";
  const sourceRoot = await createVersionedBakeSource(publicationVersion);
  const packageRoot = await mkdtemp(join(tmpdir(), "active-publication-package-"));
  t.after(() => rm(sourceRoot, { recursive: true, force: true }));
  t.after(() => rm(packageRoot, { recursive: true, force: true }));

  await publishBakeAtomically(sourceRoot, packageRoot);

  for (let restart = 0; restart < 2; restart += 1) {
    const app = buildApp({ bakeRoot: packageRoot, webRoot: false });
    const response = await app.inject({ method: "GET", url: "/health" });
    assert.equal(response.statusCode, 200);
    assert.equal(response.json().publicationVersion, publicationVersion);
    await app.close();
  }
});

test("活动发布指针损坏时启动安全失败", async (t) => {
  const packageRoot = await mkdtemp(join(tmpdir(), "broken-publication-pointer-"));
  t.after(() => rm(packageRoot, { recursive: true, force: true }));
  await mkdir(resolve(packageRoot, "renders"), { recursive: true });
  await writeFile(
    resolve(packageRoot, "renders/active-publication.json"),
    "{not-json",
  );

  assert.throws(
    () => buildApp({ bakeRoot: packageRoot, webRoot: false }),
    /活动发布指针损坏/,
  );
});

test("GET /api/v1/catalog 返回根目录 catalog fixture", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  const response = await app.inject({ method: "GET", url: "/api/v1/catalog" });

  assert.equal(response.statusCode, 200);
  assert.equal(response.json().vehicle.vehicleId, "demo-car");
  assert.equal(response.json().parts.length, 4);
});

test("安全托管 Web 构建产物并保持 API 路由优先", async (t) => {
  const app = buildFixtureApp({ webRoot });
  t.after(() => app.close());

  const index = await app.inject({ method: "GET", url: "/" });
  const bundlePath = index.body.match(/src="(\/assets\/[^"]+\.js)"/)?.[1];
  assert.ok(bundlePath, "index.html 必须引用生产 JS bundle");
  const bundle = await app.inject({ method: "GET", url: bundlePath });
  const manifest = await app.inject({
    method: "GET",
    url: "/sc01/crop-manifest.json",
  });
  const catalog = await app.inject({ method: "GET", url: "/api/v2/catalog" });
  const missingApi = await app.inject({ method: "GET", url: "/api/not-found" });

  assert.equal(index.statusCode, 200);
  assert.match(index.headers["content-type"] ?? "", /^text\/html/);
  assert.equal(index.headers["x-content-type-options"], "nosniff");
  assert.match(index.body, /<div id="root"><\/div>/);
  assert.equal(bundle.statusCode, 200);
  assert.match(bundle.headers["content-type"] ?? "", /^text\/javascript/);
  assert.match(bundle.headers["cache-control"] ?? "", /immutable/);
  assert.equal(manifest.statusCode, 200);
  assert.match(manifest.headers["content-type"] ?? "", /^application\/json/);
  assert.equal(manifest.headers["cache-control"], "no-cache");
  assert.equal(catalog.statusCode, 200);
  assert.equal(catalog.json().vehicle.vehicleId, "sc01");
  assert.equal(missingApi.statusCode, 404);
  assert.equal(missingApi.json().code, "NOT_FOUND");
  assert.doesNotMatch(missingApi.body, /<div id="root">/);
});

test("Web 静态托管拒绝路径穿越且不把缺失资源回退为 HTML", async (t) => {
  const app = buildFixtureApp({ webRoot });
  t.after(() => app.close());

  for (const url of [
    "/%2e%2e/contracts/openapi.yaml",
    "/assets/%2e%2e/%2e%2e/contracts/openapi.yaml",
    "/missing.js",
  ]) {
    const response = await app.inject({ method: "GET", url });
    assert.notEqual(response.statusCode, 200);
    assert.doesNotMatch(response.body, /<div id="root">/);
  }
});

test("resolve 校验完整请求并返回 canonical key、视角和图片 URL", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  const response = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    payload: {
      catalogVersion: "mvp-v1",
      publicationVersion: "mvp-v1",
      vehicleId: "demo-car",
      selections: {
        frame: "frame-red",
        interior: "interior-ivory",
        wheel: "wheel-forged",
        paint: "paint-silver",
      },
      renderViewId: "front",
    },
  });

  assert.equal(response.statusCode, 200);
  assert.deepEqual(response.json(), {
    configurationKey: validKey,
    renderViewId: "front",
    imageUrl: `/assets/renders/mvp-v1/demo-car/${validKey}/front.png`,
  });
});

test("resolve 拒绝缺少分区、额外分区及跨分区选项", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  const invalidSelections = [
    {
      paint: "paint-red",
      wheel: "wheel-sport",
      interior: "interior-dark",
    },
    { ...validSelections, unknown: "value" },
    { ...validSelections, paint: "wheel-sport" },
  ];

  for (const selections of invalidSelections) {
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/renders/resolve",
      payload: { ...validResolveRequest, selections },
    });
    assert.equal(response.statusCode, 400);
  }
});

test("resolve 拒绝未知顶层字段和非法 JSON", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  const extra = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    payload: { ...validResolveRequest, totalPriceMinor: 1 },
  });
  const malformed = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    headers: { "content-type": "application/json" },
    payload: "{",
  });

  assert.equal(extra.statusCode, 400);
  assert.equal(malformed.statusCode, 400);
});

test("resolve 对 catalog 或 publication 版本不一致返回 409", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  for (const changed of [
    { catalogVersion: "stale-catalog" },
    { publicationVersion: "stale-publication" },
  ]) {
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/renders/resolve",
      payload: { ...validResolveRequest, ...changed },
    });
    assert.equal(response.statusCode, 409);
    assert.equal(response.json().code, "VERSION_CONFLICT");
  }
});

test("resolve 拒绝未知车型、非法视角和缺失字段", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  const unknownVehicle = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    payload: { ...validResolveRequest, vehicleId: "unknown-car" },
  });
  const invalidView = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    payload: { ...validResolveRequest, renderViewId: "unknown" },
  });
  const { vehicleId: _omitted, ...missingVehicleId } = validResolveRequest;
  const missing = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    payload: missingVehicleId,
  });

  assert.equal(unknownVehicle.statusCode, 404);
  assert.equal(invalidView.statusCode, 400);
  assert.equal(missing.statusCode, 400);
});

test("resolve 按 StableId 契约拒绝非法版本和车型 ID", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  for (const changed of [
    { catalogVersion: "../mvp-v1" },
    { publicationVersion: "MVP-V1" },
    { vehicleId: "demo_car" },
  ]) {
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/renders/resolve",
      payload: { ...validResolveRequest, ...changed },
    });
    assert.equal(response.statusCode, 400);
    assert.equal(response.json().code, "INVALID_REQUEST");
  }
});

test("resolve 在发布配置不存在时返回 404", async (t) => {
  const data = copyData();
  (data.configurations as Map<string, PublishedConfiguration>).delete(validKey);
  const app = buildApp({ data });
  t.after(() => app.close());

  const response = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    payload: validResolveRequest,
  });

  assert.equal(response.statusCode, 404);
  assert.equal(response.json().code, "CONFIGURATION_NOT_FOUND");
});

test("resolve 检出发布价格或 canonical key 不一致", async (t) => {
  for (const changed of [
    { totalPriceMinor: 1 },
    { configurationKey: "paint-red__wheel-sport__interior-dark__frame-black" },
  ]) {
    const data = copyData((configuration) =>
      configuration.configurationKey === validKey
        ? { ...configuration, ...changed }
        : configuration,
    );
    const app = buildApp({ data });
    const response = await app.inject({
      method: "POST",
      url: "/api/v1/renders/resolve",
      payload: validResolveRequest,
    });
    await app.close();

    assert.equal(response.statusCode, 500);
    assert.equal(response.json().code, "CONTRACT_INCONSISTENT");
  }
});

test("render 只返回 manifest 已校验的 PNG", async (t) => {
  const pngPath = resolve(
    validBakeRoot,
    "renders/mvp-v1/demo-car",
    validKey,
    "front.png",
  );
  const png = await readFile(pngPath);
  const app = buildFixtureApp();
  t.after(() => app.close());
  const found = await app.inject({
    method: "GET",
    url: `/assets/renders/mvp-v1/demo-car/${validKey}/front.png`,
  });

  assert.equal(found.statusCode, 200);
  assert.equal(found.headers["content-type"], "image/png");
  assert.deepEqual(found.rawPayload, png);
});

test("render 拒绝未知配置、未知视角和路径穿越", async (t) => {
  const app = buildFixtureApp();
  t.after(() => app.close());

  const urls = [
    "/assets/renders/mvp-v1/demo-car/not-a-configuration/front.png",
    `/assets/renders/mvp-v1/demo-car/${validKey}/unknown.png`,
    `/assets/renders/stale/demo-car/${validKey}/front.png`,
    `/assets/renders/mvp-v1/unknown-car/${validKey}/front.png`,
    "/assets/renders/mvp-v1/demo-car/%2e%2e/front.png",
    `/assets/renders/mvp-v1/demo-car/${encodeURIComponent(`${validKey}/../../secret`)}/front.png`,
  ];
  for (const url of urls) {
    const response = await app.inject({ method: "GET", url });
    assert.notEqual(response.statusCode, 200);
  }
});

test("resolve 只返回 manifest 中存在的 ready 组合", async (t) => {
  const bake = validateBakeManifest(
    resolve(validBakeRoot, "bake-manifest.json"),
    validBakeRoot,
  );
  const renders = new Map(bake.renders);
  renders.delete(`${validKey}\0front`);
  const app = buildApp({ bake: { ...bake, renders } });
  t.after(() => app.close());

  const response = await app.inject({
    method: "POST",
    url: "/api/v1/renders/resolve",
    payload: validResolveRequest,
  });

  assert.equal(response.statusCode, 404);
  assert.equal(response.json().code, "RENDER_NOT_FOUND");
});
