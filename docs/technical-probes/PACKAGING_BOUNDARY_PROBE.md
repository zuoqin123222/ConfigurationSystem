# Development/Shipping 打包边界探针

状态：通过

验证引擎：Unreal Engine 5.8.1

目标地图：`/Game/Maps/L_ConfigProbe`

## 目标

验证最小 Windows 包满足：

1. 启动进入项目 Content 下的地图，而不是 Engine Entry 地图。
2. 地图及 C++ 硬引用资源进入 Cook。
3. Primary Asset 和软引用纹理进入 Cook。
4. Development 和 Shipping 均能运行。
5. Runtime 包不包含或加载项目 Editor 模块。

## 项目地图

Editor 模块提供幂等命令：

```text
PackagingProbe.CreateProjectMap
```

命令创建或刷新：

```text
/Game/Maps/L_ConfigProbe
```

地图包含一个 `APackagingProbeMarkerActor`：

- `Marker = 1`
- C++ 构造器硬引用 `/Engine/BasicShapes/Cube.Cube`
- 地图中只允许存在一个 Marker

`DefaultEngine.ini` 将 Editor、Game 和 Server 默认地图统一指向项目地图。`DefaultGame.ini` 的 `MapsToCook` 也显式包含该地图，避免仅依赖启动地图的隐式收集。

## Runtime 检查

`-PackagingBoundaryProbe` 在 Cooked 程序启动后检查：

- `RequiresCookedData == true`
- 当前地图是 `/Game/Maps/L_ConfigProbe`
- Marker 数量等于 1
- Cube 硬引用有效
- `ConfigurationSystemEditor` 模块不存在
- `ConfigurationSystemEditor` 模块未加载
- Asset Manager 枚举到 P0-1 Data Asset
- Data Asset 异步加载成功
- Probe Bundle 中的软纹理加载成功

探针不会把 `PKG_FilterEditorOnly` 当成“地图已 Cook”的证据。它只作为 `editorOnlyDataFilteredFromMap` 诊断项；Cook 结论来自 `RequiresCookedData` 和实际加载的项目地图。

## 结果

| 检查 | Development | Shipping |
|---|---:|---:|
| Build/Cook/Stage/Pak/Archive | 通过 | 通过 |
| Cooked Data | 是 | 是 |
| 项目地图启动 | 通过 | 通过 |
| Marker 数量 | 1 | 1 |
| Cube 硬引用 | 通过 | 通过 |
| Primary Data Asset | 通过 | 通过 |
| 软纹理 | 通过 | 通过 |
| Editor 模块存在 | 否 | 否 |
| Editor 模块加载 | 否 | 否 |
| 归档文件数 | 52 | 30 |
| Editor 泄漏文件数 | 0 | 0 |

机器可读结果：

- [Development Runtime JSON](assets/p0-5/packaging-boundary-development.json)
- [Shipping Runtime JSON](assets/p0-5/packaging-boundary-shipping.json)
- [汇总 JSON](assets/p0-5/packaging-boundary-summary.json)

## 文件级扫描

Development 和 Shipping 归档均递归检查文件名：

```text
ConfigurationSystemEditor
UnrealEditor-
UnrealEd
```

两种配置的匹配数量均为 0。

Runtime 还通过 `FModuleManager` 确认：

```text
editorModuleExists = false
editorModuleLoaded = false
```

## 重复执行

### 生成项目地图

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\ue\ConfigurationSystem.uproject' `
  -unattended -NoSplash -NullRHI `
  '-ExecCmds=PackagingProbe.CreateProjectMap,QUIT_EDITOR' -log
```

### 打包

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' `
  BuildCookRun `
  '-Project=D:\ConfigurationSystem\ue\ConfigurationSystem.uproject' `
  -noP4 -platform=Win64 -clientconfig=Development `
  -build -cook -stage -pak -archive `
  '-archivedirectory=<临时 Development 目录>' `
  -unattended -utf8output `
  '-map=/Game/Maps/L_ConfigProbe'
```

Shipping 将 `-clientconfig` 改为 `Shipping`。

运行包时增加：

```text
-PackagingBoundaryProbe
-PackagingBoundaryProbeOutput=<结果 JSON>
```

Windows GUI 程序使用 `Start-Process -Wait`，确保探针完成后再读取报告。

## 边界结论

- `ConfigurationSystemEditor` 只存在于 Editor Target。
- Runtime 模块不依赖 `UnrealEd`、`ToolMenus` 或 Editor 工具类型。
- 地图生成和资产刷新命令只注册在 Editor 模块。
- Windows 项目在 `.uproject` 中显式禁用无关的 `AndroidFileServer`，避免自动写入随机令牌和产生配置漂移。
- Development 和 Shipping 只包含 Runtime 代码及 Cooked 资产。

## 结论

P0-5 在 UE5.8.1 Windows Development 和 Shipping 上通过。项目默认入口已从 Engine Entry 替换为项目地图，硬引用、Primary Asset 和软引用资源均进入 Cook，项目 Editor 模块没有进入运行时归档。
