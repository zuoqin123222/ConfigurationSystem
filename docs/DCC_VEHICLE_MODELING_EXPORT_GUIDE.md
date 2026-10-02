# DCC 车辆模型制作与导出指南

状态：首个原子阶段契约已实现；真实车辆仍需在 DCC 与 UE5.8 中人工验收。

本指南供资产作者和 AI Agent 共同执行。规范性关键词 `必须`、`禁止`、`应` 不可弱化。每次交付包含模型 FBX、动画 FBX、源文件、车辆 sidecar 和动画 sidecar；JSON 必须先通过仓库验证器。

## 稳定规则 ID

规则 ID 是自动验证、AI 修复报告和人工验收的稳定引用；发布后禁止改名、复用或改变原含义。规则扩展必须新增 ID。

| 规则 ID | 稳定要求 |
|---|---|
| `DCC-COORD-001` | 使用固定 UE 厘米坐标并应用对象级变换。 |
| `DCC-NODE-001` | 必需控制节点、标签和父子关系完整且唯一。 |
| `DCC-PIVOT-001` | 每个 Pivot 至少有一个直属 Mesh，轴心符合真实结构。 |
| `DCC-PART-001` | `paint`、`wheel`、`interior`、`frame` 四个分区在 `partBindings` 中恰好各出现一次。 |
| `DCC-PART-002` | 每个分区绑定的目标节点和材质槽都必须存在。 |
| `DCC-MATERIAL-001` | 材质槽 ID 稳定、唯一，并在全部 LOD 中保持同一全集。 |
| `DCC-LOD-001` | LOD 连续编号，屏幕阈值和三角形数严格递减。 |
| `DCC-ANIM-001` | clip 目标存在、标签匹配、帧区间有效且动作可逆。 |
| `DCC-ARTIFACT-001` | artifact `clipId` 唯一，并与 clip 一一对应。 |
| `DCC-AUTH-001` | 授权信息有来源证据并允许 Unreal 导入和渲染。 |
| `DCC-HASH-001` | 源文件和 FBX 的字节数、SHA-256 与实际文件一致。 |

## 1. 固定输入与输出

### 输入

- 已确认授权的车辆源模型。
- DCC：Blender、Maya 或 3ds Max。
- 目标引擎：Unreal Engine 5.8。
- 交换格式：二进制 `FBX 2020.2`。

### 输出

```text
source/<vehicle>.<blend|ma|max>
export/<vehicle>_model.fbx
export/<clip-id>.fbx
vehicle-model.sidecar.json
vehicle-animation.sidecar.json
```

不得把临时缓存、自动备份、贴图烘焙缓存或未授权原文件放进交付目录。

## 2. UE 坐标与变换

| 项 | 固定值 |
|---|---|
| 单位 | centimeter |
| 前方 | +X |
| 右方 | +Y |
| 上方 | +Z |
| 手性 | left |

1. 在 DCC 中将模型尺寸校准为真实厘米。
2. 车头朝 `+X`，车辆右侧朝 `+Y`，车顶朝 `+Z`。
3. `Vehicle.Root` 放在世界原点；车身中心线与 X 轴重合。
4. 导出前应用对象级旋转与缩放。
5. 所有节点 Scale 必须为 `[1,1,1]`；禁止负缩放和未应用的非均匀缩放。
6. 不得通过 FBX 导入器的补偿旋转掩盖错误坐标。

sidecar 固定记录：

```json
{
  "coordinateSystem": {
    "unit": "centimeter",
    "forward": "+X",
    "right": "+Y",
    "up": "+Z",
    "handedness": "left"
  }
}
```

## 3. 节点、标签与父子关系

最小层级：

```text
Vehicle.Root [Vehicle.Root]
└─ Vehicle.Body [Vehicle.Body]
   ├─ BodyMesh
   ├─ InteriorMesh
   ├─ FrameMesh
   ├─ DoorPivot_FL [Vehicle.Part.Door.FrontLeft]
   │  └─ DoorMesh_FL
   ├─ HoodPivot [Vehicle.Part.Hood]
   │  └─ HoodMesh
   ├─ TrunkPivot [Vehicle.Part.Trunk]
   │  └─ TrunkMesh
   ├─ WheelPivot_FL [Vehicle.Part.Wheel.FrontLeft]
   │  └─ WheelMesh_FL
   ├─ WheelPivot_FR [Vehicle.Part.Wheel.FrontRight]
   │  └─ WheelMesh_FR
   ├─ WheelPivot_RL [Vehicle.Part.Wheel.RearLeft]
   │  └─ WheelMesh_RL
   └─ WheelPivot_RR [Vehicle.Part.Wheel.RearRight]
      └─ WheelMesh_RR
```

- 节点名必须唯一。
- 每个控制标签必须全局唯一，只放在 Root、Body 或 Pivot，不放在 Mesh。
- Body 必须直属 Root；所有必需 Pivot 必须直属 Body。
- 每个 Pivot 必须至少有一个直属 Mesh 子节点。
- 可增加右前门和后排车门，标签沿用 `Vehicle.Part.Door.FrontRight`、`RearLeft`、`RearRight`。
- 禁止把活动部件合并到车身，禁止依赖 DCC 集合名或对象数组顺序推断部件。

## 4. Pivot 制作

### 车门、机盖、后备箱

- Pivot 位于真实铰链轴，Mesh 相对 Pivot 必须有符合几何结构的偏移。
- 在 sidecar 动画 clip 中记录局部旋转轴、关闭角和打开角。
- 关闭角代表交付静止姿态；打开过程中不得穿过车身、玻璃或相邻部件。

### 车轮

- Pivot 位于轮心，四轮旋转轴方向一致。
- 轮毂替换不得改变轮心和轮胎外径。
- 轮胎滚动、转向和悬架位移应使用不同控制层；首阶段 sidecar 每个 clip 只绑定一个 Pivot。

## 5. 材质槽

- 使用稳定语义 ID，例如 `paint_body`、`paint_frame`、`wheel_rim`、`tire`、`glass`、`light_front`、`interior_seat`。
- 禁止 `MaterialSlot_0` 等序号名称。
- `materialSlots[].slotId` 必须唯一。
- 每一级 LOD 的 `materialSlotIds` 必须与模型材质槽全集一致；顺序可不同，集合不可增删。
- 车漆、玻璃、灯罩、发光面、轮毂和内饰可配置区域不得共用无法独立替换的槽。

### 5.1 分区绑定

模型 sidecar 必须声明 `partBindings`，把产品配置的四个稳定 `partId` 显式映射到 DCC 节点和材质槽：

```json
{
  "partBindings": [
    {
      "partId": "paint",
      "targetNodes": ["BodyMesh"],
      "materialSlotIds": ["paint_body"]
    },
    {
      "partId": "wheel",
      "targetNodes": ["WheelMesh_FL", "WheelMesh_FR", "WheelMesh_RL", "WheelMesh_RR"],
      "materialSlotIds": ["wheel_rim"]
    },
    {
      "partId": "interior",
      "targetNodes": ["InteriorMesh"],
      "materialSlotIds": ["interior_seat"]
    },
    {
      "partId": "frame",
      "targetNodes": ["FrameMesh"],
      "materialSlotIds": ["paint_frame"]
    }
  ]
}
```

- 数组必须恰有四项，`paint`、`wheel`、`interior`、`frame` 恰好各一次；禁止缺失、重复或增加临时分区。
- `targetNodes` 和 `materialSlotIds` 都必须非空且项内唯一。
- `targetNodes` 中每个名称必须精确引用 `nodes[].name`；禁止按层级位置、前缀或模糊名称推断。
- `materialSlotIds` 中每个 ID 必须精确引用 `materialSlots[].slotId`，并存在于全部 LOD 的材质槽全集。
- 一个分区可以绑定多个目标节点或材质槽，但不得用不存在的占位引用绕过 DCC 制作。

## 6. LOD

1. LOD 从 `0` 连续编号，不得跳号。
2. `screenSize` 从近到远严格递减，范围为 `0..1`。
3. `triangleCount` 必须逐级严格递减。
4. 所有 LOD 保持节点语义、材质槽 ID、UV 用途和法线方向一致。
5. 减面不得破坏门缝、轮拱、玻璃边缘或活动部件闭合轮廓。
6. Nanite 是 UE 导入策略，不替代 sidecar 中可审计的 LOD 信息。

## 7. 动画

- 每个 clip 使用小写 kebab-case `clipId`，只控制一个 `targetNode`。
- `targetNode` 必须存在于车辆 sidecar，`targetTag` 必须与该节点标签一致。
- `endFrame` 必须大于 `startFrame`，`frameRate >= 1`。
- 开合 clip 的 `openDegrees` 与 `closedDegrees` 必须不同。
- 动作必须可逆：`reversible: true`；禁止 Root Motion、循环和时间轴外约束依赖。
- FBX 导出启用 Bake Animation 与 Resample All。
- 每个 clip 恰有对应动画 FBX；artifact 不得引用未知 clip。
- `artifacts[].clipId` 必须唯一，不得用两个文件重复声明同一 clip。
- UE 运行时仍以可逆执行器为准，FBX clip 是制作基准和验收证据，不得用不可逆 Montage 覆盖运行时契约。

## 8. FBX 2020.2 导出

模型 FBX：

- `format: FBX`
- `fbxVersion: 2020.2`
- `binary: true`
- `bakeTransforms: true`
- 只导出交付层级，不导出相机、灯光、DCC helper、隐藏草稿或代理几何。

动画 FBX：

- `fbxVersion: 2020.2`
- `bakeAnimation: true`
- `resampleAll: true`
- 每个文件只包含声明的 clip 和必要层级。

导出后必须重新导入空场景，核对朝向、尺寸、节点名、Pivot、帧区间和材质槽，不能只信任导出面板。

## 9. 授权与哈希

每个 sidecar 必须记录：

- `rightsHolder`：权利人。
- `licenseId`：合同、采购单或授权记录 ID。
- `permittedUses`：至少包含 `unreal-import` 与 `render`。
- `territory`、`expiresOn`、`redistribution`。

无明确授权不得进入仓库或构建产物。`expiresOn: null` 仅表示授权无到期日，不表示未知。

源文件、模型 FBX 和每个动画 FBX 必须记录：

- 相对交付根目录的 `path`。
- 文件字节数 `bytes`。
- 小写十六进制 SHA-256 `sha256`。

PowerShell 计算命令：

```powershell
(Get-FileHash -Algorithm SHA256 '.\export\demo-car_model.fbx').Hash.ToLowerInvariant()
```

任何文件重导出后都必须重新计算哈希；动画 sidecar 的 `modelRef.fbxSha256` 必须等于车辆 sidecar 的 `artifacts.fbx.sha256`。

## 10. AI 执行清单

AI Agent 按顺序执行，失败即停止，不得猜测缺失值：

1. 读取两份 JSON Schema 与本指南。
2. 确认授权字段有来源证据，不生成虚构权利人或 license ID。
3. 检查坐标固定值和所有节点单位缩放。
4. 检查必需节点、唯一标签、父子关系和每个 Pivot 的 Mesh 子节点。
5. 检查四个 `partBindings` 恰好各一次，且目标节点与材质槽引用存在。
6. 检查材质槽唯一且跨 LOD 集合一致。
7. 检查 LOD 连续、屏幕阈值和三角形数严格递减。
8. 检查动画目标节点/标签、帧区间、旋转范围、可逆性、artifact `clipId` 唯一性和闭合关系。
9. 核对模型引用哈希，重新计算全部实际文件哈希。
10. 执行 `node tools/validate-contracts.mjs`。
11. 在 UE5.8 中执行导入、层级审计、动作反向、穿模和 Path Tracing 人工验收。

### 10.1 AI 修复指令

验证失败时，AI 必须按以下顺序修复，不得通过放宽 Schema、删除验证规则或伪造引用使检查变绿：

1. 按上方规则表把错误映射到对应稳定规则 ID，并在修复记录中保留该 ID。
2. 对 `DCC-PART-001`，把 `partBindings` 规范化为固定顺序 `paint`、`wheel`、`interior`、`frame`，合并同分区的有效引用并补齐缺失分区；没有事实依据时停止并请求资产作者确认。
3. 对 `DCC-PART-002`，优先纠正拼写；若节点或材质确实缺失，先在 DCC 源文件中创建并重新导出，再同步 `nodes`、`materialSlots`、全部 LOD 和 `partBindings`，禁止只造 sidecar 占位项。
4. 对 `DCC-ARTIFACT-001`，每个 clip 只保留一个权威 FBX artifact；删除错误重复项或为真实独立动画创建新的唯一 clip，并重新计算字节数与 SHA-256。
5. 修复 DCC 或 FBX 后执行 `DCC-HASH-001`，更新所有受影响哈希及动画 `modelRef.fbxSha256`。
6. 依次运行独立 sidecar 测试和全部契约验证；仍失败时继续按规则 ID 修复，不得跳过失败项。

JSON 通过只证明元数据和跨文件关系符合契约，不证明 FBX 内容、视觉质量、Pivot 物理位置或授权文件真实性。

## 11. 验证命令

```powershell
node tools/validate-vehicle-sidecars.mjs
node --test tools/validate-vehicle-sidecars.test.mjs
node tools/validate-contracts.mjs
```

Schema 与样例位于：

- `contracts/schemas/vehicle-model-sidecar.schema.json`
- `contracts/schemas/vehicle-animation-sidecar.schema.json`
- `contracts/fixtures/vehicle-model.valid.json`
- `contracts/fixtures/vehicle-animation.valid.json`
- `contracts/fixtures/vehicle-model.invalid.json`
- `contracts/fixtures/vehicle-animation.invalid.json`
