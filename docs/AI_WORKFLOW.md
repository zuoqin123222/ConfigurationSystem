# AI 协作工作流

## 适用范围

本流程约束 AI 对 ConfigurationSystem 的代码、契约、UE 资产辅助工具和文档修改。目标是让每一步都有明确边界、可重复验证和可追溯证据，避免以“看起来可行”代替运行结果。

用户指令优先于默认流程。用户明确要求“不提交”时，AI 必须保留本地改动，不执行 `git commit` 或 `git push`；其余质量门槛仍然适用。

## 先计划

开始修改前先完成四项判断：

1. 明确交付结果、影响端和不可改变的兼容边界。
2. 读取相关实现、契约、ADR 和最近的探针结论。
3. 把任务拆成可独立验证的步骤，并标出依赖关系。
4. 为每一步指定验证命令和预期证据。

计划必须区分“已验证事实”“由现有代码推断”“待实现目标”。遇到 UE 版本、Cook、Shipping、渲染输出或线程边界问题时，不允许只根据旧版本经验作决定。

## 并行任务

没有文件写冲突且输入稳定的任务可以并行，例如：

- 同时读取 UE、Web、Server 和契约现状。
- 并行检查独立 Schema、文档链接和模块边界。
- 在接口冻结后，分别实现 Web、Server 和 UE 的消费者。
- 并行执行互不共享输出目录的探针。

存在下列关系时必须串行：

- 共享 Schema 尚未冻结，三个消费者都依赖其字段。
- 后一步需要前一步生成的资产、manifest 或版本号。
- 多个任务会修改同一文件、同一 UE 资产或同一 publication 目录。
- 需要先用探针决定公开 API、Cook 或 Alpha 方案。

并行结果合并前，重新运行跨端验证，不能把各自通过等同于集成通过。

## 探针先行

高风险引擎能力先写最小探针，再进入业务实现。探针应满足：

- 只验证一个明确问题。
- 同时包含成功路径和能揭示假阳性的负向条件。
- 输出机器可读 JSON，并以非零退出码表示失败。
- 记录引擎、平台、配置、硬件或驱动等环境。
- 给出可重复命令和实际结果。
- Development 与 Shipping 行为可能不同时，两种包都要验证。

现有探针及入口：

| 探针 | 文档 | 已冻结结论 |
|---|---|---|
| Primary Asset | `docs/technical-probes/PRIMARY_ASSET_PROBE.md` | 启动扫描、AlwaysCook、Asset Bundle 软引用 |
| Runtime Path Tracing | `docs/technical-probes/PATH_TRACING_RUNTIME_PROBE.md` | 公开 `ApplyViewMode` 适配和精确进度 |
| Path Tracing Alpha | `docs/technical-probes/PATH_TRACING_ALPHA_PROBE.md` | 反向 coverage 必须规范化为 straight Alpha |
| 车辆层级 | `docs/technical-probes/VEHICLE_HIERARCHY_PROBE.md` | 标签审计和可中途反向执行器；真实资产仍待验收 |
| 打包边界 | `docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md` | 项目地图进入 Cook，Editor 模块不进入 Runtime 包 |

引擎小版本、渲染后端、打包配置或关键依赖变化后，重跑受影响探针。旧探针证据只能说明原记录环境，不能自动覆盖新环境。

## C++ 优先

业务规则和状态机优先放在可编译、可测试的 C++：

- 产品配置、模板应用、价格和 canonical key。
- Primary Asset 枚举、筛选和加载。
- 镜头状态、车辆动作和可逆执行器。
- Path Tracing 能力检测、切换和进度快照。
- 内容校验、Bake 编排和 manifest 生成。

Data Asset 保存内容和资产引用，不承载跨资产分支。UMG Blueprint 负责布局、绑定和简单表现动画，不拼配置键、不计算价格、不扫描资产。Level Blueprint 不保存业务状态。

Blueprint 原型若用于快速验证，必须把最终业务规则迁回 C++，并保留相同输入输出的自动验证。不得因 GUI 修改方便而复制一套规则。

## GUI 边界

必须使用 Unreal Editor GUI 的工作限于：

- 导入和检查外部资产。
- 设置材质槽、骨骼、Pivot、ComponentTag 和 Mobility。
- 放置镜头、车辆与环境 Actor。
- 调整需要人工视觉判断的材质、灯光和构图参数。
- 对真实车辆执行穿模、铰链和画面质量验收。

可通过 C++、命令行或数据文件完成的工作不依赖 GUI，包括：

- 项目编译、Cook、打包和探针执行。
- 创建幂等测试资产或测试地图。
- Schema、OpenAPI、fixture 和 manifest 校验。
- canonical key、价格和发布完整性检查。

GUI 操作后要记录资产路径、关键字段和人工判断结果。只有界面截图而没有可重放设置或资产变更，不算完整证据。

## 版本化修改

稳定 ID 和已发布版本不可原地改义：

- JSON 结构由 `schemaVersion` 管理。
- 目录内容由 `catalogVersion` 管理。
- 图片发布由 `publicationVersion` 管理。
- 破坏性结构变更升级 Schema 主版本。
- 选项、价格或模板变化升级 catalog 版本。
- 任一图片或 manifest 变化发布新的不可变 publication 版本。

重大流程改造保留旧路径，新增 V2 实现并用同一 fixture 对照。新流程通过 Development、Shipping 和跨端验证后，才能移除旧流程。禁止同时改协议语义、迁移路径和删除兼容层而没有独立证据。

## 原子提交与推送

默认协作节奏是一项完整步骤对应一个原子提交：

1. 修改前确认 `git status --short`，避免覆盖他人未提交工作。
2. 只修改当前步骤需要的文件。
3. 运行该步骤的最小验证和受影响的回归验证。
4. 检查 diff，不提交生成文件、密钥、`package/` 或本地 UE 输出。
5. 用描述结果的提交信息创建一个提交。
6. 推送当前分支，并记录远端提交哈希。
7. 下一步从已推送状态继续。

提交不应混合无关格式化、重命名和功能变化。跨文件的单一契约变更可以作为一个原子提交，因为拆开后任一端都处于不可用状态。

以下情况不提交、不推送：

- 用户明确要求“不提交”或“只改本地”。
- 验证失败且改动不能作为独立、诚实的失败探针交付。
- 工作区包含来源不明的现有改动，无法安全隔离。
- 需要用户确认破坏性协议或资产变化。

用户要求不提交时，最终报告必须明确工作区保留了哪些文件，并给出验证结果；不得为了遵循默认节奏而违背指令。

## 每步验证

仓库级契约验证：

```powershell
node tools/validate-contracts.mjs
```

UE Editor 编译：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  ConfigurationSystemEditor Win64 Development `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

只改 Markdown 时至少执行：

```powershell
git diff --check
git status --short
```

并逐一确认新增相对链接指向存在的文件。涉及契约时运行 Node 验证；涉及 UE 模块、配置或 Content 时运行对应编译、探针或 Cook，不能用文档检查替代。

Windows GUI 程序使用 `Start-Process -Wait`，否则终端可能在探针写完 JSON 前返回。每个探针的完整参数以其文档为准，不从记忆中缩写命令。

## 验证证据

完成报告要包含：

- 实际执行的命令。
- 退出码或明确的通过/失败结果。
- 机器可读报告路径或测试摘要。
- 未执行项目及原因。
- 环境限制，例如缺少 UE、GPU、正式车辆或浏览器。
- 与原计划不同的降级方案。

“代码已写完”“理论上可用”“未发现问题”不能代替证据。编译通过不等于 Cook 通过，Development 通过不等于 Shipping 通过，程序化 Cube 层级通过不等于真实车辆通过。

## 失败处理

失败发生后保留第一现场：

1. 记录失败命令、退出码和关键日志。
2. 判断是实现缺陷、环境缺失、数据问题还是旧假设失效。
3. 缩小到最小复现，必要时新增探针。
4. 修复后重新执行原失败命令和相关回归。
5. 若无法修复，说明阻塞条件、已验证范围和未验证范围。

禁止以下行为：

- 把未运行描述成通过。
- 用上一次 JSON 代替本次执行结果。
- 删除失败项或返回零退出码来让流水线变绿。
- 把 Editor 结果外推为 Shipping 结果。
- 把占位资产结果外推为真实车辆结果。
- 用错误图片、最近配置或默认版本静默替代缺失结果。

## 完成报告

报告按“结果、改动、验证、限制、提交状态”的顺序组织。只引用实际文件和实际命令。若用户要求不提交，提交状态写明“未提交、未推送”，并保留 `git status --short` 可见改动。

架构边界见 [ARCHITECTURE.md](ARCHITECTURE.md)，已定方案见 [DECISIONS.md](DECISIONS.md)，贡献要求见 [CONTRIBUTING.md](../CONTRIBUTING.md)。
