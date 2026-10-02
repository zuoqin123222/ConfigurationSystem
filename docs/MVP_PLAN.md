# 汽车选配系统 MVP 计划

版本：0.2（多重验证修订版）  
目标引擎：Unreal Engine 5.8  
目标平台：Windows 桌面端、手机竖屏 Web、大屏 Web

Color preset: Product Teal — cue: 汽车产品与技术方案

Intro mode: contained — cue: 正式技术计划

## 结论

MVP 采用“一台演示车、一套共享配置、三端完整闭环”的范围。UE 负责实时选配和离线出图，Server 只负责配置与图片分发，Web 只负责基于烘焙图完成响应式选配。

所有核心需求保留，但缩小内容量：只做 1 台车、4 个选配分区、每区固定 2 个选项、2 套配色模板、4 个固定 Web 视角、2 套 UE 环境。4 个分区共产生 16 个合法配置和 64 张 Web 图片，全部烘焙，避免 Web 出现合法但缺图的状态。

目标车型现已确认为 `SC01`，消费者界面和对外传播以 `SC01` 为主名称；“江铃 羿驰”作为生产资质厂家和子品牌信息保留。现有 `demo-car`、`mvp-v1` 和 64 张占位图只承担技术 fixture 与流水线验收职责。正式车辆模型预计在团队复工后重新取得，届时通过新 catalog 和不可变 publication 迁移到 `vehicleId=sc01`，不得覆盖既有发布。完整背景与资产边界见 `docs/PRODUCT_CONTEXT_SC01.md`。

## MVP 边界

### 包含

| 范围 | 最小内容 |
|---|---|
| 演示车型 | 1 台，具备车身、轮毂、内饰、内部车架、灯、门、机盖、后备箱、轮胎可控部件 |
| 材质分区 | 车漆、轮毂、内饰、内部车架 4 区 |
| 材质选项 | 每区固定 2 个，共 8 个 |
| 配色模板 | 运动、豪华 2 套，套用后仍可逐区修改 |
| 镜头 | 默认、车漆、轮毂、内饰、内部车架 5 个交互机位 |
| 环境 | 影棚、暗色展厅 2 套 |
| 独立控制 | 车灯、车门、机盖、后备箱盖、轮胎转动 |
| 渲染模式 | 实时模式与 Path Tracing 模式切换，显示采样进度 |
| 自动出图 | 16 个配置 × 4 个视角，共 64 张透明 PNG 和 manifest |
| Web | 手机竖屏与 1920×1080 大屏，完成模板、分区、材质、视角选择 |
| Server | 配置目录、图片解析、静态图片、健康检查 |

### 延后

- 多车型、年款、地区车型包和增量 DLC。
- 库存、套餐、互斥依赖、法规和报价规则引擎。
- Web 实时 3D、Pixel Streaming、WebGPU 和云端实时渲染。
- 用户、收藏、分享、订单、支付、CRM/DMS 和管理后台。
- 分布式出图、断点续跑、自动视觉差异检测和 CDN 发布。
- 自由编排的车辆动画状态机；MVP 只提供固定动作接口。

## 目录目标

当前基础 C++ 骨架已迁移到 `source/clients/ue/`，并已建立 `source/clients/web/`、`source/server/` 及被 Git 忽略的 `package/` 目录。目标结构如下：

```text
ConfigurationSystem/
├── source/                         # Git 管理
│   ├── server/                     # Server 源码
│   ├── clients/
│   │   ├── ue/                     # UE5.8 C++ 工程
│   │   └── web/                    # 响应式 Web 客户端
│   ├── contracts/                  # 三端共享 ID、Schema、OpenAPI、示例数据
│   └── tools/                      # 校验与发布脚本
├── package/                        # 不进入 Git
│   ├── server/                     # 可部署 Server
│   ├── clients/
│   │   ├── ue/                     # Windows 打包程序
│   │   └── web/                    # Web 静态构建
│   └── renders/                    # Web 使用的烘焙图片
├── docs/                           # 计划、架构、规范、AI 协作记录
├── README.md
└── .gitignore
```

`.gitignore` 已忽略整个 `package/`，Web 生成目录规则已指向 `/source/clients/web/`。源码目录不得包含打包产物。出图先写入 UE 的临时输出，校验完整后再通过发布脚本原子发布到新版本目录；失败时保留旧版本，不允许发布部分结果。

## 共享数据契约

三端使用同一套稳定 ID，不允许按中文名或资产路径互相推断。

```text
vehicleId: demo-car  # 当前技术 fixture；正式 SC01 catalog 计划迁移为 sc01
partId: paint | wheel | interior | frame
optionId: paint-red | wheel-sport | interior-dark | frame-black
templateId: sport | luxury
interactionCameraId: default | paint | wheel | interior | frame
renderViewId: front | front-left | side | rear-right
configurationKey:
  paint-red__wheel-sport__interior-dark__frame-black
```

`source/contracts/` 至少包含：

- `catalog.schema.json`：车型、分区、材质、模板、视角和价格结构。
- `configuration.schema.json`：完整产品配置、版本和跨端序列化格式。
- `bake-manifest.schema.json`：出图路径、尺寸、透明通道、配置键和版本。
- `published-configurations.mvp.json`：Web 可达并必须完成出图的 16 个配置。
- `openapi.yaml`：Server 接口。
- `catalog.mvp.json`：MVP 唯一演示数据。
- `README.md`：ID 命名、版本升级和兼容规则。

产品配置、渲染状态和 UE 交互状态必须分开：

- 产品配置只包含车型与 4 个材质分区，派生 `configurationKey`。
- Web 渲染状态只包含 `renderViewId`，环境固定为 `web-studio`，动作固定为全部关闭、车灯关闭、轮胎静止。
- UE 的环境、车门、灯光、轮胎和当前镜头属于交互状态，不进入 Web 配置键。
- `interactionCameraId` 和 `renderViewId` 使用不同 Schema 和 C++ 类型，不允许互换。
- 配置可序列化为版本化 JSON 或 URL，用于复现和跨端对照。

价格规则固定为：`总价 = basePriceMinor + 四个当前选项的 priceDeltaMinor`。MVP 固定一种币种，模板没有独立价格，UE 与 Web 使用同一组 fixture 验证总价。

## UE 最小设计

### C++ 模块

保留一个 Runtime 模块，新增一个 Editor 模块，避免把出图和资产校验代码打进桌面程序。

| 模块 | 关键类型 | 职责 |
|---|---|---|
| Runtime | `UCarConfiguratorSubsystem` | 保存当前配置、模板应用、逐区修改、事件广播 |
| Runtime | `UCarMaterialOptionDataAsset` | Primary Asset；材质、预览图、中文名、价格、分类标签 |
| Runtime | `UCarPartDefinitionDataAsset` | 分区、材质槽、允许分类、机位、联动动作 |
| Runtime | `UCarColorTemplateDataAsset` | 模板名称和各分区默认材质映射 |
| Runtime | `UCarMaterialLibraryService` | 从配置目录扫描并建立材质索引 |
| Runtime | `UCarVariantComponent` | 将材质、部件可见性和参数应用到车辆 |
| Runtime | `ACarCameraDirector` | 机位插值、推进、内外饰黑场切换 |
| Runtime | `UCarActionComponent` | 灯、门、机盖、后备箱、轮胎动作 |
| Runtime | `UCarRenderModeService` | 实时/Path Tracing 切换和进度状态 |
| Editor | `UCarBakeSubsystem` | 遍历配置和视角，透明出图并写 manifest |
| Editor | `UCarContentValidator` | 校验重复 ID、缺图、缺材质、无效分区绑定 |

`Build.cs` 预计增加 `UMG`、`Slate`、`SlateCore`、`AssetRegistry`、`Json`、`JsonUtilities`、`DeveloperSettings`；Editor 模块再增加 `UnrealEd`、`ToolMenus` 和出图所需编辑器依赖。依赖名称以 UE5.8 本机编译结果为准。

### AI Native 边界

- C++ 负责配置状态、筛选规则、价格、镜头状态机、动作状态机、渲染模式、校验和出图编排。
- Data Asset 只保存可编辑内容和资产引用，不保存跨资产业务分支。
- UMG Blueprint 只负责布局、绑定和简单表现动画，不直接拼配置键、不计算价格、不扫描资产。
- Level Blueprint 不承载业务逻辑；展厅只放置 Actor 和环境资产。
- 必须使用编辑器 GUI 的工作限定为导入资产、设置材质槽/骨骼、放置镜头和调整视觉参数。
- C++ 对外接口和资产字段都写用途、坐标系、生命周期和降级方式注释，避免只重复代码含义。

### 材质库

材质库固定放在：

```text
/Game/Configurator/Materials/Library/
├── Paint/
├── Wheel/
├── Interior/
└── Frame/
```

每个可选材质使用一个继承 `UPrimaryDataAsset` 的 `UCarMaterialOptionDataAsset` 描述，包含：

- 稳定 `optionId`
- 中文名
- 价格，单位分，使用整数
- 预览图
- 实际材质
- 分类和筛选标签

`UCarConfiguratorSettings` 保存 Primary Asset 扫描目录。目录只负责资产组织，运行时通过 Asset Manager 枚举已注册的 Primary Asset；材质和预览图使用软引用，并配置递归 Cook 规则。分区不直接写死选项列表，而是用允许分类和标签筛选材质库。

材质能力必须同时在 Editor、Windows Development 和 Windows Shipping 中验证，目标是：

```text
Editor发现数 == Cook资产数 == Development发现数 == Shipping发现数
```

### 分区与模板

每个分区定义以下最小信息：

- `partId`、中文名和显示顺序。
- 目标 Mesh Component 标签及材质槽名。
- 允许的材质分类/标签。
- 对应镜头预设。
- 是否属于内饰。
- 可选的联动车辆动作。

模板只保存 `partId -> optionId`。套用模板时一次替换所有已定义分区，随后用户对单区的选择覆盖模板结果。

### 启动与镜头

启动流程固定为：

1. 黑场并锁定输入。
2. 车辆与材质库准备完成后淡入。
3. 默认镜头执行小角度旋转和缓慢推进。
4. 镜头到位后选配面板淡入，恢复输入。

选择分区时，镜头以缓动曲线过渡到对应机位。外饰到内饰或内饰到外饰时使用“淡出至黑色、瞬移机位、淡入”的流程；同类机位只做平滑插值。连续点击时取消旧过渡并从当前状态开始新过渡，避免跳帧。

### 车辆联动

MVP 使用统一动作接口，不为每个车型重写 UI：

| 触发 | 最小表现 |
|---|---|
| 内部车架颜色 | 机盖打开，再应用材质 |
| 轮毂变化 | 车身轻微下压和回弹 |
| 车灯 | 灯罩材质自发光与灯光组件同步开关 |
| 驾驶室屏幕/灯光 | 内饰选配时启用屏幕和环境灯参数 |
| 车门/机盖/后备箱 | 按钮切换开合状态 |
| 轮胎转动 | 按钮切换持续旋转 |

动画优先使用车辆已有骨骼/动画资产；没有动画资产时，允许 C++ 时间轴驱动部件相对变换作为演示降级方案。所有动作都要支持重复切换和中途反向。

### 环境与 Path Tracing

环境提供两个预设，每个预设绑定 HDRI、主光、曝光和后处理参数。切换时在 0.3 至 0.6 秒内插值亮度，再替换环境，避免瞬间闪烁。

Path Tracing 提供：

- 实时模式/Path Tracing 模式按钮。
- 基于公开 ViewState API 的精确 UMG 进度条，以及“准备、累积、完成”状态。
- 相机、材质、灯光或车辆状态变化后重置进度。
- 当前硬件不支持时禁用按钮并显示原因。

P0-2 已确认 UE5.8.1 的 `FSceneViewStateInterface` 可公开读取当前样本和目标样本，Development 与 Shipping 都能实现精确进度。产品使用 Scene View Extension 采集快照，不依赖 Renderer Private 头文件。MRQ 的任务进度只用于 Editor 批量出图，不能冒充交互视口采样进度。

Game/Shipping 的标准 `SetViewMode` 会把 Path Tracing 回退到 Lit，因此 `UCarRenderModeService` 使用公开的 `ViewModeIndex + ApplyViewMode` 适配路径，并在返回实时模式时恢复 `VMI_Lit`。该行为必须在每次 UE 升级后重新验证。

实现时参考 Epic Automotive Configurator Sample 的交互方式，但必须按 UE5.8 当前渲染接口重新验证，不能直接复制 UE4.27 的蓝图或控制台变量。Path Tracing 需要单独验证硬件光追、Compute Skin Cache、材质 Shader Permutation、Development/Shipping 支持和无支持硬件时的降级。

### 自动出图

Editor 工具提供“一键校验”和“一键出图”两个入口。出图能力只存在于 Unreal Editor，不进入 Windows Shipping 客户端：

1. 读取 `catalog.mvp.json` 和 UE 资产绑定。
2. 校验所有配置、材质、预览图和机位。
3. 遍历 `published-configurations.mvp.json` 中全部 16 个配置和 4 个视角。
4. 等待材质、动画和 Path Tracing 达到稳定条件。
5. 输出带 Alpha 的 PNG。
6. 写入 `manifest.json`，记录失败项，不用错误图片静默替代。

MVP 输出全部 16 个合法配置，模板只是其中两个预设，不形成额外组合。命名示例：

```text
renders/v1/demo-car/<configurationKey>/<viewId>.png
```

透明底流程需要启用 UE Alpha 输出设置，并通过白、黑、灰、红和棋盘背景验证边缘。manifest 记录宽高、色彩空间、文件哈希和 `alphaMode`。自动校验确认文件为 RGBA、Alpha 非全 0/全 255、车辆未裁切且透明区没有不可接受的 RGB 污染。若 Path Tracing 直接输出 PNG 不稳定，允许先输出 EXR，再转换为规范化 PNG。

## Web 最小设计

建议使用 React、TypeScript 和 Vite。页面只有一个选配工作区：

- 主图区域显示 Server 返回的透明 PNG，可叠加本地背景色或环境图。
- 分区栏、模板栏、材质卡片和动作无关；Web 只展示烘焙结果。
- 材质卡片显示预览图、中文名和价格。
- 支持 4 个固定视角。
- 显示当前配置摘要和总价。
- 图片切换时预加载下一张并做短交叉淡化，失败时保留上一张并提示。
- 快速连续选择时只允许最后一次请求更新画面，旧请求不得覆盖新状态。
- 选项触控区域不小于 44×44 CSS 像素，选中态不能只依赖颜色。

手机竖屏使用“上方车辆、下方抽屉式选配”；大屏使用“左侧车辆、右侧固定面板”。核心断点只保留一个，避免 MVP 引入复杂响应式体系。

Web 只展示固定 `web-studio` 光照下的透明车辆图。UE 的两个环境用于桌面端演示，不进入 Web 组合维度；Web 背景只是合成底图，不承诺改变车辆反射和照明。

UE 使用版本化 SaveGame 保存产品选择、当前模板、环境和 UI 面板开关。车门/机盖等临时动作、当前镜头、轮胎旋转和 Path Tracing 累积不跨启动保存；catalog 版本不兼容时回退到默认模板并记录原因。Web 将产品配置和视角写入版本化 URL，刷新后可恢复。

## Server 最小设计

建议使用 Node.js、TypeScript 和 Fastify，不接数据库。Server 启动时读取 catalog 和 bake manifest，并提供：

```http
GET  /health
GET  /api/v1/catalog
POST /api/v1/renders/resolve
GET  /assets/renders/*
```

`resolve` 校验车型、分区、选项、视角和版本后返回图片 URL。非法配置返回 `400`，manifest 缺图返回 `404`，客户端版本冲突返回 `409`。开发环境允许 Server 托管 Web 构建，正式部署方式留到后续版本。

最低安全边界包括路径穿越防护、请求体大小、未知字段、非法 JSON、正确 MIME 和版本化缓存。发布脚本写入新版本目录，完成 manifest 与文件哈希校验后原子切换，失败时继续提供旧版本。

## 并行批次

### 批次 0：契约与仓库

可并行：

- A：已将 UE 工程迁移到 `source/clients/ue/`，并建立 `source/` 和 `package/` 结构。
- B：编写 catalog、manifest Schema、OpenAPI 和 ID 规则。
- C：整理演示车资产清单、部件标签、材质槽和动画可用性。
- D：建立 `docs/AI_WORKFLOW.md`、`docs/ARCHITECTURE.md`、`docs/DECISIONS.md`。

合并门槛：目录迁移后 UE 仍可生成项目文件；三端共同确认 ID 和配置键规则。

### 批次 0.5：技术探针

以下五项可以互相并行，但全部通过后才展开 UE 大规模业务实现：

- P0-1（已通过）：Primary Asset 在 UE5.8.1 Editor、Cook、Development、Shipping 中发现和加载一致，见 `docs/technical-probes/PRIMARY_ASSET_PROBE.md`。
- P0-2（已通过）：Runtime Path Tracing 在 UE5.8.1 Development/Shipping 中可用，公开 ViewState API 支持精确进度，见 `docs/technical-probes/PATH_TRACING_RUNTIME_PROBE.md`。
- P0-3（已通过）：Path Tracing 可输出 PNG/EXR；原始反向 Alpha 经 Coverage 转换、自发光恢复、透明 RGB 清理和裁切后可用于 Web，见 `docs/technical-probes/PATH_TRACING_ALPHA_PROBE.md`。
- P0-4（原型通过，资产待接入）：层级审计器、铰链 Pivot 车门和中途反向执行器已通过程序化测试；真实车辆尚未提供，见 `docs/technical-probes/VEHICLE_HIERARCHY_PROBE.md`。
- P0-5（已通过）：Development/Shipping 最小包进入 `/Game/Maps/L_ConfigProbe`，硬引用、Primary Asset 和软纹理有效，Editor 模块无运行时或文件级泄漏，见 `docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md`。

合并门槛：五项探针都有可重复步骤、结果记录和明确结论；失败项必须选择降级方案后才能继续。

### 批次 1：三端可运行骨架

可并行：

- UE：展厅地图、车辆 Actor、GameMode、输入、基础 UMG。
- Web：基于 fixture 完成手机/大屏布局和本地图占位。
- Server：完成 catalog、resolve、静态文件和错误模型。
- 内容：建立材质库目录、4 个分区定义、5 个交互镜头和 4 个渲染视角。

合并门槛：三端都能独立启动，UE 可显示车辆，Web 可切换假数据，Server 通过接口测试。

### 批次 2：UE 选配体验

可并行：

- A：基于已冻结 Primary Asset 方案实现材质发现、分区过滤、模板和价格计算。
- B：基于已冻结接口实现启动淡入、镜头插值、内外饰黑场切换。
- C：基于资产审计表实现灯、门、机盖、后备箱、轮胎和联动动作。
- D：环境预设与灯光/屏幕材质参数。
- E：UMG 材质卡、模板栏、动作栏和状态持久化。

合并门槛：不重启地图即可完成模板应用、逐区覆盖、镜头联动和所有独立动作。

### 批次 3：渲染与出图

可并行：

- A：按 P0-2 结论实现 Path Tracing 模式切换、进度和状态失效重置。
- B：按 P0-3 结论实现 Editor 校验器、MRQ/MRG 批量出图和 manifest。
- C：Server manifest 加载和缺图检测。
- D：Web 图片预加载、交叉淡化和错误态。

合并门槛：全部 16 个配置 × 4 个视角的透明图完整生成，并能从 Web 端访问。

### 批次 4：集成与打包

可并行：

- A：UE Windows Development/Shipping 打包与启动测试。
- B：Web 生产构建和 Server 发布包。
- C：自动遍历 manifest，校验图片存在、尺寸、Alpha 和 HTTP 状态。
- D：补齐 README、部署说明、资产规范和已知限制。

合并门槛：`package/` 内同时存在可运行 UE 客户端、Web 客户端和 Server，且 `package/` 不被 Git 跟踪。

## 无依赖任务池

以下任务不依赖正式车辆资产、Path Tracing 结论或三端业务代码，可以在批次 0 与 0.5 同时推进：

| 任务 | 交付结果 |
|---|---|
| 需求追踪矩阵 | 每条需求对应设计、模块、测试编号和状态 |
| 产品配置 Schema | 车型、4 个分区、版本和序列化规则 |
| 合法组合生成器 | 确定性生成 16 个合法配置 |
| 配置键测试向量 | 正常、乱序、缺字段、重复和未知 ID 用例 |
| 价格 fixture | 两套模板和逐区覆盖后的唯一总价 |
| OpenAPI 与错误模型 | catalog、resolve、health 和静态资源 |
| Server 负向测试 | 路径穿越、非法 JSON、超大请求和版本冲突 |
| Web 状态线框 | 加载、缺图、冲突、空数据和窄屏抽屉 |
| 假图片资源包 | 16 个配置 × 4 个视角的占位图片 |
| 图片质量规则 | RGBA、尺寸、Alpha、哈希、裁切和命名 |
| 车辆资产审计模板 | 部件、枢轴、骨骼、动作和降级字段 |
| 机位命名规范 | 交互机位与渲染视角彻底分离 |
| 日志字段规范 | 版本、配置键、视角、错误码和耗时 |
| 资产授权表 | 车型、HDRI、贴图、字体和动画来源 |
| 测试环境矩阵 | UE 验收机、浏览器、DPR、触控和网络条件 |
| 文档模板 | 架构、ADR、模块 README 和 AI 工作流 |

## 默认交互参数

这些值用于消除“缓慢、轻微、明显、立即”等主观判断，均保存在 Data Asset 或配置文件中：

| 项目 | MVP 默认值 |
|---|---:|
| 启动黑场淡入 | 0.8 秒 |
| 启动旋转推进 | 1.6 秒 |
| UI 淡入 | 0.35 秒 |
| 同类机位过渡 | 0.6 秒 |
| 内外饰淡出/淡入 | 0.25 / 0.25 秒 |
| 轮毂联动回弹 | 0.5 秒 |
| Web 图片交叉淡化 | 0.2 秒 |
| UE 点击视觉反馈 | 100 毫秒内 |
| Server 本机 resolve | p95 小于 200 毫秒 |

## 验收标准

| 范围 | 通过条件 |
|---|---|
| 仓库 | `source/` 含 Server、UE、Web；`package/` 含对应三项打包结果且被 Git 忽略 |
| 启动演出 | 首次进入按 0.8 秒淡入、1.6 秒旋转推进、0.35 秒 UI 淡入播放，期间无可见默认地图 |
| 材质库 | 新增合规 Primary Data Asset 后无需改 UI 代码即可发现；Editor、Cook、Development、Shipping 数量一致 |
| 模板 | 两套模板可一键应用，任一分区可继续覆盖修改 |
| 卡片 | 每个材质显示预览图、中文名和价格，缺字段时校验失败 |
| 镜头 | 分区选择触发对应机位；内外饰按 0.25/0.25 秒黑场切换；连续 20 次快速操作无旧回调覆盖 |
| 联动 | 指定分区能触发机盖、车身回弹、灯或屏幕变化 |
| 独立动作 | 灯、门、机盖、后备箱和轮胎按钮均可重复开关 |
| 环境 | 两个 UE 环境可切换，过渡时间与曝光曲线由配置固定；Web 环境保持 `web-studio` |
| Path Tracing | 可切换模式；显示探针确认的精确或状态型进度；状态变化后重新累积；不支持硬件有明确降级 |
| 出图 | 64 张透明 PNG 和 manifest 完整；自动检查 RGBA、尺寸、Alpha、哈希、裁切和命名 |
| Web | 390×844 与 1920×1080 无横向溢出；触控区至少 44×44；快速请求只呈现最后结果 |
| Server | catalog、resolve、静态资源、health 和负向安全测试通过，错误体结构固定 |
| 一致性 | UE、manifest、Server、Web 对同一选择生成完全一致的配置键 |
| 性能 | 先登记 CPU、GPU、驱动、画质、渲染比例和场景；固定条件下实时模式 1080p 平均不低于 30 FPS，点击 100 毫秒内有视觉反馈 |

## AI 流程基建

每个批次都同步维护文档和机器可验证规则，不在项目末尾集中补文档：

- `docs/AI_WORKFLOW.md`：AI 修改边界、允许的 GUI 操作、编译和验证命令。
- `docs/ARCHITECTURE.md`：模块职责、事件流、配置键和跨端边界。
- `docs/DECISIONS.md`：重要方案选择、原因、替代方案和日期。
- 各模块 `README.md`：本地启动、构建、测试和常见错误。
- C++ 注释说明资产约定、线程/生命周期限制和引擎接口原因，不重复代码字面含义。
- 每批至少增加一个自动校验：Schema、C++ Automation Test、Server 测试或 Web 测试。
- 版本化保留稳定路径，重大改造新建 V2 流程，验证后再淘汰旧流程。

## 风险

| 风险 | MVP 处理 |
|---|---|
| 正式车辆资产未就绪 | 先用具备明确部件层级和授权的占位车验证链路 |
| 车辆骨骼/部件命名不一致 | 用 Component Tag 和 Data Asset 绑定，不在代码中写死对象名 |
| Path Tracing 接口与 4.27 示例不同 | 只复用交互目标；探针决定精确或状态型进度，不依赖 Renderer Private |
| 透明出图边缘异常 | 像素级 Alpha 校验；必要时 EXR 转规范化 PNG |
| 材质在 Editor 可见但未 Cook | Primary Asset、软引用和 Development/Shipping 加载探针 |
| 组合数量快速膨胀 | MVP 固定 16 个合法配置并全部出图；扩区前重新评估分层合成或实时 3D |
| 连续点击造成镜头/动画跳变 | 所有插值从当前状态重算，动作支持取消和反向 |
| 发布一半导致新旧资源混用 | 新版本目录完整写入和校验后原子切换 |

## 完成定义

MVP 完成时，评审者可以在 UE Windows 包中经历启动演出，选择模板和材质，看到镜头、车辆动作、环境与 Path Tracing 联动；内容制作人员可以在 Unreal Editor 中运行透明出图；随后能在手机或大屏 Web 中通过 Server 访问全部 16 个配置的 64 张烘焙结果。三端由共享 Schema、稳定 ID、配置键和 manifest 对齐，不依赖人工改文件名。

多重验证过程、遗漏项和技术探针说明见 `docs/MVP_PLAN_REVIEW.md`。
