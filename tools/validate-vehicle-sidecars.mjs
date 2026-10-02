import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const schemaDir = resolve(root, "contracts", "schemas");
const fixtureDir = resolve(root, "contracts", "fixtures");

const contractFiles = {
  vehicleSchema: resolve(schemaDir, "vehicle-model-sidecar.schema.json"),
  animationSchema: resolve(schemaDir, "vehicle-animation-sidecar.schema.json"),
  validVehicle: resolve(fixtureDir, "vehicle-model.valid.json"),
  validAnimation: resolve(fixtureDir, "vehicle-animation.valid.json"),
  invalidVehicle: resolve(fixtureDir, "vehicle-model.invalid.json"),
  invalidAnimation: resolve(fixtureDir, "vehicle-animation.invalid.json")
};

async function readJson(path) {
  return JSON.parse(await readFile(path, "utf8"));
}

function jsonTypeMatches(value, type) {
  if (type === "null") return value === null;
  if (type === "array") return Array.isArray(value);
  if (type === "object") return value !== null && typeof value === "object" && !Array.isArray(value);
  if (type === "integer") return Number.isInteger(value);
  return typeof value === type;
}

function resolveRef(rootSchema, ref) {
  if (!ref.startsWith("#/")) throw new Error(`仅支持本地 $ref，收到 ${ref}`);
  return ref
    .slice(2)
    .split("/")
    .reduce((value, segment) => value?.[segment.replaceAll("~1", "/").replaceAll("~0", "~")], rootSchema);
}

export function validateJsonSchema(value, schema, rootSchema = schema, path = "$") {
  const errors = [];
  if (schema.$ref) {
    const referenced = resolveRef(rootSchema, schema.$ref);
    return referenced
      ? validateJsonSchema(value, referenced, rootSchema, path)
      : [`${path}: 无法解析 ${schema.$ref}`];
  }

  if (schema.const !== undefined && value !== schema.const) {
    errors.push(`${path}: 必须等于 ${JSON.stringify(schema.const)}`);
  }
  if (schema.enum && !schema.enum.some((item) => item === value)) {
    errors.push(`${path}: 必须是 ${schema.enum.map((item) => JSON.stringify(item)).join("、")} 之一`);
  }

  const types = Array.isArray(schema.type) ? schema.type : schema.type ? [schema.type] : [];
  if (types.length > 0 && !types.some((type) => jsonTypeMatches(value, type))) {
    errors.push(`${path}: 类型必须为 ${types.join(" | ")}`);
    return errors;
  }
  if (value === null) return errors;

  if (typeof value === "string") {
    if (schema.minLength !== undefined && value.length < schema.minLength) {
      errors.push(`${path}: 长度不得小于 ${schema.minLength}`);
    }
    if (schema.pattern && !new RegExp(schema.pattern).test(value)) {
      errors.push(`${path}: 不匹配模式 ${schema.pattern}`);
    }
  }

  if (typeof value === "number") {
    if (schema.minimum !== undefined && value < schema.minimum) {
      errors.push(`${path}: 不得小于 ${schema.minimum}`);
    }
    if (schema.maximum !== undefined && value > schema.maximum) {
      errors.push(`${path}: 不得大于 ${schema.maximum}`);
    }
  }

  if (Array.isArray(value)) {
    if (schema.minItems !== undefined && value.length < schema.minItems) {
      errors.push(`${path}: 元素数不得少于 ${schema.minItems}`);
    }
    if (schema.maxItems !== undefined && value.length > schema.maxItems) {
      errors.push(`${path}: 元素数不得多于 ${schema.maxItems}`);
    }
    if (schema.uniqueItems) {
      const keys = value.map((item) => JSON.stringify(item));
      if (new Set(keys).size !== keys.length) errors.push(`${path}: 元素必须唯一`);
    }
    if (schema.items) {
      value.forEach((item, index) => {
        errors.push(...validateJsonSchema(item, schema.items, rootSchema, `${path}[${index}]`));
      });
    }
  }

  if (value !== null && typeof value === "object" && !Array.isArray(value)) {
    for (const required of schema.required ?? []) {
      if (!Object.hasOwn(value, required)) errors.push(`${path}.${required}: 缺少必需字段`);
    }
    for (const [key, child] of Object.entries(schema.properties ?? {})) {
      if (Object.hasOwn(value, key)) {
        errors.push(...validateJsonSchema(value[key], child, rootSchema, `${path}.${key}`));
      }
    }
    if (schema.additionalProperties === false) {
      const allowed = new Set(Object.keys(schema.properties ?? {}));
      for (const key of Object.keys(value)) {
        if (!allowed.has(key)) errors.push(`${path}.${key}: 不允许额外字段`);
      }
    }
  }
  return errors;
}

const requiredControls = new Map([
  ["Vehicle.Root", { type: "root", parent: null, tag: "Vehicle.Root" }],
  ["Vehicle.Body", { type: "body", parent: "Vehicle.Root", tag: "Vehicle.Body" }],
  ["DoorPivot_FL", { type: "pivot", parent: "Vehicle.Body", tag: "Vehicle.Part.Door.FrontLeft" }],
  ["HoodPivot", { type: "pivot", parent: "Vehicle.Body", tag: "Vehicle.Part.Hood" }],
  ["TrunkPivot", { type: "pivot", parent: "Vehicle.Body", tag: "Vehicle.Part.Trunk" }],
  ["WheelPivot_FL", { type: "pivot", parent: "Vehicle.Body", tag: "Vehicle.Part.Wheel.FrontLeft" }],
  ["WheelPivot_FR", { type: "pivot", parent: "Vehicle.Body", tag: "Vehicle.Part.Wheel.FrontRight" }],
  ["WheelPivot_RL", { type: "pivot", parent: "Vehicle.Body", tag: "Vehicle.Part.Wheel.RearLeft" }],
  ["WheelPivot_RR", { type: "pivot", parent: "Vehicle.Body", tag: "Vehicle.Part.Wheel.RearRight" }]
]);
const requiredPartIds = ["paint", "wheel", "interior", "frame"];

function sameSet(left, right) {
  return left.length === right.length && left.every((item) => right.includes(item));
}

function validateAuthorization(authorization, path) {
  const errors = [];
  for (const use of ["unreal-import", "render"]) {
    if (!authorization?.permittedUses?.includes(use)) {
      errors.push(`${path}.permittedUses: 必须授权 ${use}`);
    }
  }
  return errors;
}

export function validateVehicleSemantics(vehicle) {
  const errors = [...validateAuthorization(vehicle.authorization, "$.authorization")];
  const nodes = vehicle.nodes ?? [];
  const nodeByName = new Map();
  const tags = new Set();
  for (const node of nodes) {
    if (nodeByName.has(node.name)) errors.push(`$.nodes: 节点名重复 ${node.name}`);
    nodeByName.set(node.name, node);
    for (const tag of node.tags ?? []) {
      if (tags.has(tag)) errors.push(`$.nodes: 控制标签重复 ${tag}`);
      tags.add(tag);
    }
    if (node.transform?.scale?.some((axis) => axis !== 1)) {
      errors.push(`$.nodes.${node.name}.transform.scale: 必须为 [1,1,1]`);
    }
  }
  for (const [name, expected] of requiredControls) {
    const node = nodeByName.get(name);
    if (!node) {
      errors.push(`$.nodes: 缺少必需控制节点 ${name}`);
      continue;
    }
    if (node.type !== expected.type) errors.push(`$.nodes.${name}.type: 必须为 ${expected.type}`);
    if (node.parent !== expected.parent) errors.push(`$.nodes.${name}.parent: 必须为 ${expected.parent}`);
    if (!node.tags?.includes(expected.tag)) errors.push(`$.nodes.${name}.tags: 缺少 ${expected.tag}`);
    if (node.type === "pivot" && !nodes.some((child) => child.parent === name && child.type === "mesh")) {
      errors.push(`$.nodes.${name}: Pivot 必须有直接 Mesh 子节点`);
    }
  }

  const slotIds = (vehicle.materialSlots ?? []).map((slot) => slot.slotId);
  if (new Set(slotIds).size !== slotIds.length) errors.push("$.materialSlots: slotId 必须唯一");
  const slotIdSet = new Set(slotIds);
  const partBindingCounts = new Map(requiredPartIds.map((partId) => [partId, 0]));
  for (const [index, binding] of (vehicle.partBindings ?? []).entries()) {
    const path = `$.partBindings[${index}]`;
    if (partBindingCounts.has(binding.partId)) {
      partBindingCounts.set(binding.partId, partBindingCounts.get(binding.partId) + 1);
    }
    for (const nodeName of binding.targetNodes ?? []) {
      if (!nodeByName.has(nodeName)) {
        errors.push(`${path}.targetNodes: 目标节点不存在 ${nodeName}`);
      }
    }
    for (const slotId of binding.materialSlotIds ?? []) {
      if (!slotIdSet.has(slotId)) {
        errors.push(`${path}.materialSlotIds: 材质槽不存在 ${slotId}`);
      }
    }
  }
  for (const [partId, count] of partBindingCounts) {
    if (count === 0) errors.push("$.partBindings: 缺少分区绑定 " + partId);
    if (count > 1) errors.push("$.partBindings: 分区绑定重复 " + partId);
  }
  (vehicle.lods ?? []).forEach((lod, index, lods) => {
    if (lod.level !== index) errors.push(`$.lods[${index}].level: LOD 必须从 0 连续编号`);
    if (!sameSet(lod.materialSlotIds ?? [], slotIds)) {
      errors.push(`$.lods[${index}].materialSlotIds: 必须与稳定材质槽全集一致`);
    }
    if (index > 0 && lod.triangleCount >= lods[index - 1].triangleCount) {
      errors.push(`$.lods[${index}].triangleCount: 必须低于前一级 LOD`);
    }
    if (index > 0 && lod.screenSize >= lods[index - 1].screenSize) {
      errors.push(`$.lods[${index}].screenSize: 必须低于前一级 LOD`);
    }
  });
  return errors;
}

export function validateAnimationSemantics(animation, vehicle) {
  const errors = [...validateAuthorization(animation.authorization, "$.authorization")];
  const nodeByName = new Map((vehicle.nodes ?? []).map((node) => [node.name, node]));
  const clipIds = new Set();
  for (const [index, clip] of (animation.clips ?? []).entries()) {
    const path = `$.clips[${index}]`;
    if (clipIds.has(clip.clipId)) errors.push(`${path}.clipId: clipId 必须唯一`);
    clipIds.add(clip.clipId);
    const node = nodeByName.get(clip.targetNode);
    if (!node) errors.push(`${path}.targetNode: 模型中不存在 ${clip.targetNode}`);
    else if (!node.tags?.includes(clip.targetTag)) errors.push(`${path}.targetTag: 与模型节点标签不一致`);
    if (clip.endFrame <= clip.startFrame) errors.push(`${path}.endFrame: 必须大于 startFrame`);
    if (clip.openDegrees === clip.closedDegrees) errors.push(`${path}.openDegrees: 必须与 closedDegrees 不同`);
  }
  const artifactClipIds = new Set();
  for (const [index, artifact] of (animation.artifacts ?? []).entries()) {
    if (artifactClipIds.has(artifact.clipId)) {
      errors.push(`$.artifacts[${index}].clipId: artifact clipId 必须唯一`);
    }
    artifactClipIds.add(artifact.clipId);
  }
  for (const clipId of clipIds) {
    if (!artifactClipIds.has(clipId)) errors.push(`$.artifacts: 缺少 clip ${clipId} 的 FBX`);
  }
  for (const artifact of animation.artifacts ?? []) {
    if (!clipIds.has(artifact.clipId)) errors.push(`$.artifacts: 引用了未知 clip ${artifact.clipId}`);
  }
  if (animation.modelRef?.vehicleId !== vehicle.vehicleId) {
    errors.push("$.modelRef.vehicleId: 与车辆 sidecar 不一致");
  }
  if (animation.modelRef?.modelVersion !== vehicle.modelVersion) {
    errors.push("$.modelRef.modelVersion: 与车辆 sidecar 不一致");
  }
  if (animation.modelRef?.fbxSha256 !== vehicle.artifacts?.fbx?.sha256) {
    errors.push("$.modelRef.fbxSha256: 与车辆 FBX 哈希不一致");
  }
  return errors;
}

export function validateVehicle(vehicle, schema) {
  return [...validateJsonSchema(vehicle, schema), ...validateVehicleSemantics(vehicle)];
}

export function validateAnimation(animation, schema, vehicle) {
  return [
    ...validateJsonSchema(animation, schema),
    ...validateAnimationSemantics(animation, vehicle)
  ];
}

export async function validateSidecarFixtures() {
  const [vehicleSchema, animationSchema, vehicle, animation, invalidVehicle, invalidAnimation] =
    await Promise.all(Object.values(contractFiles).map(readJson));
  const validVehicleErrors = validateVehicle(vehicle, vehicleSchema);
  const validAnimationErrors = validateAnimation(animation, animationSchema, vehicle);
  const invalidVehicleErrors = validateVehicle(invalidVehicle, vehicleSchema);
  const invalidAnimationErrors = validateAnimation(invalidAnimation, animationSchema, vehicle);
  const failures = [];
  if (validVehicleErrors.length) failures.push(...validVehicleErrors.map((error) => `有效车辆 fixture: ${error}`));
  if (validAnimationErrors.length) failures.push(...validAnimationErrors.map((error) => `有效动画 fixture: ${error}`));
  if (!invalidVehicleErrors.length) failures.push("无效车辆 fixture 未被拒绝");
  if (!invalidAnimationErrors.length) failures.push("无效动画 fixture 未被拒绝");
  return {
    failures,
    rejected: {
      vehicle: invalidVehicleErrors,
      animation: invalidAnimationErrors
    },
    summary: "2 个 sidecar Schema、2 个有效 fixture、2 个无效 fixture"
  };
}

async function main() {
  const result = await validateSidecarFixtures();
  if (result.failures.length) {
    console.error(`车辆 sidecar 验证失败（${result.failures.length} 项）：`);
    result.failures.forEach((failure) => console.error(`- ${failure}`));
    process.exitCode = 1;
    return;
  }
  console.log(`车辆 sidecar 验证通过：${result.summary}；无效 fixture 均按预期被拒绝。`);
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  await main();
}
