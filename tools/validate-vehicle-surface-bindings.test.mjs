import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import test from "node:test";
import {
  validateSurfaceBinding,
  validateSurfaceBindingFixtures
} from "./validate-vehicle-surface-bindings.mjs";

const root = resolve(import.meta.dirname, "..");
const json = async (path) => JSON.parse(await readFile(resolve(root, path), "utf8"));
const [schema, catalog, valid] = await Promise.all([
  json("contracts/schemas/vehicle-surface-binding.schema.json"),
  json("contracts/fixtures/sc01.catalog.draft.v2.json"),
  json("contracts/fixtures/vehicle-surface-binding.valid.json")
]);

test("SC01 surface-binding 按 selectionOrder 显式覆盖 38 个 surface", () => {
  assert.deepEqual(validateSurfaceBinding(valid, schema, catalog), []);
  assert.equal(valid.bindings.length, 38);
  assert.deepEqual(
    valid.bindings.map((binding) => binding.surfaceId),
    catalog.selectionOrder
  );
  assert.equal(
    new Set(valid.bindings.flatMap((binding) => binding.materialSlotIds)).size,
    38
  );
  assert.deepEqual(valid.unsupportedSurfaceIds, []);
});

test("正反 fixture 聚合验证通过", async () => {
  const result = await validateSurfaceBindingFixtures();
  assert.deepEqual(result.failures, []);
  assert.ok(result.rejected.some((error) => error.includes("全部 38 个 surface")));
  assert.ok(result.rejected.some((error) => error.includes("unknown-surface")));
  assert.ok(result.rejected.some((error) => error.includes("material slot 重复")));
  assert.ok(result.rejected.some((error) => error.includes("重复分配")));
});

test("拒绝调换稳定 surface 顺序或复用 material slot", () => {
  const candidate = structuredClone(valid);
  [candidate.bindings[0], candidate.bindings[1]] = [
    candidate.bindings[1],
    candidate.bindings[0]
  ];
  candidate.bindings[1].materialSlotIds = [candidate.bindings[0].materialSlotIds[0]];
  const errors = validateSurfaceBinding(candidate, schema, catalog);
  assert.ok(errors.some((error) => error.includes("catalog.selectionOrder")));
  assert.ok(errors.some((error) => error.includes("material slot 重复")));
});

test("拒绝同一来源材质面被多个 surface 重叠选择", () => {
  const candidate = structuredClone(valid);
  candidate.bindings[1].selectors = [{
    targetNode: "ExteriorMesh",
    sourceMaterialId: "SRC_EXTERIOR_BODY_COVER",
    faceRanges: ["0:9"]
  }];
  candidate.bindings[0].selectors[0].faceRanges = ["5:12"];
  const errors = validateSurfaceBinding(candidate, schema, catalog);
  assert.ok(errors.some((error) => error.includes("面 5 重复分配")));
});

test("代理 capability 必须以绑定和显式缺口无重叠覆盖全部 surface", () => {
  const proxy = structuredClone(valid);
  proxy.capability = "proxy";
  proxy.bindings = [proxy.bindings[0], proxy.bindings[22]];
  proxy.unsupportedSurfaceIds = catalog.selectionOrder.filter(
    (surfaceId) => !proxy.bindings.some((binding) => binding.surfaceId === surfaceId)
  );
  assert.deepEqual(validateSurfaceBinding(proxy, schema, catalog), []);

  proxy.bindings[1].materialSlotIds = [proxy.bindings[0].materialSlotIds[0]];
  assert.ok(validateSurfaceBinding(proxy, schema, catalog)
    .some((error) => error.includes("material slot 重复")));
});

test("一个 surface 可数据驱动映射到多个互斥 slot", () => {
  const candidate = structuredClone(valid);
  candidate.bindings[0].materialSlotIds.push("sc01_exterior_body_cover_secondary");
  assert.deepEqual(validateSurfaceBinding(candidate, schema, catalog), []);
});
