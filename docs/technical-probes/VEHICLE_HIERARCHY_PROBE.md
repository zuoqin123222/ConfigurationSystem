# 车辆层级与可逆动作探针

状态：程序原型通过，真实车辆资产待接入

结果码：`PROTOTYPE_PASS_ASSET_PENDING`

验证引擎：Unreal Engine 5.8.1

## 目标

验证车辆部件层级能够被 C++ 自动审计，并实现满足以下要求的动作执行器：

- 车门、机盖、后备箱和车轮使用独立 Pivot。
- 运行时按标签查找部件，不依赖 Blueprint 硬连线。
- 支持打开、关闭和切换。
- 运动途中可以连续反向。
- 重复操作不会累积误差。
- 错误层级能够被自动拒绝。

当前工程没有真实车辆资产，因此本探针使用程序生成的 Cube 层级。它验证代码和资产契约，不代表真实车型已经通过。

## 标签规范

| 组件 | 必需标签 |
|---|---|
| Actor 根组件 | `Vehicle.Root` |
| 车身主体 | `Vehicle.Body` |
| 左前门 Pivot | `Vehicle.Part.Door.FrontLeft` |
| 机盖 Pivot | `Vehicle.Part.Hood` |
| 后备箱 Pivot | `Vehicle.Part.Trunk` |
| 左前轮 Pivot | `Vehicle.Part.Wheel.FrontLeft` |
| 右前轮 Pivot | `Vehicle.Part.Wheel.FrontRight` |
| 左后轮 Pivot | `Vehicle.Part.Wheel.RearLeft` |
| 右后轮 Pivot | `Vehicle.Part.Wheel.RearRight` |

每个必需标签必须：

- 只出现一次。
- 标记在 `Movable` 的 `USceneComponent` 上。
- `Vehicle.Root` 是 Actor 根组件。
- `Vehicle.Body` 直接挂到 Root。
- 所有部件 Pivot 直接挂到 Body。

部件网格挂到对应 Pivot，并保持无控制标签。标签标记控制节点，不标记可视网格。

## 门铰链结构

```text
Vehicle.Root
└─ Vehicle.Body
   ├─ DoorPivot [Vehicle.Part.Door.FrontLeft]
   │  └─ DoorMesh [无标签，相对 Pivot 偏移]
   ├─ HoodPivot [Vehicle.Part.Hood]
   │  └─ HoodMesh
   ├─ TrunkPivot [Vehicle.Part.Trunk]
   │  └─ TrunkMesh
   └─ Wheel Pivots × 4
      └─ Wheel Mesh
```

`DoorPivot` 位于真实铰链轴。`DoorMesh` 必须相对 Pivot 存在非零偏移，禁止直接绕网格中心旋转。

## 可逆执行器

`UReversiblePartActuatorComponent` 只维护一个 `[0,1]` 线性进度：

```cpp
Progress = FMath::FInterpConstantTo(
    Progress,
    TargetProgress,
    DeltaTime,
    1.0f / Duration);

const float Eased = FMath::SmoothStep(0.0f, 1.0f, Progress);
Result.Blend(ClosedTransform, OpenTransform, Eased);
```

`SetOpen()` 只修改目标值，不重建插值起点。中途反向时 Transform 在命令前后保持一致，下一帧沿相反方向继续。

公开接口：

- `BindPart()`
- `SetOpen()`
- `Toggle()`
- `GetProgress()`
- `IsMoving()`

## 自动验证结果

### 正向层级

- 9 个必需标签均出现一次。
- 所有控制组件均为 `Movable`。
- 所有部件 Pivot 直接挂到 Body。
- DoorMesh 是 DoorPivot 的直接子项。
- DoorMesh 无控制标签。
- DoorMesh 相对铰链 Pivot 存在偏移。
- 执行器绑定 DoorPivot。

### 负向层级

| 错误夹具 | 审计拒绝 | 原因匹配 | 恢复后通过 |
|---|---:|---:|---:|
| 删除 Hood 标签 | 是 | 是 | 是 |
| Body 重复 Hood 标签 | 是 | 是 | 是 |
| TrunkPivot 设为 Static | 是 | 是 | 是 |
| 右后轮改挂 Root | 是 | 是 | 是 |

### 动作测试

| 测试 | 结果 |
|---|---:|
| 相同 `SetOpen(true)` 幂等 | 通过 |
| 约 40% 时反向关闭 | 通过 |
| 完整打开再关闭 | 通过 |
| 连续三轮打开/关闭 | 通过 |
| 最大命令跳变 | 0 |
| 最大单步跳变 | 2.16642° |
| 单步阈值 | 2.2° |
| 最大插值误差 | 0.00000296 |
| 最大终态误差 | 0 |

机器可读结果：

- [P0-4 JSON](assets/p0-4/vehicle-hierarchy-probe-results.json)

## 重复执行

```powershell
$Arguments = @(
  'D:\ConfigurationSystem\ue\ConfigurationSystem.uproject',
  '-game',
  '-unattended',
  '-NoSplash',
  '-NoSound',
  '-windowed',
  '-ResX=320',
  '-ResY=180',
  '-VehicleHierarchyProbe',
  '-VehicleHierarchyProbeOutput=<输出 JSON>'
)

Start-Process `
  -FilePath 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  -ArgumentList $Arguments `
  -WorkingDirectory 'D:\ConfigurationSystem\ue' `
  -Wait
```

## 真实资产门槛

真实车型导入后，P0-4 只有同时满足以下条件才能改为完全通过：

1. 真实车辆 Actor 通过同一标签和父子关系审计。
2. 车门、机盖、后备箱和四轮 Pivot 位置及旋转轴经视觉确认。
3. 至少一个真实车门完成中途反向、完整开关和三轮重复测试。
4. 动画过程中没有穿模、网格中心旋转或比例漂移。
5. 关闭终态与原始模型 Transform 一致。
6. Path Tracing 模式下动作会重置采样进度。

## 结论

层级审计器和可逆部件执行器已经通过程序化验证，可进入业务代码复用。真实车辆缺失是当前唯一未闭合项，因此 P0-4 状态保持 `PROTOTYPE_PASS_ASSET_PENDING`，不能标记为完全通过。
