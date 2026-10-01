# 车辆资产接入规范

本规范用于真实演示车导入 UE5.8 前的资产验收。未满足必需项的车辆不得进入选配逻辑和自动出图流程。

## 基础坐标

- 单位：厘米。
- 坐标：X 前、Y 右、Z 上。
- 车辆中心线与 X 轴一致。
- 根节点位置建议为世界原点。
- 所有控制 Pivot 的 Scale 必须为 `(1,1,1)`。
- 禁止负缩放和未应用的非均匀缩放。

## 必需层级

```text
Vehicle.Root
└─ Vehicle.Body
   ├─ DoorPivot_FL
   │  └─ DoorMesh_FL
   ├─ HoodPivot
   │  └─ HoodMesh
   ├─ TrunkPivot
   │  └─ TrunkMesh
   ├─ WheelPivot_FL
   │  └─ WheelMesh_FL
   ├─ WheelPivot_FR
   │  └─ WheelMesh_FR
   ├─ WheelPivot_RL
   │  └─ WheelMesh_RL
   └─ WheelPivot_RR
      └─ WheelMesh_RR
```

控制标签统一添加到 Pivot，不添加到 Mesh：

| Pivot | ComponentTag |
|---|---|
| Root | `Vehicle.Root` |
| Body | `Vehicle.Body` |
| 左前门 | `Vehicle.Part.Door.FrontLeft` |
| 机盖 | `Vehicle.Part.Hood` |
| 后备箱 | `Vehicle.Part.Trunk` |
| 左前轮 | `Vehicle.Part.Wheel.FrontLeft` |
| 右前轮 | `Vehicle.Part.Wheel.FrontRight` |
| 左后轮 | `Vehicle.Part.Wheel.RearLeft` |
| 右后轮 | `Vehicle.Part.Wheel.RearRight` |

如果车型包含更多车门，可继续使用：

```text
Vehicle.Part.Door.FrontRight
Vehicle.Part.Door.RearLeft
Vehicle.Part.Door.RearRight
```

## Pivot 要求

### 车门

- Pivot 位于实际铰链轴。
- Z 轴或明确记录的局部轴沿铰链方向。
- 车门网格相对 Pivot 存在偏移。
- 打开角度由车型配置定义，不写死在网格。

### 机盖与后备箱

- Pivot 位于真实铰链线。
- 左右铰链不一致时使用一个经过确认的等效旋转轴。
- 打开过程中不得穿过车身或玻璃。

### 车轮

- Pivot 位于轮心。
- 四轮旋转轴方向一致。
- 轮毂替换不得改变轮胎外径和轮心。
- 轮胎转动与悬架弹动分离为两个控制层。

## 材质槽

每个可选配区域必须有稳定、唯一、跨 LOD 一致的槽位 ID。建议命名：

```text
paint_body
paint_frame
trim_exterior
wheel_rim
tire
glass
light_front
light_rear
interior_seat
interior_trim
interior_screen
```

禁止使用 `MaterialSlot_0`、`MaterialSlot_1` 等无语义名称进入正式资产。

## 灯光与屏幕

- 车灯发光面和灯罩分离。
- 前灯、尾灯、日行灯使用独立材质槽或参数集合。
- 驾驶室屏幕使用独立材质槽。
- 需要 Bloom 的发光内容必须参加 P0-3 Alpha 规范化复测。

## 几何与渲染

- 车身、玻璃、内饰和活动部件不得合并成无法单独控制的单一网格。
- LOD 间材质槽 ID 保持一致。
- Nanite 开关按部件验证，活动部件不得因设置失去变形或材质能力。
- 法线、切线和 UV 无明显错误。
- 玻璃内外表面与法线方向经过 Path Tracing 验证。
- 关闭状态不得存在可见缝隙或重叠面闪烁。

## 导入验收

导入后依次执行：

1. 运行 `UVehicleHierarchyAuditor::AuditActor`。
2. 记录所有 ComponentTag、父节点和 Mobility。
3. 单独验证左前门 Pivot。
4. 验证机盖和后备箱。
5. 验证四轮轮心及旋转方向。
6. 运行中途反向和三轮重复动作。
7. 在实时模式和 Path Tracing 模式中检查穿模。
8. 对车漆、玻璃和车灯执行 P0-3 透明出图。

## 交付清单

资产方交付时应包含：

- 模型源文件和 UE 导入资产。
- 车型版本号。
- 部件命名表。
- 材质槽清单。
- 活动部件 Pivot 说明。
- 推荐开合角度。
- 已知穿模或简化项。
- LOD/Nanite 策略。
- 授权和使用范围说明。
