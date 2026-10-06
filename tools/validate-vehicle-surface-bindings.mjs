import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { validateJsonSchema } from "./validate-vehicle-sidecars.mjs";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const fixtureDir = resolve(root, "contracts", "fixtures");
const schemaPath = resolve(
  root,
  "contracts",
  "schemas",
  "vehicle-surface-binding.schema.json"
);
const catalogPath = resolve(fixtureDir, "sc01.catalog.draft.v2.json");
const validPath = resolve(fixtureDir, "vehicle-surface-binding.valid.json");
const invalidPath = resolve(fixtureDir, "vehicle-surface-binding.invalid.json");

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

function expandFaceRange(value) {
  if (value === "*") return null;
  const [startText, endText = startText] = value.split(":");
  const start = Number(startText);
  const end = Number(endText);
  if (end < start) return [];
  return Array.from({ length: end - start + 1 }, (_, index) => start + index);
}

export function validateSurfaceBindingSemantics(contract, catalog) {
  const errors = [];
  if (contract.vehicleId !== catalog.vehicle?.vehicleId) {
    errors.push("$.vehicleId: 必须与 catalog.vehicle.vehicleId 一致");
  }
  if (contract.catalogVersion !== catalog.catalogVersion) {
    errors.push("$.catalogVersion: 必须与 catalog.catalogVersion 一致");
  }

  const expectedSurfaceIds = catalog.selectionOrder ?? [];
  const catalogSurfaceIds = new Set((catalog.surfaces ?? []).map((surface) => surface.surfaceId));
  const actualSurfaceIds = (contract.bindings ?? []).map((binding) => binding.surfaceId);
  if (
    actualSurfaceIds.length !== expectedSurfaceIds.length
    || actualSurfaceIds.some((surfaceId, index) => surfaceId !== expectedSurfaceIds[index])
  ) {
    errors.push(
      `$.bindings: 必须按 catalog.selectionOrder 显式覆盖全部 ${expectedSurfaceIds.length} 个 surface`
    );
  }

  const seenSurfaces = new Set();
  const seenSlots = new Set();
  const selectedFaces = new Map();
  for (const [bindingIndex, binding] of (contract.bindings ?? []).entries()) {
    const path = `$.bindings[${bindingIndex}]`;
    if (!catalogSurfaceIds.has(binding.surfaceId)) {
      errors.push(`${path}.surfaceId: catalog 中不存在 ${binding.surfaceId}`);
    }
    if (seenSurfaces.has(binding.surfaceId)) {
      errors.push(`${path}.surfaceId: surfaceId 重复 ${binding.surfaceId}`);
    }
    seenSurfaces.add(binding.surfaceId);
    if (seenSlots.has(binding.materialSlotId)) {
      errors.push(`${path}.materialSlotId: materialSlotId 重复 ${binding.materialSlotId}`);
    }
    seenSlots.add(binding.materialSlotId);

    for (const [selectorIndex, selector] of (binding.selectors ?? []).entries()) {
      const selectorPath = `${path}.selectors[${selectorIndex}]`;
      const key = `${selector.targetNode}\0${selector.sourceMaterialId}`;
      const state = selectedFaces.get(key) ?? { all: false, faces: new Set() };
      for (const range of selector.faceRanges ?? []) {
        const faces = expandFaceRange(range);
        if (faces === null) {
          if (state.all || state.faces.size > 0) {
            errors.push(`${selectorPath}.faceRanges: 通配选择与已有显式面选择重叠`);
          }
          state.all = true;
          continue;
        }
        if (faces.length === 0) {
          errors.push(`${selectorPath}.faceRanges: 面范围结束值不得小于开始值`);
        }
        for (const face of faces) {
          if (state.all || state.faces.has(face)) {
            errors.push(
              `${selectorPath}.faceRanges: ${selector.targetNode}/${selector.sourceMaterialId} 的面 ${face} 重复分配`
            );
            break;
          }
          state.faces.add(face);
        }
      }
      selectedFaces.set(key, state);
    }
  }
  return errors;
}

export function validateSurfaceBinding(contract, schema, catalog) {
  return [
    ...validateJsonSchema(contract, schema),
    ...validateSurfaceBindingSemantics(contract, catalog)
  ];
}

export async function validateSurfaceBindingFixtures() {
  const [schema, catalog, valid, invalid] = await Promise.all(
    [schemaPath, catalogPath, validPath, invalidPath].map(readJson)
  );
  const validErrors = validateSurfaceBinding(valid, schema, catalog);
  const invalidErrors = validateSurfaceBinding(invalid, schema, catalog);
  const failures = [];
  if (validErrors.length) {
    failures.push(...validErrors.map((error) => `有效 surface-binding fixture: ${error}`));
  }
  if (!invalidErrors.length) failures.push("无效 surface-binding fixture 未被拒绝");
  return {
    failures,
    rejected: invalidErrors,
    summary: `1 个 Schema、${valid.bindings.length} 个 SC01 surface binding、正反 fixture`
  };
}

async function main() {
  const result = await validateSurfaceBindingFixtures();
  if (result.failures.length) {
    console.error(`车辆 surface-binding 验证失败（${result.failures.length} 项）：`);
    result.failures.forEach((failure) => console.error(`- ${failure}`));
    process.exitCode = 1;
    return;
  }
  console.log(`车辆 surface-binding 验证通过：${result.summary}；无效 fixture 按预期被拒绝。`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  await main();
}
