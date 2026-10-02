import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import test from "node:test";
import {
  validateAnimation,
  validateSidecarFixtures,
  validateVehicle
} from "./validate-vehicle-sidecars.mjs";

const root = resolve(import.meta.dirname, "..");

async function json(relativePath) {
  return JSON.parse(await readFile(resolve(root, relativePath), "utf8"));
}

const [vehicleSchema, animationSchema, validVehicle, validAnimation] = await Promise.all([
  json("contracts/schemas/vehicle-model-sidecar.schema.json"),
  json("contracts/schemas/vehicle-animation-sidecar.schema.json"),
  json("contracts/fixtures/vehicle-model.valid.json"),
  json("contracts/fixtures/vehicle-animation.valid.json")
]);

function clone(value) {
  return structuredClone(value);
}

test("有效车辆与动画 sidecar 通过 Schema 和跨文件语义验证", () => {
  assert.deepEqual(validateVehicle(validVehicle, vehicleSchema), []);
  assert.deepEqual(validateAnimation(validAnimation, animationSchema, validVehicle), []);
});

test("仓库 fixture 集合要求有效样例通过、无效样例被拒绝", async () => {
  const result = await validateSidecarFixtures();
  assert.deepEqual(result.failures, []);
  assert.ok(result.rejected.vehicle.length >= 8);
  assert.ok(result.rejected.animation.length >= 6);
});

test("车辆 sidecar 拒绝非 UE 坐标、非 FBX 2020.2 和非法哈希", () => {
  const vehicle = clone(validVehicle);
  vehicle.coordinateSystem.forward = "+Y";
  vehicle.export.fbxVersion = "2019";
  vehicle.artifacts.fbx.sha256 = "ABC";
  const errors = validateVehicle(vehicle, vehicleSchema);
  assert.ok(errors.some((error) => error.includes("$.coordinateSystem.forward")));
  assert.ok(errors.some((error) => error.includes("$.export.fbxVersion")));
  assert.ok(errors.some((error) => error.includes("$.artifacts.fbx.sha256")));
});

test("车辆 sidecar 拒绝重复标签、错误 Pivot、材质槽漂移和非递减 LOD", () => {
  const vehicle = clone(validVehicle);
  vehicle.nodes.find((node) => node.name === "HoodPivot").tags = [
    "Vehicle.Part.Door.FrontLeft"
  ];
  vehicle.nodes.find((node) => node.name === "DoorPivot_FL").transform.scale = [1, 2, 1];
  vehicle.nodes = vehicle.nodes.filter((node) => node.parent !== "TrunkPivot");
  vehicle.lods[1].materialSlotIds = ["paint_body", "glass"];
  vehicle.lods[1].triangleCount = vehicle.lods[0].triangleCount;
  const errors = validateVehicle(vehicle, vehicleSchema);
  assert.ok(errors.some((error) => error.includes("控制标签重复")));
  assert.ok(errors.some((error) => error.includes("scale: 必须为 [1,1,1]")));
  assert.ok(errors.some((error) => error.includes("TrunkPivot: Pivot 必须有直接 Mesh")));
  assert.ok(errors.some((error) => error.includes("稳定材质槽全集一致")));
  assert.ok(errors.some((error) => error.includes("必须低于前一级 LOD")));
});

test("车辆 sidecar 要求授权 Unreal 导入与渲染", () => {
  const vehicle = clone(validVehicle);
  vehicle.authorization.permittedUses = ["modify", "distribute-render"];
  const errors = validateVehicle(vehicle, vehicleSchema);
  assert.ok(errors.some((error) => error.includes("必须授权 unreal-import")));
  assert.ok(errors.some((error) => error.includes("必须授权 render")));
});

test("车辆 sidecar 要求 paint、wheel、interior、frame 四分区恰好绑定一次", () => {
  const vehicle = clone(validVehicle);
  vehicle.partBindings = [
    { partId: "paint", targetNodes: ["BodyMesh"], materialSlotIds: ["paint_body"] },
    { partId: "wheel", targetNodes: ["WheelMesh_FL"], materialSlotIds: ["wheel_rim"] },
    { partId: "interior", targetNodes: ["InteriorMesh"], materialSlotIds: ["interior_seat"] },
    { partId: "paint", targetNodes: ["FrameMesh"], materialSlotIds: ["paint_frame"] }
  ];
  const errors = validateVehicle(vehicle, vehicleSchema);
  assert.ok(errors.some((error) => error.includes("分区绑定重复 paint")));
  assert.ok(errors.some((error) => error.includes("缺少分区绑定 frame")));
});

test("车辆 sidecar 拒绝 partBindings 中悬空的目标节点与材质槽引用", () => {
  const vehicle = clone(validVehicle);
  vehicle.partBindings = [
    { partId: "paint", targetNodes: ["MissingMesh"], materialSlotIds: ["paint_body"] },
    { partId: "wheel", targetNodes: ["WheelMesh_FL"], materialSlotIds: ["missing_slot"] },
    { partId: "interior", targetNodes: ["InteriorMesh"], materialSlotIds: ["interior_seat"] },
    { partId: "frame", targetNodes: ["FrameMesh"], materialSlotIds: ["paint_frame"] }
  ];
  const errors = validateVehicle(vehicle, vehicleSchema);
  assert.ok(errors.some((error) => error.includes("目标节点不存在 MissingMesh")));
  assert.ok(errors.some((error) => error.includes("材质槽不存在 missing_slot")));
});

test("动画 sidecar 拒绝未知节点、无位移动画和错误帧区间", () => {
  const animation = clone(validAnimation);
  animation.clips[0].targetNode = "UnknownPivot";
  animation.clips[0].endFrame = animation.clips[0].startFrame;
  animation.clips[0].openDegrees = animation.clips[0].closedDegrees;
  const errors = validateAnimation(animation, animationSchema, validVehicle);
  assert.ok(errors.some((error) => error.includes("模型中不存在 UnknownPivot")));
  assert.ok(errors.some((error) => error.includes("必须大于 startFrame")));
  assert.ok(errors.some((error) => error.includes("必须与 closedDegrees 不同")));
});

test("动画 sidecar 拒绝模型哈希不匹配和 clip/artifact 不闭合", () => {
  const animation = clone(validAnimation);
  animation.modelRef.fbxSha256 =
    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  animation.artifacts[0].clipId = "orphan-clip";
  const errors = validateAnimation(animation, animationSchema, validVehicle);
  assert.ok(errors.some((error) => error.includes("车辆 FBX 哈希不一致")));
  assert.ok(errors.some((error) => error.includes("缺少 clip door-front-left-open")));
  assert.ok(errors.some((error) => error.includes("引用了未知 clip orphan-clip")));
});

test("动画 sidecar 拒绝重复的 artifact clipId", () => {
  const animation = clone(validAnimation);
  animation.artifacts[1].clipId = animation.artifacts[0].clipId;
  const errors = validateAnimation(animation, animationSchema, validVehicle);
  assert.ok(errors.some((error) => error.includes("artifact clipId 必须唯一")));
});
