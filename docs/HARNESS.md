# ConfigurationSystem 项目 Harness

更新时间：2026-10-05

本文件为大规模仓库的新会话提供最小、稳定、可验证的上下文。它不替代代码、契约、测试或 ADR，而是告诉执行者先看什么、哪些结论仍有效、哪些历史描述已经过时。

## 事实判定

同一问题出现多个答案时，按以下顺序判定：

1. 用户当前明确指令。
2. 当前工作树中的代码、契约、测试、Git 状态和实际运行结果。
3. 最新时间戳且有提交、测试、GUI 或发布证据的项目记录。
4. 尚未被后续实现取代的 ADR、产品决策和资产政策。
5. README、旧计划、旧探针和历史对话摘要。

对话记录用于发现线索，不直接证明代码已经进入当前工作树。跨对话继续任务时，必须用 `git branch --all`、`git worktree list` 和 `git log --all` 定位真实提交。

## 启动快照

每个新会话先运行：

```powershell
git status --short --branch
git worktree list
git branch --all --verbose --no-abbrev
git log --all --date-order -n 30 --oneline --decorate
node tools/validate-harness.mjs
```

截至本文件更新时间：

- 主工作树：`D:\ConfigurationSystem`，分支 `main`，基线 `d52c99e`。
- 烘焙质量工作树：`D:\ConfigurationSystem-web-bake-quality`，分支 `feature/web-bake-quality`，基线 `41be77f`。
- `feature/web-bake-quality` 包含尚未合入 `main` 的全分辨率烘焙、Web 复位和 UE Bridge `reset` 白名单改动。
- 主工作树存在用户或其他会话留下的未跟踪渲染、文档、配置和启动脚本。它们不是 Harness 的清理目标，不得擅自删除或提交。

以上 HEAD 只用于说明 2026-10-05 的分叉事实。后续会话必须以命令输出为准，不能永久假设这些哈希仍是最新状态。

## 当前产品事实

当前机器可读源为 `contracts/fixtures/sc01.catalog.draft.v2.json`：

| 项目 | 当前值 |
|---|---|
| `schemaVersion` | `2.0.0` |
| `catalogVersion` | `sc01-draft-20260121` |
| `vehicleId` | `sc01` |
| 展示名称 | `SC01` |
| 基础价 | `22980000` 分 |
| 生命周期 | `draft` |
| 正式报价 | `quotable=false` |
| 阶段 | 4 |
| 部件 | 16 |
| surface | 38 |
| 材料族 | 17 |
| option | 154 |
| 语义机位 | `exterior`、`wheel`、`driver`、`seat`、`front-cabin` |

`draft` 与 `quotable=false` 不再表示基础价未知。早期“基础价和所有金额必须为 null”的描述已被后续产品确认与实现取代；正式订单报价仍关闭。

当前关键选配规则：

- A 柱默认 `a-pillar-woven`，显示“织布”，固定黑色且免费；“织布”与“织物羊毛”是不同材料。
- 车顶默认 `roof-woven-standard`，显示“织布”，固定黑色且免费。
- 仪表台回中标默认 `ip-center-mark-uncovered-black`；奥司维、Alcantara、超纤皮、牛皮及其色卡均为 100 元。
- 座椅回中标默认 `seat-headrest-mark-ultrasuede`；四类材料及其色卡均免费。
- 座椅 `seat-shell-back` 显示名为“背板”，默认 `seat-shell-carbon-original` /“高光原色碳纤维”。
- `seat-shell-custom` 显示“自定义取色”，支持亮面与雾面；当前通过 `PaintCustomization.roughness` 表达。
- 所有自定义颜色入口使用 `source/clients/web/public/sc01/option-icons/rainbow.svg` 的满幅彩虹方块。
- 产品流程为外饰 → 内饰 → 性能配置 → 其他个性化；Web 不在顶部增加材料筛选。
- 消费者主名称始终为 `SC01`。“江铃 羿驰”只能作为生产或资质信息。

## 车型和资产

- 最终目标是非敞篷 SC01。敞篷机构、动画和配置逻辑不在生产范围。
- 正式 SC01 三维模型尚未到位。当前授权 Audi A5 只是内部代理资产，不得对外暗示为 SC01 最终造型。
- 正式模型到位后必须走管理员 FBX 暂存流程，验证授权、哈希、朝向、尺寸、骨骼、Pivot、材质槽、玻璃、动作、LOD、穿模和 Path Tracing，再发布新的不可变 Render 版本。
- 当前骨骼车辆标准为 +X 向前、+Z 向上、Z=0 落地、30 fps；非目标骨骼在动作区间开始前复位。
- 代理车辆含车门、机盖、后备箱、车轮和静止卡钳骨骼。卡钳不随车轮滚动。
- 生成器维护的 `M_SC01_*` 母材质不能直接手调；视觉生产使用 `MI_` 实例，并在固定 Studio 曝光下同时检查 Lit 和 Path Tracing。

授权边界以 `docs/REFERENCE_ASSET_POLICY.md` 为准。Epic 样例专项许可只覆盖内部研发、分析和测试，源文件、派生几何、导入资产和授权截图不得推送到公开远端。

## 三端现状

### 共享契约

`contracts/` 是稳定 ID、Schema、fixture、OpenAPI 和发布形状的唯一跨端协议来源。SC01 使用 `surfaceId → optionId` 完整选择和 `selectionOrder`，历史 `demo-car` v1 仅保留为自动化 fixture。

稳定 ID 不得按中文名称、UI 顺序、数组下标或 UE 路径推断。已发布 ID 不得原地改义；兼容旧配置时使用明确 alias 或版本迁移。

### UE

工程入口为 `source/clients/ue/ConfigurationSystem.uproject`，版本为 UE5.8。Runtime C++ 负责状态、相机、动画、材质绑定、Path Tracing、内容包和受限 Web Bridge；Editor 模块负责资产生成、导入和审计。

当前相机规则：

- 车外自由视角支持左键环绕、右键/中键平移和滚轮推拉。
- 车外预设使用绕车圆弧，并持续看向车辆焦点。
- 主驾、副驾及车内外切换使用黑屏，不做空间飞行动画。
- 车内禁止平移，只允许受限旋转和推拉。
- 右侧面板导致的构图偏移使用非对称投影处理，不移动相机 Transform。
- Path Tracing 离线构图必须与实时预览一致。

Path Tracing 可能因 RTPSO 同步编译表现为长时间假死。安全预热、驱动门禁、缓存和超时回退优先于直接切换；不能把无响应直接诊断为显存耗尽。

### Web

React/Vite 是选配内容和业务交互的唯一 UI 实现。独立 Web 显示烘焙图，UE 通过 CEF/Bridge 复用同一套内容和顶栏控制。不要在 UE UMG 再复制目录、价格或同等选配 UI。

CEF 透明叠加需保持 Gamma 修正边界，圆角视口使用原生反向遮罩方案。大屏和嵌入模式都要检查窄宽度、固定总价、滚动区域、弹层裁剪和输入焦点。

`feature/web-bake-quality` 的新改动包括 2044×1328 全分辨率渲染适配、29 张内饰部件图、复位动作和 Bridge 透传。在合并前，这些只能视为该分支事实。

### Server

Fastify Server 托管生产 Web、目录、API 和不可变发布图片。Web 必须通过 HTTP 访问，不直接打开本地 `index.html`。

发布过程写入新 `publicationVersion`，完成图片数、尺寸、sRGB、Alpha/RGBA、哈希和 manifest 校验后原子切换。失败时继续保留旧版本，不静默回退到错误图片或“最近配置”。

## 任务路由

| 任务 | 首先读取 | 主要修改位置 | 最低验证 |
|---|---|---|---|
| 产品字段、价格、选项 | `contracts/README.md`、SC01 来源映射 | `contracts/fixtures/`、Schema、三端消费者 | 契约 + Web + Server + UE 领域测试 |
| Web UI/交互 | Web README、`App.tsx`、相关测试 | `source/clients/web/` | `npm test`、`npm run build`、真实浏览器 |
| Server/API | Server README、OpenAPI | `source/server/`、`contracts/openapi.yaml` | `npm test`、HTTP smoke |
| UE 状态/Bridge | UE README、Catalog、Bridge 测试 | UE Runtime C++ | Editor 编译 + 对应自动化 + GUI |
| 相机/输入 | 相机控制器、体验测试、Catalog 机位 | UE Runtime 和必要配置 | `CameraOrbit`/体验测试 + GUI + Shipping |
| 材质/灯光 | 视觉制作指引、材质库、Binder | `Content/SC01`、Binder、MI 实例 | Binder 测试 + Lit/PT GUI |
| 车辆 FBX/动画 | DCC 指引、sidecar Schema、资产政策 | `SourceAssets`、Editor 导入工具 | sidecar + 预检 + UE GUI |
| Bake/发布 | Bake 验收文档、manifest Schema | Bake C++、Server 发布工具 | 任务完整性 + 图片校验 + 原子发布 |
| Harness/文档 | 本文件、`AGENTS.md` | 根入口、`docs/`、验证器 | `node tools/validate-harness.mjs` |

## 验证矩阵

### 快速门禁

```powershell
node tools/validate-harness.mjs
node tools/validate-contracts.mjs
git diff --check
git status --short
```

### Web

```powershell
npm test
npm run build
```

工作目录：`source/clients/web`

### Server

```powershell
npm test
```

工作目录：`source/server`

### UE 编译

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  ConfigurationSystemEditor Win64 Development `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

自动化测试名和完整命令从对应探针文档读取，不从历史记忆缩写。涉及真实资产、镜头、材质、灯光、CEF、玻璃、Bloom、穿模和构图时，命令行结果必须补充 UE GUI 或真实浏览器证据。

### 发布门槛

大版本或跨端视觉变更的完成定义：

1. 契约、Web、Server 和受影响的 UE 自动化通过。
2. UE5.8 GUI/实际客户端完成交互与视觉验收。
3. Windows Shipping Build/Cook/Stage/IoStore/Archive 成功并启动。
4. Web production build 更新。
5. 渲染图片重新 Bake，manifest 和像素校验通过。
6. 新 publication 原子切换，Server `/health` 返回预期版本。
7. diff 只包含任务文件，形成独立提交并推送；用户要求不提交时例外。

## 已知文档漂移

以下历史描述不能脱离时间线直接使用：

- `docs/ARCHITECTURE.md` 仍包含“Web 与 Server 是骨架”和早期 SC01 金额全为 `null` 的描述。
- `docs/product-data/sc01/FIELD_DECISIONS_V2.md` 仍记录早期不可报价原子阶段。
- `source/clients/ue/README.md` 的部分章节仍以 v1 四分区和占位车辆为主。
- `source/clients/web/README.md` 的部分章节仍以 v1 固定四分区界面为主。
- `docs/DECISIONS.md` 的 ADR-010 描述的是早期“UE 原生控制 + 右侧 Web 面板”，后续已演进为 Web 叠加控制和受限 Bridge。

这些文件包含仍有价值的历史理由和命令，不能粗暴删除；执行当前任务时以实际 catalog、代码、测试和最新已合并实现为准。若修改相关领域，应同步修正文档或新增取代 ADR。

## 长期未完成项

- 正式 SC01 模型到位后的 38 surface 映射、层级、材质、动画和正式出图验收。
- 多视角 Alpha atlas、透明区域裁剪和偏移记录。
- 主驾/副驾 CubeMap 360 度内饰环视。

这些项目不得被代理车辆或程序化探针结果标记为已完成。

## Harness 维护

`node tools/validate-harness.mjs` 会检查入口文件、SC01 关键事实、文档链接和禁止跟踪目录。若产品结构发生有意变化，应在同一提交中更新：

- `contracts/fixtures/sc01.catalog.draft.v2.json`
- 受影响的 Schema、fixture 和三端测试
- `AGENTS.md`
- 本文件
- `tools/validate-harness.mjs` 中的期望快照

Harness 只保存当前有效结论。历史变化写入 ADR、来源映射或技术探针，并附上时间、提交和验证证据。
