# ConfigurationSystem Agent Harness

本文件适用于整个仓库，是新 AI 会话进入工程时的首要入口。详细事实、模块路由和验证矩阵见
[`docs/HARNESS.md`](docs/HARNESS.md)。

## 指令优先级

发生冲突时按以下顺序处理，禁止把旧摘要覆盖到新实现上：

1. 用户在当前对话中的明确指令。
2. 当前工作树中可运行的代码、契约、测试和实际 Git 状态。
3. 带时间和验证证据的最新 ConfigurationSystem 项目记录。
4. 已接受且尚未被后续实现取代的 ADR、产品决策和资产政策。
5. README、旧计划、旧探针和历史对话摘要。

“其他对话已完成”只代表可检索的历史证据。只有提交确实位于当前分支或工作树，才能视为当前代码事实。

## 每次会话先做

在修改、构建或判断完成状态前：

```powershell
git status --short --branch
git worktree list
git log -n 12 --date=iso --pretty=format:'%h|%ad|%s'
node tools/validate-harness.mjs
```

随后只读取与任务有关的入口：

- 全局边界：`docs/HARNESS.md`、`docs/AI_WORKFLOW.md`、`docs/ARCHITECTURE.md`
- 已冻结决策：`docs/DECISIONS.md`
- SC01 产品：`docs/PRODUCT_CONTEXT_SC01.md`、`docs/product-data/sc01/`
- 共享协议：`contracts/README.md`
- UE：`source/clients/ue/README.md`
- Web：`source/clients/web/README.md`
- Server：`source/server/README.md`
- 资产：`docs/REFERENCE_ASSET_POLICY.md`、`docs/DCC_VEHICLE_MODELING_EXPORT_GUIDE.md`
- 发布：`docs/technical-probes/BAKE_RELEASE_VALIDATION.md`

先确认当前分支、HEAD、其他工作树和未提交文件。不得覆盖来源不明的改动，也不得擅自删除未跟踪的烘焙、文档或用户文件。

## 上下文预算

仓库文件量大，但受 Git 跟踪的源码规模远小于生成物。默认不要递归读取或索引：

- `.git/`、`.trae-html-share-packages/`
- `package/`
- `**/Binaries/`、`**/Intermediate/`、`**/Saved/`、`**/DerivedDataCache/`
- `**/node_modules/`、`**/dist/`、`**/coverage/`
- 大批 PNG、WEBP、EXR、FBX、PAK、UCAS、UTOC、UASSET 和 UMAP
- 根目录临时烘焙目录，例如 `SC01-fullres-review-*`

先用路径、文件名、符号或稳定 ID 定向搜索，再读取最小必要范围。二进制 UE 资产的真实层级、Pivot、材质槽和视觉效果应通过 UE5.8 GUI 与探针确认，不能把二进制内容猜成文本。

## 当前产品硬约束

- 对外产品主名称为 `SC01`；“江铃 羿驰”只作为生产或资质信息。
- 正式 SC01 是非敞篷车型，不实现敞篷机构、动画或 UI。
- 正式 SC01 模型尚未到位；当前 Audi A5 仅为内部授权代理资产，不得冒充最终 SC01。
- SC01 基础价为 `22980000` 分（22.98 万元），目录仍可保持 `lifecycle=draft` 和 `quotable=false`。
- 当前目录以 `contracts/fixtures/sc01.catalog.draft.v2.json` 为产品与 UI 的主要事实源。
- 当前结构为 4 个阶段、16 个部件、38 个 surface、17 个材料族、154 个 option；变化时必须同步更新 harness 和验证。
- A 柱、车顶默认黑色“织布”，不得写成“织物羊毛”。
- 仪表台“回中标”默认“无包覆(黑)”；四类材料及颜色均为 100 元。
- 座椅“回中标”四类材料及颜色均免费。
- 座椅“背板”默认“高光原色碳纤维”；自定义取色支持亮面和雾面。
- 自定义颜色图标使用满幅彩虹方块，不使用渐变圆环。

完整且可校验的当前事实见 `docs/HARNESS.md`。如果文档与 catalog、测试或最新已合并代码冲突，先查明时间线，再修正文档，不要反向篡改当前实现去迎合旧描述。

## 架构边界

- `contracts/` 是 UE、Web、Server 的共享协议和稳定 ID 来源。
- `source/clients/ue/` 是 UE5.8 Runtime/Editor 工程。
- `source/clients/web/` 是 React/Vite 统一选配内容。
- `source/server/` 是 Fastify 服务、目录和不可变图片发布入口。
- `tools/` 是仓库级生成器和验证器。
- `docs/` 是架构、决策、来源和可复验证据。
- `package/` 只放可重建发布物，禁止作为源码事实或提交内容。

跨端变更顺序固定为：先冻结契约和兼容策略，再修改消费者，最后做跨端回归。稳定 ID 不得原地改义；需要兼容旧配置时使用显式 alias 或版本迁移。

UE 业务规则、状态机、相机、动作、材质绑定和 Bridge 白名单优先放在可编译测试的 C++。React 是选配内容和业务交互的唯一 UI 实现，UE 不复制目录与价格 UI。UE Runtime 不得依赖 Editor-only 模块。

## 资产与授权

- 外部资产先确认许可、来源和 SHA-256，未知授权一律停止。
- Epic Automotive Configurator 的专项授权只覆盖指定样例的内部研发、分析和测试，不允许公开分发、转授权或训练集用途。
- 样例资产不能直接迁移 `.uasset`、Blueprint、材质、纹理或项目配置。
- 允许的车辆路径是：明确授权的 FBX → Maya 2025 规范化 → sidecar → 管理员暂存导入 → UE GUI 验收。
- DCC 节点用 `Vehicle_Root` 风格；UE ComponentTag 用 `Vehicle.Root` 风格。
- 不直接修改生成器维护的 `M_SC01_*` 母材质；视觉调节优先创建 `MI_` 实例。

## 验证与完成

按变更面选择最小验证，并补做受影响回归：

- Harness/文档：`node tools/validate-harness.mjs`、`git diff --check`
- 契约：`node tools/validate-contracts.mjs`
- Web：在 `source/clients/web` 执行 `npm test`、`npm run build`
- Server：在 `source/server` 执行 `npm test`
- UE C++：编译 `ConfigurationSystemEditor Win64 Development`
- UE 资产、镜头、材质、灯光和交互：对应自动化 + UE5.8 GUI
- Cook/Runtime/发布边界：对应探针 + 实际 Development/Shipping 包
- 图片发布：完整 manifest、尺寸、哈希、Alpha/RGBA 校验后原子切换新 `publicationVersion`

大版本或跨端视觉变更必须完成真实 GUI 验收、Windows Shipping 重打包和 Web 图片重新烘焙。编译通过不等于 Cook、Shipping 或视觉验收通过。

默认在验证通过后创建单一目的的提交并推送当前分支；用户明确要求不提交时不得提交。若工作区存在来源不明的改动，先隔离或报告，不得顺手纳入。

## 更新 Harness

以下变化必须同时更新 `docs/HARNESS.md`，必要时更新本文件和
`tools/validate-harness.mjs`：

- 产品基础价、阶段、部件、surface、材料族、option 数量或关键默认项。
- 跨端职责、Bridge 协议、相机/动画规则或发布流程。
- 新工作树成为集成基线，功能分支合并或废弃。
- 正式 SC01 模型到位或授权边界变化。
- UE 版本、目标平台、构建和验证入口变化。

历史原因保留在 ADR 或阶段文档中；Harness 只描述当前有效规则，并在冲突处明确指出被取代的旧结论。
