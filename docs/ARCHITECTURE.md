# 系统架构

## 范围与现状

ConfigurationSystem 以一套共享产品契约连接三个运行端：

- UE5.8 Windows 桌面端负责实时车辆选配、交互和 Path Tracing。
- Web 端负责手机竖屏与大屏选配，展示 UE 预先烘焙的透明图片。
- Server 负责目录、图片解析和静态资源分发，不参与实时渲染。

当前仓库已经具备 UE C++ 工程、共享契约和五项技术探针；Web 与 Server 仍是源码目录骨架。本文同时标注已经验证的边界和后续业务实现应遵守的边界，不把规划中的类型描述成已完成代码。

## 目录与 source-package 边界

```text
ConfigurationSystem/
├── source/                         # Git 跟踪的产品源码
│   ├── clients/
│   │   ├── ue/                     # UE5.8 Runtime 与 Editor 工程
│   │   └── web/                    # 响应式 Web 客户端
│   └── server/                     # 配置与图片服务
├── contracts/                      # 三端唯一共享契约
│   ├── fixtures/
│   ├── schemas/
│   └── openapi.yaml
├── tools/                          # 仓库级验证工具
├── docs/                           # 架构、决策、流程和探针证据
└── package/                        # 本机构建与发布产物，Git 忽略
    ├── clients/
    │   ├── ue/
    │   └── web/
    ├── server/
    └── renders/
```

`source/` 只保存可审查、可编译的源码和 UE Content 源资产。`package/` 只保存可运行或可部署的产物，不作为任何源码、契约或人工编辑数据的来源。`.gitignore` 忽略整个 `/package/`，也忽略 UE 的 `Binaries/`、`Intermediate/`、`Saved/` 和 Web/Server 构建目录。

共享契约当前位于仓库根目录 `contracts/`，工具当前位于 `tools/`。文档和代码必须引用这两个实际路径，不使用早期计划中的 `source/contracts/` 或 `source/tools/`。若未来迁移，必须在一次原子变更中同步脚本、文档和流水线。

## 三端职责

| 端 | 输入 | 核心职责 | 输出 | 禁止承担 |
|---|---|---|---|---|
| UE Runtime | catalog 映射、Primary Asset、用户输入 | 产品配置状态、价格、镜头、动作、环境、实时/Path Tracing 切换 | 当前产品配置、交互画面、可观测渲染状态 | Editor 资产保存、批量出图编排、按中文名生成 ID |
| UE Editor | 共享契约、UE 资产、发布集合 | 内容校验、项目地图生成、批量出图、Alpha 规范化、manifest 生成 | 完整且校验通过的版本化 render 集 | 进入 Development/Shipping Runtime 包 |
| Web | catalog、resolve 结果、透明 PNG | 响应式选择流程、价格展示、视角切换、图片预加载与错误态 | 版本化 URL 和用户界面状态 | 实时 3D、资产扫描、价格重算规则分叉 |
| Server | catalog、bake manifest、render 文件 | 契约校验、配置解析、静态资源服务、版本冲突处理 | catalog、图片 URL、固定错误模型 | 数据库规则引擎、动态渲染、猜测缺失配置 |

三个运行端以 `contracts/` 为唯一跨端事实来源。UE Content 路径、中文名称、UI 顺序和数组下标都不能成为跨端协议。

## UE Editor-Runtime 边界

`source/clients/ue/ConfigurationSystem.uproject` 声明两个模块：

- `ConfigurationSystem`，类型为 `Runtime`。
- `ConfigurationSystemEditor`，类型为 `Editor`。

Runtime 模块只依赖运行时可用的公开模块。目前包括 `Core`、`CoreUObject`、`Engine`、`AssetRegistry`、`InputCore`、`EnhancedInput`、`RHI`、`RenderCore`、`ImageCore` 和 `Json`。Editor 模块可以依赖 `UnrealEd`，并依赖 Runtime 模块复用数据类型和校验逻辑。

以下能力必须留在 Runtime：

- 产品选择、模板应用、价格和 canonical key 的领域逻辑。
- Primary Asset 枚举和异步加载。
- 镜头、车辆动作和可逆执行器。
- Runtime Path Tracing 能力检测、模式切换和精确进度快照。
- Development/Shipping 中需要执行的诊断探针。

以下能力必须留在 Editor：

- 创建或修改 `.uasset`、地图和测试资产。
- 注册 `PrimaryAssetProbe.CreateTestAssets`、`PackagingProbe.CreateProjectMap` 等编辑器命令。
- 内容审计入口、批量 Bake、manifest 写入和发布前图片处理。
- `UnrealEd`、`ToolMenus` 或其他 Editor-only API。

P0-5 已在 Development 与 Shipping 包中验证 `ConfigurationSystemEditor` 模块既不存在也未加载。新增依赖后必须重新执行打包边界探针，不能仅凭 `.uproject` 的模块类型判断没有泄漏。

## 数据模型边界

产品配置只包含能够改变可销售组合的选择：

```text
vehicleId
paint optionId
wheel optionId
interior optionId
frame optionId
catalogVersion
```

下列状态不进入产品配置键：

- UE 环境、交互镜头和当前渲染模式。
- 车门、机盖、后备箱、灯光和轮胎状态。
- Path Tracing 当前样本数。
- Web 当前背景、加载状态和面板状态。
- `renderViewId`；它选择同一产品配置的图片视角。

`interactionCameraId` 与 `renderViewId` 是两个不同概念。前者控制 UE 交互镜头，后者选择 Web 烘焙图。两者必须使用不同的 Schema 字段和 C++ 类型。

## canonical key

MVP 的配置键由固定分区顺序生成：

```text
paint__wheel__interior__frame
paint-red__wheel-sport__interior-dark__frame-black
```

生成流程必须先按契约中的固定分区顺序取值，再连接完整 `optionId`。禁止依赖 JSON 对象遍历顺序、UI 排列、本地化名称或 UE 资产路径。模板只是四个分区的预设，不改变键格式，也不产生额外组合。

规范图片路径为：

```text
renders/<publicationVersion>/<vehicleId>/<configurationKey>/<renderViewId>.png
```

`node tools/validate-contracts.mjs` 会验证 16 个唯一配置、canonical key、4 个视角和 64 个图片期望。

## 配置与图片数据流

```text
contracts/fixtures + contracts/schemas
        │
        ├── UE Runtime：映射为产品状态和 Primary Asset 引用
        │       └── 交互画面、价格、动作和 Path Tracing
        │
        ├── UE Editor：读取发布集合并遍历配置与视角
        │       └── Render → Alpha 规范化 → 校验 → manifest
        │
        └── Server：加载 catalog + manifest + renders
                └── Web：catalog → 用户选择 → resolve → PNG
```

Web 请求链路如下：

1. Web 读取 `GET /api/v1/catalog`。
2. 用户选择被归一化为完整产品配置。
3. 客户端或共享实现按 canonical 规则生成配置键；Server 必须独立校验输入，不能信任客户端字符串。
4. Web 调用 `POST /api/v1/renders/resolve`，同时提供 `renderViewId` 和版本。
5. Server 在不可变 manifest 中查找结果，返回版本化图片 URL。
6. Web 预加载图片；成功后替换画面，失败时保留上一张并显示错误。

非法配置返回 `400`，manifest 缺图返回 `404`，版本不兼容返回 `409`。Server 不应回退到“最接近”的配置或错误占位图。

## UE 资产发现流

材质选项采用 `UPrimaryDataAsset` 和 Asset Manager：

```text
DefaultGame.ini 扫描规则
→ Asset Manager 启动扫描
→ 按 PrimaryAssetId 枚举
→ 异步加载 Data Asset 与 Asset Bundle 软引用
→ 按分类和标签供分区筛选
```

扫描配置属于 `DefaultGame.ini`，不能放在 `DefaultEngine.ini`。Runtime 不调用 `ScanPathsForPrimaryAssets` 补扫来掩盖启动配置错误。正式材质、预览图等软引用必须通过明确的 Asset Bundle 与 Cook Rule 进入包。

## Path Tracing 数据流

Runtime 模式切换封装公开适配路径：

```cpp
GameViewport->ViewModeIndex = VMI_PathTracing;
ApplyViewMode(VMI_PathTracing, true, GameViewport->EngineShowFlags);
```

标准 `SetViewMode` 在 Cooked Development/Shipping 中会回退到 Lit，因此不能作为产品路径。Scene View Extension 在 `SetupView` 阶段从公开 `FSceneViewStateInterface` 读取当前样本和目标样本，再把线程安全快照提供给游戏线程 UI：

```text
progress = clamp(currentSample / max(targetSample, 1), 0, 1)
```

相机、材质、灯光或车辆变化后样本数下降，UI 必须立即归零并重新累计。UMG Tick 不直接访问 Renderer 内部对象，Runtime 模块也不得包含 Renderer Private 头文件。

## Editor 出图与发布流

原始 Path Tracing Alpha 在已验证管线中是反向 coverage，且自发光可能出现透明像素仍有 RGB 能量。Editor 发布链必须执行：

```text
Render
→ Alpha 语义识别
→ Coverage 反转与规范化
→ 自发光像素恢复
→ 透明 RGB 清理
→ Alpha 裁切
→ 多背景验证
→ manifest 与哈希校验
→ 原子发布新 publicationVersion
```

Web 只消费 `alphaMode=straight` 的规范化 PNG，不消费 Renderer 原始文件。发布过程先写临时目录；只有图片数量、尺寸、Alpha、哈希和 manifest 全部通过后，才切换到新的不可变版本目录。失败时继续提供旧版本。

## 车辆层级与动作流

运行时通过唯一 ComponentTag 找到控制 Pivot。`Vehicle.Root` 为根，`Vehicle.Body` 直接挂在 Root 下，车门、机盖、后备箱和四轮 Pivot 直接挂在 Body 下；可视网格挂在 Pivot 下且不带控制标签。

`UReversiblePartActuatorComponent` 以 `[0,1]` 的单一进度表达动作。`SetOpen()` 只改变目标值，动作中途反向时不重建起点，因此变换连续且重复操作不累积误差。真实车辆接入仍需按 `docs/VEHICLE_ASSET_REQUIREMENTS.md` 完成 Pivot、穿模和 Path Tracing 重置验证。

## 版本与失败语义

- `schemaVersion` 管 JSON 结构；破坏性字段变化升级主版本。
- `catalogVersion` 管目录内容；选项、价格或模板变化时升级。
- `publicationVersion` 管完整、不可变的图片发布集。
- 未知主版本必须拒绝，禁止猜测字段或 ID 映射。
- 已发布 ID 和版本目录不可原地复用。
- 探针、构建或发布失败必须保留原始退出码、日志和机器可读结果，不得用旧证据宣称本次通过。

关键方案及证据入口见 [DECISIONS.md](DECISIONS.md)，协作步骤见 [AI_WORKFLOW.md](AI_WORKFLOW.md)，提交与验证要求见 [CONTRIBUTING.md](../CONTRIBUTING.md)。
