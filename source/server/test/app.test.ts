import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { buildApp } from "../src/app.js";
import { validateBakeManifest } from "../src/bake.js";
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
const validResolveRequest = {
  catalogVersion: "mvp-v1",
  publicationVersion: "mvp-v1",
  vehicleId: "demo-car",
  selections: validSelections,
  renderViewId: "front",
};

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

test("GET /health 返回 fixture 版本", async (t) => {
  const app = buildApp();
  t.after(() => app.close());

  const response = await app.inject({ method: "GET", url: "/health" });

  assert.equal(response.statusCode, 200);
  assert.deepEqual(response.json(), {
    status: "ok",
    catalogVersion: "mvp-v1",
    publicationVersion: "mvp-v1",
  });
});

test("GET /api/v1/catalog 返回根目录 catalog fixture", async (t) => {
  const app = buildApp();
  t.after(() => app.close());

  const response = await app.inject({ method: "GET", url: "/api/v1/catalog" });

  assert.equal(response.statusCode, 200);
  assert.equal(response.json().vehicle.vehicleId, "demo-car");
  assert.equal(response.json().parts.length, 4);
});

test("resolve 校验完整请求并返回 canonical key、视角和图片 URL", async (t) => {
  const app = buildApp();
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
  const app = buildApp();
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
  const app = buildApp();
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
  const app = buildApp();
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
  const app = buildApp();
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
  const app = buildApp();
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
  const app = buildApp({ bakeRoot: validBakeRoot });
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
  const app = buildApp({ bakeRoot: validBakeRoot });
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
