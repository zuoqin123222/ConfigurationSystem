# UE5 客户端

Windows 桌面客户端基于 Unreal Engine 5.8，以 Runtime C++ 模块 `ConfigurationSystem` 为业务核心，Editor-only 资产生成和审计工具位于 `ConfigurationSystemEditor`。

内嵌选配 UI 由 `source/clients/web/` 的唯一 React 源码构建。先在 Web 目录执行
`npm run build`，同一产物会部署到被忽略的 `Content/WebUI/`；UE 以 `file://`
加载该目录，并通过 NonUFS Runtime Dependency 把 bundle、catalog、icon 和
thumbnail 放入 Windows 包，不需要启动 Server。

## 配置状态

`UCarConfigurationState` 负责：

- `paint`、`wheel`、`interior`、`frame` 四分区选择。
- 固定顺序 canonical key。
- 基础价与选项价差汇总。
- 模板原子应用。
- 非法分区、跨分区选项和未知模板拒绝。
- Blueprint 可订阅的 `OnChanged` 事件。

SC01 v2 目录由可 Cook 的 `DA_SC01Catalog` 从共享 JSON 生成；UI 不复制价格、配置键或 surface binding 规则。

`SourceAssets/` 仅用于有授权和来源记录的源侧参考文件，不是 Unreal `Content/`。提交前必须运行
`node tools/validate-source-assets.mjs`，确保其中没有 `.uasset`、`.umap` 或 `.tps`；完整规则见
`docs/REFERENCE_ASSET_POLICY.md`。

## 验证

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  ConfigurationSystemEditor Win64 Development `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

配置状态探针参数：

```text
-ConfigurationStateProbe
-ConfigurationStateProbeOutput=<结果 JSON>
```

探针验证 8 个选项、2 个模板、16 个唯一配置键、价格、事件次数和非法输入不改变状态。

## Editor-only 管理员导入

在 Unreal Editor 中打开 `Tools > Configuration System 管理员导入`。模型和动画可以单独或同时接入，每一类都必须选择一个 `.fbx` 和对应的 sidecar `.json`。骨骼车辆还可选择独立 `vehicle-surface-binding` 契约；该扩展不改变既有 sidecar 含义。

工作流固定为：

1. 选择 FBX 与 sidecar。
2. 点击“运行 C++ 预检”。
3. 确认关键字段、授权中的 `unreal-import`、文件存在性、字节数和 SHA-256 均通过。
4. 提供 surface-binding 时，确认其车型和模型版本与骨骼车辆 sidecar 一致。
5. 点击“批准并导入暂存区”；导入完成后自动审计 40 个命名槽以及每一级 LOD 的实际槽引用，失败会删除本次新增暂存资产。
6. 在暂存资产人工验收完成后，再通过后续发布流程移动到正式目录；首版工具本身不提供正式发布按钮。

每次预检生成新会话，导入目标只能是：

```text
/Game/Configurator/_ImportStaging/<session>
```

选择发生任何变化都会使已有预检失效。导入任务设置 `bReplaceExisting=false`，且代码会再次检查预检结果和暂存路径边界。JSON 报告写入：

```text
Saved/ConfigurationSystem/AdminImportReports/<session>.json
```

报告包含引擎版本、会话、暂存路径、每个文件的声明/实际字节数与 SHA-256、错误列表、导入状态和导入对象路径。

### 自动化测试

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Editor.AdminImport.Preflight;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

测试覆盖标准 SHA-256 向量、有效模型/动画 sidecar、固定暂存路径、报告写出、哈希不匹配拒绝，以及 surface-binding 的 slot/LOD 正反审计。

## Runtime 内容包

`FContentPackMountService` 接收任意工具生成的标准 `.pak` 与共享 manifest，不依赖 HotPatcher 插件 API。服务在挂载前严格校验 schema、pack/version、catalog/engine/platform、白名单 mount point、包内 mount point、文件大小、SHA-256，以及包内和已挂载内容包之间的 PrimaryAssetId 唯一性。任何预检失败都不会调用底层挂载。

Editor 的 `Tools > Configuration System 管理员导入` 同时提供材质包 manifest/.pak 选择、预检、挂载与结果区。策略固定为 `catalog=mvp-v1`、`engine=5.8`、`platform=Win64`；输入变化会使预检失效，只有预检通过才可挂载，且不会创建或修改正式资产。

契约、边界和调用示例见 `docs/CONTENT_PACK_RUNTIME.md`。自动化测试：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Runtime.ContentPack;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

### 命令行预检探针

不打开导入界面即可验证真实交付包：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound -AdminImportPreflightProbe `
  '-AdminModelFbx=D:\Intake\car.fbx' `
  '-AdminModelSidecar=D:\Intake\car.json' -log
```

动画参数为 `-AdminAnimationFbx` 与 `-AdminAnimationSidecar`，可和模型参数同时使用。探针通过返回 `0`，失败返回 `7`，并始终尝试写出 JSON 报告。

骨骼车辆可追加 `-AdminSurfaceBinding=<vehicle-surface-binding.json>`。该参数只做
预检；和 `-AdminImportApprovedProbe` 一起使用时，还会在导入后执行 slot/LOD
审计。

真实 pak 内容包预检参数：

```text
-ContentPackProbe
-ContentPackManifest=<manifest.json>
-ContentPackPak=<内容包.pak>
```

该探针使用固定的 `mvp-v1 / 5.8 / Win64` 策略和真实 `FPakFile` 索引读取，只预检、不挂载、不修改资产；通过返回 `0`，失败返回 `8`。

## Path Tracing 批量 Bake

产品 Path Tracing 使用 UE5.8 自带的 `OpenImageDenoise`（OIDN）空间降噪，
不启用 `NNEDenoiser` 或 `NNERuntimeORT`。PIE 直接跳过仅用于产品启动体验的
全屏就绪遮罩；独立运行和 Shipping 仍等待场景、车辆及三个 Web 视图全部就绪后淡入。

批量入口读取仓库 `contracts/fixtures/published-configurations.mvp.json`，按配置顺序和
`front`、`front-left`、`side`、`rear-right` 顺序生成 16 × 4 个任务。当前渲染对象是
明确标识为 Authorized Audi A5 proxy 的独立静态分件，不代表正式 SC01。Catalog 的
40 个 surface 各自绑定唯一可见运行时目标；缺少同名语义的项目使用未占用 A5 分件作
代理。每项 v2 任务必须经 `UAutomotiveMaterialBinder::ApplyTransaction` 原子应用，
不得直接绕过 Binder 修改状态；随后切换到带独立
`RenderView.<id>` 标签的相机，确认 Path Tracing 累积已重置并达到目标样本数后回读。
输出会自动检测 coverage 方向、清空全透明像素 RGB，并对无几何 coverage 的发光像素
构造 Alpha，最终写出规范 straight-alpha sRGB PNG。

Runtime/命令行入口：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  /Game/Maps/L_ConfigShowroom -game -windowed -ResX=1280 -ResY=720 `
  -dx12 -raytracing -ConfigurationBatchBake -log
```

Editor 入口：先启动 PIE（保证存在 Game Viewport），再在控制台执行：

```text
ConfigurationSystem.BakePublishedConfigurations
```

可选参数：

```text
-ConfigurationBakeInput=<published-configurations.json>
-ConfigurationBakeStaging=<输出 staging 目录>
-ConfigurationBakeSamples=<每项 Path Tracing 样本数，默认 16>
-ConfigurationBakeTaskTimeout=<单项超时秒数，默认 120>
```

默认输出为仓库 `staging/bake-manifest.json` 以及清单中 64 个
`renders/<publication>/<vehicle>/<configuration>/<view>.png`。即使单项失败，清单仍保留
完整 64 项并将对应项标记为 `failed`；命令行全量成功返回 `0`，否则返回 `10`。

不依赖 GPU 的计划与相机自动化：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Runtime.BatchBake;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

测试固定检查 16 个配置各自包含四视角、64 个任务/路径无重复，以及四个 RenderView
分别拥有唯一标签和唯一相机 Transform。正式车辆接入时只需替换车辆生成/配置映射，
不得改变任务键、RenderView ID、输出路径和 straight-alpha 清单契约。

## UE5.8 材质视觉基准

该探针只用于测试，不进入发布流程。固定地图
`/Game/Maps/L_MaterialVisualBaseline` 包含原点朝 `+Z` 的平面、垂直向下的白色
DirectionalLight、手动曝光相机和 `AMaterialVisualBaselineProbeActor`。创建命令可重复
执行，并会从空白地图重建相同场景：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NoSplash -NoSound `
  '-ExecCmds=ConfigurationSystem.MaterialVisualBaseline.CreateScene;Quit' -log
```

Editor 探针使用 Actor 内固定的 `SceneCaptureComponent2D` 与
`TextureRenderTarget2D` 离屏渲染，不依赖当前 viewport。打开 Output Log 控制台后执行：

```text
ConfigurationSystem.MaterialVisualBaseline.Run
```

可选参数：

```text
ConfigurationSystem.MaterialVisualBaseline.Run Output=D:/MaterialBaseline StableSeconds=0.5
```

Runtime 探针从同一地图和同一状态机运行：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  /Game/Maps/L_MaterialVisualBaseline -game -windowed -ResX=1280 -ResY=720 `
  -MaterialVisualBaselineProbe `
  '-MaterialVisualBaselineOutput=D:\MaterialBaseline' `
  -MaterialVisualBaselineStableSeconds=0.5 -log
```

探针按 `variantId` 排序遍历 `DA_SC01MaterialLibrary` 的 352 个 variant。每次同步加载并
赋给固定平面后，至少等待 `StableSeconds`、连续 3 个稳定 tick 且全局 shader
编译队列为空，再逐项触发固定 1280×720 SceneCapture、回读 RenderTarget 的 sRGB
像素并写 PNG。输出目录包含 `renders/*.png` 和
`manifest.json`；任一失败会保留 352 项清单并以 `failed` 标记尚未完成项。Runtime
成功退出码为 `0`，失败为 `12`。

不依赖 GPU 的计划与固定场景自动化：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Runtime.MaterialVisualBaseline+ConfigurationSystem.Editor.MaterialVisualBaseline;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```
