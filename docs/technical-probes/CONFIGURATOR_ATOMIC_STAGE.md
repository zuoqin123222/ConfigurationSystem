# UE 展厅首个原子阶段

目标引擎：Unreal Engine 5.8  
默认地图：`/Game/Maps/L_ConfigShowroom`

## 已实现闭环

1. `AConfiguratorVehicleActor` 使用 `/Engine/BasicShapes/Cube`、`Cylinder` 和
   `BasicShapeMaterial` 组成占位车辆。
2. `UCarConfiguratorSubsystem` 在 GameInstance 生命周期内持有
   `UCarConfigurationState`，集中提供选项、模板、canonical key 与价格。
3. `UConfiguratorPanel` 完全由 C++ 构造，显示四个分区、每区两个选项、
   `sport`/`luxury` 两个模板、canonical key 和人民币总价。
4. `AConfigShowroomGameMode` 与 `AConfigShowroomPlayerController` 创建面板、
   启用鼠标，并切换到带 `Configurator.ShowroomCamera` 标签的相机。
5. Editor 命令 `ConfigurationSystem.CreateShowroomMap` 创建或刷新默认展厅。

## 稳定绑定

四分区不依赖组件显示名，使用固定 ComponentTag：

| partId | 分区标签 | 逻辑材质槽标签 |
|---|---|---|
| `paint` | `Configurator.Part.paint` | `Configurator.Slot.paint_body` |
| `wheel` | `Configurator.Part.wheel` | `Configurator.Slot.wheel_rim` |
| `interior` | `Configurator.Part.interior` | `Configurator.Slot.interior_trim` |
| `frame` | `Configurator.Part.frame` | `Configurator.Slot.paint_frame` |

占位 Actor、组件、地台和临时灯光使用
`Configurator.Resource.Temporary` 或 `_TEMP` 标签/名称。它们不是正式车辆、
正式材质或最终展厅资产；后续替换时必须保留上表绑定语义。

## 幂等生成地图

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  '/Game/Maps/L_ConfigProbe' -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=ConfigurationSystem.CreateShowroomMap,Quit' -log
```

命令按稳定 Actor Label 查找并复用对象，同时删除同标签重复项。重复执行后，
地图内固定保留一台占位车、一块地台、一个默认相机、一盏主光、一盏补光和
一盏天空光；地图覆盖 GameMode 为 `AConfigShowroomGameMode`。

## 自动化探针

Runtime 原子闭环：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  '/Game/Maps/L_ConfigShowroom' -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Runtime.Configurator.AtomicStage;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

地图生成结果：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  '/Game/Maps/L_ConfigShowroom' -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Editor.Showroom.GeneratedMap;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

Runtime 探针覆盖 8 个选项、2 个模板、默认与豪华模板的 key/价格、四分区
稳定标签/逻辑槽、临时资源声明、UMG 控件数量以及 GameMode/Controller 绑定。
Editor 探针覆盖地图可加载、GameMode 覆盖和六类生成 Actor 各自唯一。
