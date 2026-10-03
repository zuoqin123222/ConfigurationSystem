# 架构决策记录

本文件记录已经冻结并影响多个模块的方案。状态为“已接受”的决策是当前实现和后续开发的默认约束；修改时新增 ADR 或将原 ADR 标记为被取代，不直接改写历史理由。

## ADR-001 source 与 package 分离

- 状态：已接受
- 文档化日期：2026-10-02
- 相关证据：`.gitignore`、`source/README.md`、`docs/MVP_PLAN.md`

### 背景

UE、Web 和 Server 会产生体积大、平台相关且不可审查的构建结果。若产物混入源码目录，Git 状态、Cook 输入和发布来源都会变得不明确。

### 决策

所有受 Git 管理的产品源码放在 `source/`：

```text
source/clients/ue/
source/clients/web/
source/server/
```

本地构建和发布产物放在根目录 `package/`：

```text
package/clients/ue/
package/clients/web/
package/server/
package/renders/
```

`package/` 整体被 Git 忽略。共享契约和仓库工具当前分别位于根目录 `contracts/` 与 `tools/`，它们是受 Git 管理的输入，不属于发布产物。

### 取舍

采用各端自行保存 `dist/`、`build/` 或 UE Archive 的方案会让发布内容散落在源码树中，难以统一清理和审计。统一 `package/` 增加了构建脚本的输出参数，但清楚分开“可重建输入”和“生成结果”。

### 后果

- 源码不得读取 `package/` 作为唯一配置来源。
- 发布任务先写临时位置，校验完成后再进入版本目录。
- 文档和脚本必须引用实际的根级 `contracts/`、`tools/` 路径。
- `package/` 中任何内容都不能作为代码评审或提交的一部分。

## ADR-002 Primary Asset 管理可选内容

- 状态：已接受
- 文档化日期：2026-10-02
- 相关证据：`docs/technical-probes/PRIMARY_ASSET_PROBE.md`

### 背景

材质选项和预览图不一定被地图硬引用。仅依赖 Content Browser 目录或同步硬引用，会造成 Editor 可见但 Cook 后缺失，也会让 UI 和业务代码绑定资产路径。

### 决策

可选材质使用继承 `UPrimaryDataAsset` 的类型描述。Asset Manager 扫描规则写入 `source/clients/ue/Config/DefaultGame.ini`，材质和预览图使用软引用及 Asset Bundle，生产类型配置明确 Cook Rule。

运行时只接受 Asset Manager 启动扫描结果，通过 Primary Asset ID 枚举并异步加载。禁止调用 `ScanPathsForPrimaryAssets` 补扫来掩盖配置错误。正式 ID 形状为：

```text
CarMaterialOption:<optionId>
```

### 取舍

地图硬引用实现简单，但无法支持可扩展材质库。运行时目录扫描在 Editor 中容易产生假阳性，而且不证明 Cook 内容正确。Primary Asset 配置较多，但能同时表达发现、异步加载和 Cook 规则。

### 后果

- 新增正式资产后必须验证 Editor、Development 和 Shipping 枚举数一致。
- Asset Manager 设置属于 Game 配置，不放在 `DefaultEngine.ini`。
- 分区按分类和标签筛选，不硬编码资产列表。
- P0-1 必须在影响扫描、Bundle 或 Cook 的改动后重跑。

## ADR-003 使用公开 Path Tracing 适配与精确进度

- 状态：已接受
- 文档化日期：2026-10-02
- 相关证据：`docs/technical-probes/PATH_TRACING_RUNTIME_PROBE.md`

### 背景

UE5.8 的标准 `UGameViewportClient::SetViewMode` 在 Cooked Development 和 Shipping 中会把 Path Tracing 请求回退为 Lit。产品还需要显示真实的当前样本和目标样本，而不是用计时器模拟进度。

### 决策

Runtime 服务通过公开接口设置视图模式：

```cpp
GameViewport->ViewModeIndex = VMI_PathTracing;
ApplyViewMode(VMI_PathTracing, true, GameViewport->EngineShowFlags);
```

返回实时模式时应用 `VMI_Lit`。Scene View Extension 在 `SetupView` 阶段从公开 `FSceneViewStateInterface` 读取：

```cpp
View.State->GetPathTracingSampleIndex();
View.State->GetPathTracingSampleCount();
```

游戏线程 UI 消费快照，按 `current / max(target, 1)` 显示精确进度。Runtime 不包含 Renderer Private 头文件，不使用控制台命令作为产品接口。

### 取舍

标准 `SetViewMode` 在打包程序中不可用。控制台命令缺少稳定类型和错误语义。MRQ 任务进度不等于交互 Viewport 的采样进度。公开直接适配能满足当前 UE5.8.1，但绕过了 `SetViewMode` 内部守卫，需要版本升级回归。

### 后果

- 切换前检查硬件、RHI、Shader Platform 和项目设置。
- 相机、材质、灯光或车辆变化后，进度归零并重新累计。
- UMG Tick 不直接访问 ViewState。
- 每次 UE 小版本升级都重跑 P0-2 的 Editor Game、Development 和 Shipping 验证。

## ADR-004 Path Tracing Alpha 必须规范化

- 状态：已接受
- 文档化日期：2026-10-02
- 相关证据：`docs/technical-probes/PATH_TRACING_ALPHA_PROBE.md`、`contracts/schemas/bake-manifest.schema.json`

### 背景

UE5.8.1 探针的原始 Path Tracing Alpha 表示反向透明度：背景接近 1，不透明表面接近 0。加法自发光还可能产生 Alpha 为透明但 RGB 有能量的像素。原始 PNG 直接供 Web 使用会让背景不透明、车身消失或灯光丢失。

### 决策

Editor Bake Pipeline 在发布前执行：

```text
Alpha 语义识别
→ Coverage 反转
→ 自发光 synthetic Alpha 恢复
→ straight RGB 反解
→ 全透明 RGB 清理
→ 按规范化 coverage 裁切
→ 多背景合成验证
```

发布 PNG 固定为 straight Alpha。manifest 记录 `coverageInverted`、`normalizationRequired`、`glowRecoveredPixels`、尺寸、哈希和 Alpha 策略。

### 取舍

直接发布原始 PNG 不可用。简单清零透明 RGB 会删除 Bloom。独立 additive 图层有更强控制力，但增加 Web 合成和发布组合；MVP 先采用 synthetic Alpha 的单图方案。

### 后果

- Web 只消费规范化后的 straight-alpha PNG。
- Renderer、MRQ/MRG 或材质管线变化后重新验证 Alpha 语义。
- 正式车辆的玻璃、车灯和 Bloom 必须在黑、白、灰、红、棋盘背景复测。
- 未通过像素检查的图片不能进入 ready manifest。

## ADR-005 层级标签与可逆动作

- 状态：已接受，真实车辆资产验收待完成
- 文档化日期：2026-10-02
- 相关证据：`docs/technical-probes/VEHICLE_HIERARCHY_PROBE.md`、`docs/VEHICLE_ASSET_REQUIREMENTS.md`

### 背景

车型网格命名、骨骼结构和可用动画并不稳定。若业务代码硬编码对象名或 Blueprint 引线，每个车型都需要重写控制逻辑，动作中途反向也容易跳变。

### 决策

控制节点使用唯一 ComponentTag：

```text
Vehicle.Root
Vehicle.Body
Vehicle.Part.Door.FrontLeft
Vehicle.Part.Hood
Vehicle.Part.Trunk
Vehicle.Part.Wheel.FrontLeft
Vehicle.Part.Wheel.FrontRight
Vehicle.Part.Wheel.RearLeft
Vehicle.Part.Wheel.RearRight
```

标签放在 `Movable` Pivot，不放在可视 Mesh。部件 Pivot 直接挂在 Body 下，可视 Mesh 挂在对应 Pivot 下。

`UReversiblePartActuatorComponent` 使用一个 `[0,1]` 进度和目标值。`SetOpen()` 只改变目标，中途反向从当前进度连续运动；关闭终态回到原始 Transform。

### 取舍

按组件名查找无需额外资产标记，但导入重命名会破坏行为。为每个车型编写 Blueprint 灵活但难以统一验证。标签和 C++ 执行器要求资产接入规范更严格，却能复用审计、动作和 UI。

### 后果

- 重复标签、错误父子关系、Static 控制节点和错误 Pivot 必须被审计拒绝。
- 程序化 Cube 原型通过只证明代码契约，不代表真实车辆通过。
- 正式车辆仍需验证铰链轴、穿模、中途反向、三轮重复和 Path Tracing 采样重置。

## ADR-006 项目地图进入 Cook，Editor 能力隔离

- 状态：已接受
- 文档化日期：2026-10-02
- 相关证据：`docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md`

### 背景

只在 Editor 中打开成功不能证明 Windows 包会进入项目地图，也不能证明硬引用、Primary Asset 和软引用已 Cook。Editor 工具若泄漏到 Runtime，会扩大包体并引入不可用依赖。

### 决策

Editor、Game 和 Server 默认地图统一为：

```text
/Game/Maps/L_ConfigProbe
```

`DefaultGame.ini` 的 `MapsToCook` 显式包含该地图。地图创建和测试资产刷新命令只由 `ConfigurationSystemEditor` 注册。Runtime 模块不依赖 `UnrealEd`、`ToolMenus` 或 Editor 工具类型。

Development 和 Shipping 验证同时检查项目地图、C++ 硬引用、Primary Asset、软纹理，以及 Editor 模块文件和加载状态。

### 取舍

依赖默认地图的隐式 Cook 收集配置更少，但证据不明确。显式 `MapsToCook` 有重复配置风险，因此地图改名时必须同步检查。仅检查文件名也不足以证明运行时边界，所以同时使用 `FModuleManager` 运行时检查。

### 后果

- 新地图替换 `L_ConfigProbe` 前必须更新三种默认入口、`MapsToCook` 和探针预期。
- 创建资产和地图的命令不得移入 Runtime。
- UE 模块依赖或打包配置变化后重跑 P0-5。
- Development/Shipping 必须用实际运行和归档扫描证明边界。

## ADR-007 canonical key 使用固定分区顺序

- 状态：已接受
- 文档化日期：2026-10-02
- 相关证据：`contracts/README.md`、`tools/validate-contracts.mjs`

### 背景

UE、Web 和 Server 必须对同一产品选择生成完全一致的键。对象遍历、UI 排序、本地化名称和资产路径都可能随实现或环境变化。

### 决策

MVP 固定分区顺序：

```text
paint → wheel → interior → frame
```

配置键连接四个完整 `optionId`：

```text
paint-red__wheel-sport__interior-dark__frame-black
```

模板只是产品配置预设。`renderViewId`、`interactionCameraId`、环境和车辆动作不进入配置键。图片路径在键外再增加 `publicationVersion`、`vehicleId` 和 `renderViewId`。

### 取舍

排序键名看似通用，但会把字典序变成隐式协议。传递任意客户端生成的 key 最简单，却无法拒绝缺字段、重复或未知选项。固定领域顺序清晰且可测试，新增分区时需要显式升级契约和发布数据。

### 后果

- 三端各自校验完整配置，Server 不信任客户端提供的 key。
- 禁止从中文名、数组下标或 UE 路径推断 ID。
- 新增、删除或重排分区属于破坏性协议变更。
- `node tools/validate-contracts.mjs` 是提交前最低验证，当前应得到 16 个唯一组合、4 个视角和 64 个图片期望。

## 修改决策的方式

需要改变已接受方案时，新增下一编号 ADR，写明被取代的编号、迁移边界和验证证据。旧记录保留原状态并增加“已被 ADR-xxx 取代”，避免历史提交失去上下文。

## ADR-008 外部 UE 样例隔离与 Maya FBX 重建

- 状态：已接受
- 文档化日期：2026-10-03
- 相关证据：`docs/REFERENCE_ASSET_POLICY.md`、`tools/maya/normalize_fbx_maya2025.py`

### 背景

本机安装了 Epic Automotive Configurator 5.8 样例，可用于理解 Variant Manager、车辆部件拆分、Control Rig、环境和汽车材质表现。直接迁移样例 `.uasset` 会引入隐藏依赖、冗余内容、命名冲突和授权边界不清；Fab 页面还标记该内容不允许 AI 使用。

### 决策

样例工程保持在仓库外，只能作为隔离参考源。当前仓库禁止接收样例的 `.uasset`、`.umap`、`.tps`、Blueprint、材质、材质函数、纹理、环境地图、音频和项目配置。

授权允许时，车辆几何必须由资产负责人显式导出为 FBX，再通过 Maya 2025、不可变输入哈希和显式重命名清单规范化。DCC/FBX 使用 `Vehicle_Root`、`Vehicle_Body` 等下划线节点名；UE 导入后另行设置 `Vehicle.Root`、`Vehicle.Body` 等 ComponentTag。

汽车内饰 Shader 只参考公开表现目标和通用 PBR 原理，在当前工程用自己的节点网络、参数、纹理和命名从零创建。未取得覆盖 `Allows usage with AI: No` 的明确授权前，AI 自动化不读取样例资产正文或生成其衍生资产。

### 后果

- `tools/validate-source-assets.mjs` 递归拒绝 Unreal 和 TexturePacker 二进制进入 `SourceAssets`。
- Maya 工具只接受清单目录内、哈希匹配、用户明确提供的 FBX，不扫描外部样例工程。
- 正式接入仍必须经过车辆 sidecar、管理员暂存导入、UE GUI 和 Shipping 内容包验证。
- 平台功能继续使用自建或 CC0 代理资源开发，不以 Epic 样例为阻塞项。

## ADR-009 SC01 使用 v2 多层契约与不可报价草案

- 状态：已接受
- 文档化日期：2026-10-03
- 相关证据：`contracts/schemas/*.v2.schema.json`、`contracts/fixtures/sc01.*.v2.json`、`docs/product-data/sc01/FIELD_DECISIONS_V2.md`

### 背景

SC01 选配清单包含分类、部件、表面、材料、颜色、工艺、数量和计价单位，不能
无损映射到 v1 的固定四分区与单一价差。来源表虽然显示部分金额，但基础车型价、
税费、工时、有效期和审批状态尚未确认。直接改写 v1 会破坏现有三端自动化；
把表内数字当报价则会制造未经确认的商业输出。

### 决策

保留 v1 文件、ID、16 个组合和 64 张图片约定。SC01 另用
`schemaVersion=2.0.0`，目录关系为
`category → component → surface → option`，option 可引用 material family。
catalog 的 `selectionOrder` 是配置身份的唯一顺序来源。

配置规范输入使用 UTF-8 与 LF，按固定头字段和 `selectionOrder` 逐行编码。
`configurationId` 取 SHA-256 前 24 个十六进制字符并加 `cfg-`；
`renderKey` 从独立的 `canonicalRenderInput` 计算，该输入只保留当前
`renderRelevant=true` 的所选项。完整配置变化但渲染投影不变时复用图片；
黄金向量冻结算法，JSON 属性顺序不得影响结果。

SC01 处于 draft 时，所有基础价、单价、小计和总价必须为 `null`，
`quoteAllowed` 必须为 `false`，阻断原因必须包含 `PRICE_UNCONFIRMED`。清单金额
只有在业务确认口径和有效性后，才能通过新的 catalogVersion 进入报价。

### 取舍

并行主版本增加了消费者分派和迁移成本，但避免把 SC01 的层级压扁或破坏 v1。
截断哈希便于跨端缓存和路径使用，但不承担签名或防篡改职责。草案不计算金额会
限制当前演示范围，却能明确区分资料摘录与正式报价。

### 后果

- v1 和 v2 必须由聚合验证器同时校验，任何一侧失败都阻断变更。
- SC01 字段必须带页级来源；无法确认的颜色适用关系、约束和价格不得推断。
- UE、Web 与 Server 后续实现必须复用黄金向量验证身份算法。
- 正式报价前需新增业务确认记录、发布新的 catalogVersion，并更新价格策略测试。

## ADR-010 Web 与 UE 共用选配内容，UE 原生承载实时控制

- 状态：已接受
- 文档化日期：2026-10-04
- 相关证据：`docs/technical-probes/UNIFIED_WEB_UE_UI_STAGE.md`

### 背景

独立 Web 使用静态车辆图片，UE 使用实时车辆视口，但用户在两端需要相同的选配
步骤、材料列表、保存逻辑和视觉语言。若重新维护一套完整 UE 选配 UI，字段、价格
和交互会逐渐偏离 Web；若把所有 UE 控制放入内嵌浏览器，浏览器矩形会与实时视口
争夺鼠标输入，也无法可靠承担本机画质和渲染设置。

### 决策

选配内容继续由 React 实现。独立 Web 显示静态预览与可由现有媒体资源支持的控制；
UE 在右侧 `UWebBrowser` 中加载同一选配内容，并隐藏网页体验控制。

UE 原生 UMG 在实时视口底部提供镜头、动画、灯光、渲染、复位和全屏控制，在实时
视口右上角提供画质设置。所有控制发送目标状态，不发送 Toggle 命令。Web 选配变化
通过最小权限桥提交白名单 JSON，由 UE v2 状态原子应用并驱动材质绑定。

### 后果

- React 是选配内容和业务交互的唯一实现，UE 不复制目录与价格 UI。
- UE 原生控制层不得覆盖右侧 CEF 面板，也不得阻断实时视口的拖拽和滚轮输入。
- Web 没有对应组合媒体时隐藏灯光和车辆动画，不以不可用按钮模拟能力。
- Bridge 不暴露 World、PlayerController 或通用命令执行，只接受受限配置事务。
- 修改面板宽度时必须同步调整实时视口底栏中心与齿轮位置，并执行 Shipping GUI 验收。
