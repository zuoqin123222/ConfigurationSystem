import assert from "node:assert/strict";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import test from "node:test";
import { buildApp } from "../src/app.js";
import { ConfigurationStoreV2 } from "../src/configuration-store-v2.js";
import {
  buildSc01PriceResult,
  deriveSc01Configuration,
  loadSc01V2,
} from "../src/sc01-v2.js";

const selections = {
  "exterior-body-cover": "body-cover-red",
  "wheel-material": "wheel-aluminum-alloy",
  "steering-wheel-skin": "steering-skin-ultrasuede-black",
};

const changedSelections = {
  ...selections,
  "steering-wheel-skin": "steering-skin-alcantara",
};

const request = {
  catalogVersion: "sc01-draft-20260121",
  vehicleId: "sc01",
  selections,
};

test("GET /api/v2/catalog 返回 SC01 draft 分层目录", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const response = await app.inject({ method: "GET", url: "/api/v2/catalog" });
  assert.equal(response.statusCode, 200);
  assert.equal(response.json().vehicle.vehicleId, "sc01");
  assert.equal(response.json().vehicle.quotable, false);
  assert.equal(response.json().options.length, 8);
});

test("创建配置返回稳定身份、revision 与禁止报价价格明细", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const response = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "create-sc01-a" },
    payload: request,
  });
  assert.equal(response.statusCode, 201);
  assert.equal(response.headers.etag, '"1"');
  assert.equal(response.headers["idempotency-replayed"], "false");
  const body = response.json();
  assert.match(body.configurationId, /^cfg-[a-f0-9]{24}$/);
  assert.match(body.renderKey, /__render-[a-f0-9]{24}$/);
  assert.equal(body.revision, 1);
  assert.equal(body.priceResult.quoteAllowed, false);
  assert.equal(body.priceResult.totalPriceMinor, null);
});

test("Idempotency-Key 支持重放并拒绝不同请求复用", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const first = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "same-key" },
    payload: request,
  });
  const replay = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "same-key" },
    payload: request,
  });
  const conflict = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    headers: { "idempotency-key": "same-key" },
    payload: { ...request, selections: changedSelections },
  });
  assert.equal(first.statusCode, 201);
  assert.equal(replay.statusCode, 200);
  assert.equal(replay.headers["idempotency-replayed"], "true");
  assert.equal(replay.json().configurationId, first.json().configurationId);
  assert.equal(conflict.statusCode, 409);
  assert.equal(conflict.json().code, "IDEMPOTENCY_CONFLICT");
});

test("revision 冲突不覆盖，成功更新保留 createdAt 并迁移稳定 ID", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const created = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: request,
  });
  const original = created.json();
  const conflict = await app.inject({
    method: "PUT",
    url: `/api/v2/configurations/${original.configurationId}`,
    payload: { ...request, selections: changedSelections, revision: 99 },
  });
  assert.equal(conflict.statusCode, 409);
  assert.equal(conflict.json().code, "REVISION_CONFLICT");

  const updated = await app.inject({
    method: "PUT",
    url: `/api/v2/configurations/${original.configurationId}`,
    payload: { ...request, selections: changedSelections, revision: 1 },
  });
  assert.equal(updated.statusCode, 200);
  assert.equal(updated.json().revision, 2);
  assert.equal(updated.json().createdAt, original.createdAt);
  assert.notEqual(updated.json().configurationId, original.configurationId);

  const oldLookup = await app.inject({
    method: "GET",
    url: `/api/v2/configurations/${original.configurationId}`,
  });
  const newLookup = await app.inject({
    method: "GET",
    url: `/api/v2/configurations/${updated.json().configurationId}`,
  });
  assert.equal(oldLookup.statusCode, 404);
  assert.equal(newLookup.statusCode, 200);
});

test("草案拒绝报价请求，render resolve 只返回投影标识", async (t) => {
  const app = buildApp();
  t.after(() => app.close());
  const quote = await app.inject({
    method: "POST",
    url: "/api/v2/configurations",
    payload: { ...request, quoteRequested: true },
  });
  assert.equal(quote.statusCode, 422);
  assert.equal(quote.json().code, "PRICE_UNCONFIRMED");

  const render = await app.inject({
    method: "POST",
    url: "/api/v2/renders/resolve",
    payload: { ...request, renderViewId: "front-left" },
  });
  assert.equal(render.statusCode, 200);
  assert.match(render.json().renderKey, /__render-[a-f0-9]{24}$/);
  assert.equal(render.json().renderViewId, "front-left");
  assert.equal(Object.hasOwn(render.json(), "imageUrl"), false);
});

test("原子 JSON 快照在重启后恢复配置与幂等索引", async (t) => {
  const directory = await mkdtemp(resolve(tmpdir(), "sc01-v2-store-"));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const snapshotPath = resolve(directory, "configurations-v2.json");
  const data = loadSc01V2();
  const configuration = deriveSc01Configuration(selections, data);
  const priceResult = buildSc01PriceResult(configuration, data);
  const firstStore = new ConfigurationStoreV2(
    () => new Date("2026-10-03T00:00:00.000Z"),
    snapshotPath,
  );
  firstStore.create(configuration, priceResult, "persisted-key");

  const snapshot = JSON.parse(await readFile(snapshotPath, "utf8"));
  assert.equal(snapshot.schemaVersion, "1.0.0");
  assert.equal(snapshot.configurations.length, 1);
  assert.equal(snapshot.idempotency.length, 1);

  const restored = new ConfigurationStoreV2(
    () => new Date("2026-10-03T01:00:00.000Z"),
    snapshotPath,
  );
  assert.equal(restored.get(configuration.configurationId)?.revision, 1);
  assert.equal(
    restored.create(configuration, priceResult, "persisted-key").replayed,
    true,
  );
});
