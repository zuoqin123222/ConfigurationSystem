# 汽车选配系统 MVP 多重验证报告

审查对象：`docs/MVP_PLAN.md` 0.1  
审查结论：需要修订后再作为实施基线  
审查维度：需求覆盖、三端闭环、UE 技术可行性、依赖关系、验收可测性、发布与安全

## 总体判断

原计划覆盖了大部分功能名称，模块边界也基本合理，但存在一个闭环阻断和四类高风险不确定性：

1. Web 允许逐区自由选配，但 Bake 只覆盖部分组合，合法操作可能返回缺图。
2. 材质在 Editor 中可扫描，不代表它会进入 Cook 后的 Windows 包。
3. UE5.8 交互视口的 Path Tracing 精确采样值不能预设为稳定公开 API。
4. 透明 PNG 的 Alpha、玻璃、后处理和 Web 合成约定没有形成可机器验证的规范。
5. Editor 出图能力与 Windows Shipping 客户端边界不够明确。

计划修订原则是先闭合数据集合，再用技术探针验证高风险引擎能力，最后展开三端并行开发。

## 验证方法

| 验证层 | 检查内容 | 结果 |
|---|---|---|
| 需求逐条映射 | 原始需求到设计、任务和验收 | 基本覆盖，内部车架、价格、降级和状态交接不足 |
| 数据闭环推演 | Web 可达配置、Bake 集合、manifest 和 Server resolve | 原方案不闭合 |
| UE 工程基线 | 当前模块、插件、地图和渲染设置 | 仍是 Runtime 空骨架 |
| UE 官方能力 | Asset Manager、Path Tracer、MRQ、Alpha、Packaging | 方向可行，但有公开 API 与 Cook 风险 |
| 依赖图检查 | 并行任务是否具备稳定输入 | 多项任务需先冻结 Schema/接口 |
| 验收可测性 | 是否有阈值、环境和自动判定 | 原方案存在多处主观描述 |

## 阻断问题

### 配置集合与出图库不一致

原计划允许模板后逐区修改，同时只输出模板及少量批准组合。这样 Web 会产生合法但未烘焙的状态，Server 只能返回 `404`。

修订决策：

- MVP 固定 4 个分区，每区恰好 2 个选项。
- 分区改为车漆、轮毂、内饰、内部车架。
- 共有 `2 × 2 × 2 × 2 = 16` 个合法配置。
- 每个配置输出 4 个固定视角，共 64 张透明图。
- Web 可达集合、Bake 集合和 Server manifest 全部由同一个 `published-configurations.mvp.json` 生成。
- 发布前自动遍历全部 16 个配置和 4 个视角，任一缺图都阻止发布。

### 内部车架入口缺失

原计划写了“内部车架颜色触发机盖打开”，但分区列表没有内部车架。

修订决策：

- 用 `frame` 替换非必要的 `trim`。
- `frame` 作为正式分区进入配置键、模板、价格、镜头和 Bake 集合。
- 选择 `frame` 时先打开机盖，动作稳定后应用材质。

### Editor 与 Shipping 边界

批量出图依赖 Editor 模块和 Movie Render Pipeline，不应进入 Windows Shipping 包。

修订决策：

- Windows 包只负责实时选配和可用时的交互 Path Tracing。
- Unreal Editor 中的 Editor 模块负责校验、MRQ/MRG 出图和 manifest。
- Server/Web 只消费已发布的图片和 manifest。
- Development 与 Shipping 均检查没有 `UnrealEd`、`ToolMenus` 或 Bake 入口泄漏到 Runtime。

## 高风险技术点

### 材质发现与 Cook

目录用于组织资产，运行时发现应基于 Asset Manager 注册的 Primary Asset。`UCarMaterialOptionDataAsset` 继承 `UPrimaryDataAsset`，实际材质和预览图使用软引用，并配置递归 Cook 规则。

必须验证：

- Editor 发现数量。
- Cook 后资产数量。
- Development 包发现数量。
- Shipping 包发现数量。
- Data Asset 的材质、纹理和预览图均能异步加载。

通过关系：

```text
Editor发现数 == Cook资产数 == Development发现数 == Shipping发现数
```

### Path Tracing 进度

P0-2 已确认 Path Tracer 会渐进累积，且相机变化会使采样归零后重新累积。UE5.8.1 的 `FSceneViewStateInterface` 公开提供当前样本和目标样本，Editor Game、Cooked Development 与 Cooked Shipping 均已实机读取成功。

MVP 采用精确进度方案：

- Scene View Extension 读取公开 ViewState 采样快照。
- UMG 使用当前样本/目标样本计算百分比。
- 不依赖 Renderer Private 头文件。
- Game/Shipping 通过公开 `ViewModeIndex + ApplyViewMode` 适配 Path Tracing，标准 `SetViewMode` 会被守卫回退。
- MRQ 批量出图进度仍使用 Movie Render Pipeline 的任务进度，不与交互视口采样数混用。

### 透明输出

P0-3 已确认“文件含 Alpha”不足以证明可供 Web 使用：UE5.8.1 Path Tracing Viewport 原始输出采用反向透明度，并且加法自发光可能在零 coverage 下保留 RGB。发布前必须执行 Coverage 反转、自发光恢复、透明 RGB 清理和 Alpha 裁切。

正式出图必须固定：

- 输出尺寸和色彩空间。
- `alphaMode` 是 straight 还是 premultiplied。
- 背景 Alpha 为 0、实体中心 Alpha 为 1。
- 车窗半透明的预期范围。
- 是否保留地面阴影。
- Bloom、DOF、降噪和 Alpha 累积设置。
- PNG 失败时是否采用 EXR 中间文件再转换。

自动校验至少检查 RGBA、尺寸、文件哈希、Alpha 非全 0/全 255、透明区 RGB 污染、反向 Alpha、自发光恢复和车辆裁切。P0-3 的规范化 PNG/EXR 已通过黑、白、灰、红和棋盘背景合成。

### 车辆资产结构

车门、机盖、后备箱和轮胎可能来自骨骼、独立组件或合并 Mesh。统一动作代码必须建立在资产审计结果上。

资产审计表至少包含：

```text
部件、Component/Bone、枢轴、闭合状态、开启状态、碰撞、
光追支持、既有动画、中途反向、出图稳定条件、降级方案
```

## 数据契约补充

应把以下三个概念分开：

### 产品配置

```json
{
  "schemaVersion": 1,
  "catalogVersion": "v1",
  "vehicleId": "demo-car",
  "selections": {
    "paint": "paint-red",
    "wheel": "wheel-sport",
    "interior": "interior-dark",
    "frame": "frame-black"
  }
}
```

### 渲染状态

```json
{
  "viewId": "front-left",
  "environmentId": "web-studio",
  "actionPreset": "all-closed-lights-off"
}
```

Web MVP 只使用固定 `web-studio` 光照和 `all-closed-lights-off` 动作状态。UE 的环境切换、车门和灯光属于交互状态，不进入 Web 配置键，避免组合量膨胀。

### 价格规则

- `basePriceMinor`：车辆基础价，整数最小货币单位。
- `priceDeltaMinor`：选项加价。
- `currency`：MVP 固定一种币种。
- 总价公式：基础价加当前四个分区选项加价。
- 模板没有独立价格，模板价格由其最终选择计算。
- UE 和 Web 使用共享 fixture 做相同的价格测试；Server 返回权威 catalog 数据。

## 两类机位

必须拆分：

- `interactionCameraId`：默认、车漆、轮毂、内饰、内部车架，用于 UE 交互。
- `renderViewId`：前、左前、侧、右后，用于自动出图和 Web。

两个 ID 类型不可互换，Schema 和 C++ 类型也应分开。

## 量化验收补充

以下数值作为 MVP 默认配置，均应放入 Data Asset 或配置文件而不是散落在代码中：

| 项目 | 默认值 |
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

性能验收前必须登记验收机 CPU、GPU、驱动、分辨率、渲染比例、画质档和固定测试场景。未登记硬件前，“1080p 30 FPS”只能作为目标，不能作为可复现结论。

## Server 最低边界

MVP 虽不包含账号和数据库，仍需验证：

- 路径穿越和编码路径不能逃逸静态资源根目录。
- 非法 JSON、超大请求体和未知字段被拒绝。
- 未知车型、分区、选项、视角和版本返回稳定错误体。
- 静态图片返回正确 MIME。
- 版本目录使用不可变缓存，catalog/manifest 使用可重新验证缓存。
- 发布采用新版本目录写入、完整性校验、原子切换；失败时保留旧版本。

## Web 最低边界

- 390×844 手机竖屏和 1920×1080 大屏。
- 选项触控区域不小于 44×44 CSS 像素。
- 选中态同时使用文字或图形，不只靠颜色。
- 缺图时保留上一张，展示明确错误，不自动替换成其他配置。
- 快速连续点击只展示最后一次请求结果，旧请求不得覆盖新状态。
- 配置可序列化为版本化 URL 或 JSON，便于复现和跨端对照。

## 其他遗漏修正

- AI Native 边界：业务状态、筛选、价格、镜头、动作、校验和 Bake 编排归 C++；UMG/Level Blueprint 不承载核心业务分支。
- 状态持久化：UE 只保存产品选择、模板、环境和面板开关；动作、镜头和 Path Tracing 累积不跨启动保存。
- 旧版本恢复：SaveGame 或 Web URL 的 catalog 版本不兼容时回退默认模板并记录原因。
- 请求竞态：Web 快速切换时取消或忽略旧请求，只允许最后一次选择更新画面。
- 发布一致性：manifest、图片和 catalog 作为同一版本原子发布，不能混用新旧文件。

## 批次 0.5 技术探针

以下探针必须在大规模业务开发前完成：

| 编号 | 探针 | 阻断范围 | 通过产物 |
|---|---|---|---|
| P0-1 | Primary Asset 扫描、Cook、打包加载 | 材质库和模板 | 一个未被地图硬引用的材质选项在 Development/Shipping 均能加载 |
| P0-2 | Runtime Path Tracing 与进度能力 | 渲染模式和 UMG | 明确 Shipping 支持及精确/状态型进度方案 |
| P0-3 | Path Tracing 透明 PNG/EXR | Bake 和 Web | 车漆、玻璃、灯具在多背景合成无异常 |
| P0-4 | 车辆层级与可动性 | 所有动作 | 部件绑定表和一个可逆动作原型 |
| P0-5 | Development/Shipping 最小包 | 发布边界 | 进入项目地图、无 Editor 泄漏、无缺失资产 |

## 无依赖任务池

以下工作可以立即并行，不依赖车辆正式资产、Path Tracing 结论或三端业务代码：

| 任务 | 交付结果 |
|---|---|
| 需求追踪矩阵 | 每条需求对应设计、模块、测试编号和状态 |
| 配置 Schema | 产品配置、渲染状态、版本和序列化规则 |
| 合法组合生成器 | 确定性生成 16 个 MVP 配置 |
| 配置键测试向量 | 正常、乱序、缺字段、重复和未知 ID 用例 |
| 价格规则 fixture | 两套模板和逐区覆盖后的唯一总价 |
| OpenAPI 与错误模型 | catalog、resolve、health 和静态资源 |
| Server 负向测试 | 路径穿越、非法 JSON、超大请求和版本冲突 |
| Web 状态线框 | 加载、缺图、冲突、空数据、窄屏抽屉 |
| 假图片资源包 | 16 配置 × 4 视角的低成本占位图 |
| 图片质量校验器设计 | RGBA、尺寸、Alpha、哈希、裁切和命名规则 |
| 车辆资产审计模板 | 部件、枢轴、骨骼、动作和降级字段 |
| 机位命名规范 | 交互机位与渲染视角分离 |
| 日志字段规范 | 版本、配置键、视角、错误码和耗时 |
| 资产授权表 | 车型、HDRI、贴图、字体和动画来源 |
| 测试环境矩阵 | UE 验收机、浏览器、DPR、触控和网络条件 |
| 文档模板 | 架构、ADR、模块 README、AI 工作流 |

## 修订后结论

完成上述修订后，MVP 的数据规模是可控的：16 个配置、4 个视角、64 张 Web 图片。UE 的环境和车辆动作保留完整演示价值，但不进入 Web 的组合维度。高风险的 Cook、Path Tracing、透明 Alpha、车辆动画和双配置打包通过五个探针提前验证，三端其余任务可继续并行。

## 官方技术依据

- [Epic Asset Management](https://dev.epicgames.com/documentation/unreal-engine/asset-management-in-unreal-engine)：Asset Manager 同时存在于 Editor 和打包程序，Primary Asset 可通过 ID 管理和加载。
- [Epic Packaging Unreal Engine Projects](https://dev.epicgames.com/documentation/unreal-engine/packaging-your-project)：Cook 会处理运行时资产，并可能剔除未被地图、代码或规则引用的内容。
- [Epic Path Tracer](https://dev.epicgames.com/documentation/unreal-engine/path-tracer-in-unreal-engine)：Path Tracer 依赖硬件光追；相机、材质和场景变化会使累积采样失效。
- [Epic Movie Render Pipeline](https://dev.epicgames.com/documentation/unreal-engine/movie-render-pipeline-in-unreal-engine)：MRQ/MRG 用于批量高质量输出，并要求启用相应插件和 Alpha Output 设置。
- [Epic Render Passes](https://dev.epicgames.com/documentation/unreal-engine/cinematic-render-passes-in-unreal-engine)：多重采样 Alpha 累积需要显式设置，并会增加渲染开销。
