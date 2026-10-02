# UE 展厅体验阶段

目标引擎：Unreal Engine 5.8  
默认地图：`/Game/Maps/L_ConfigShowroom`

## 产品能力

1. 五个稳定机位由 `AConfigShowroomPlayerController` 使用
   `SetViewTargetWithBlend` 平滑切换。
2. `AShowroomEnvironmentActor` 提供影棚与户外两套临时灯光环境。
3. 左右车门、机盖和后备箱均使用独立铰链 Pivot，并复用
   `UReversiblePartActuatorComponent` 支持运动中反向。
4. 车轮使用 `SteeringPivot -> SpinPivot -> Mesh` 两级控制层级，
   转向与滚动分别插值，避免欧拉轴耦合。
5. `UConfiguratorPersistenceSubsystem` 保存配置、环境和 UI 状态，
   启动展厅时自动恢复，配置或环境变化后自动写入。
6. 镜头、活动件、车轮相位和 Path Tracing 累积属于瞬态，不写入存档。
7. `UPathTracingExperienceSubsystem` 使用 UE 5.8 已验证的公开
   ViewMode/ViewState 路径切换模式，并向 C++ UMG 推送采样进度。

## 存档替换

存档写入先序列化到 `.tmp`，再执行：

```text
旧 final -> .bak
新 .tmp -> final
成功后删除 .bak
失败时 .bak -> final
```

加载前先校验 Schema、环境索引和完整四分区选择；全部合法后才修改
运行时状态。不存在或损坏的存档不会覆盖默认配置。

## 临时资源边界

- 车辆、活动覆盖件、车轮和灯光继续标记
  `Configurator.Resource.Temporary`。
- 当前“内饰”机位是面向实体占位 Cabin 的侧向特写，不代表最终车辆内饰
  构图；真实车辆接入后必须重新检查座舱空间、近裁剪面和玻璃。
- 活动件的 Pivot 层级与反向插值是正式约束，角度和局部轴仍需根据真实
  FBX 的 Sidecar 契约校准。

## 自动化验证

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  '/Game/Maps/L_ConfigShowroom' -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

当前覆盖：

- `ConfigurationSystem.Editor.Showroom.GeneratedMap`
- `ConfigurationSystem.Runtime.Configurator.AtomicStage`
- `ConfigurationSystem.Runtime.Experience.AtomicPersistence`
- `ConfigurationSystem.Runtime.Experience.ReversibleActuator`
- `ConfigurationSystem.Runtime.Experience.SmoothWheels`
- 管理员 FBX 预检与内容包安全挂载回归

## GUI 验收

真实 Editor PIE 已验证：

- 五机位按键切换和 `0.65s` Cubic Blend。
- 户外环境切换后灯光、色温和阴影发生一致变化。
- 独立铰链 Pivot 驱动的左右车门可见且不再绕网格中心平移。
- 内饰临时机位不会进入实体 Cube 或被近裁剪面完全遮挡。
- 重启 PIE 后自动恢复配置和环境，但镜头与 Path Tracing 保持默认瞬态。
- Path Tracing UI 从准备态进入累积态并达到 `256 / 256`。

