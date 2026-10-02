# 贡献指南

## 开始修改

本仓库同时包含 UE Runtime、UE Editor、Web、Server、共享契约和探针证据。修改前先阅读：

- [系统架构](docs/ARCHITECTURE.md)
- [AI 协作工作流](docs/AI_WORKFLOW.md)
- [架构决策记录](docs/DECISIONS.md)
- [共享契约](contracts/README.md)

先运行：

```powershell
git status --short
```

不要覆盖或顺手提交来源不明的本地改动。任务涉及 UE 引擎能力、Cook、Shipping、资产发现或渲染输出时，先找到对应技术探针；现有证据不能覆盖不同引擎、平台或硬件环境。

## 修改边界

- 产品源码写入 `source/`。
- 三端共享字段、ID、fixture、Schema 和 API 写入 `contracts/`。
- 仓库级验证脚本写入 `tools/`。
- `source/clients/ue/SourceAssets/` 只保存可追溯的源侧参考文件；禁止 `.uasset`、`.umap`、`.tps`。
- 计划、架构、ADR 和探针记录写入 `docs/`。
- 构建与发布结果写入被忽略的 `package/`。
- 不提交 `Binaries/`、`Intermediate/`、`Saved/`、`package/`、Node 构建目录、日志或本地环境文件。

`contracts/` 与 `tools/` 当前位于仓库根目录。不要使用旧计划中的 `source/contracts/` 或 `source/tools/`。

## 实现约束

### 跨端契约

- 跨端只传稳定的小写 kebab-case ID。
- canonical key 固定按 `paint → wheel → interior → frame` 连接完整 `optionId`。
- 不使用中文名、UI 顺序、数组下标、对象遍历结果或 UE 资产路径推断 ID。
- `interactionCameraId` 与 `renderViewId` 不得互换。
- 环境、动作、镜头和 Path Tracing 状态不进入产品配置键。
- 未知 Schema 主版本必须拒绝，不做猜测映射。

### UE Runtime 与 Editor

- `ConfigurationSystem` Runtime 模块不得依赖 `UnrealEd`、`ToolMenus` 或 Renderer Private 头文件。
- 创建、保存或刷新 `.uasset` 和地图的命令只放在 `ConfigurationSystemEditor`。
- 业务规则、状态机、价格和配置键优先使用 C++。
- Data Asset 只保存内容和资产引用；UMG Blueprint 不复制领域规则。
- C++、命令行或探针无法可靠确认资产和视觉状态时，主动打开 UE5.8 Editor 使用 GUI 检查，不允许猜测结论。
- GUI 修改或验收后记录资产路径、关键字段和操作结果，并在可行时补充自动回归。
- Asset Manager 扫描规则写入 `DefaultGame.ini`，不通过运行时补扫掩盖错误。
- 修改模块依赖、地图或 Cook 配置后，执行 P0-5 Development/Shipping 边界验证。

### 发布图片

- 不发布 Path Tracing 原始 PNG。
- 输出必须规范化为 straight Alpha，并保留 coverage、自发光恢复、透明 RGB、裁切和哈希证据。
- 新 publication 写入新版本目录，完整校验后原子切换。
- 缺图或校验失败时保留旧版本，不使用错误图片静默替代。

## 提交前验证

### 所有改动

- [ ] `git diff --check` 无空白错误。
- [ ] `git status --short` 只包含本任务文件。
- [ ] diff 不包含密钥、本地绝对临时路径、生成物或无关格式化。
- [ ] 新增 Markdown 相对链接均指向存在的文件。
- [ ] 文档区分已实现、已验证、规划中和资产待验证状态。

执行：

```powershell
git diff --check
git status --short
```

### 契约、fixture 或跨端 ID

- [ ] Schema JSON 可解析。
- [ ] option、template、PrimaryAssetId 和预览图引用有效。
- [ ] 16 个配置键唯一且符合固定顺序。
- [ ] 4 个视角和 64 个图片期望完整。
- [ ] 破坏性变化已升级版本并记录 ADR。

执行：

```powershell
node tools/validate-contracts.mjs
node tools/validate-source-assets.mjs
```

当前成功摘要应包含：

```text
5 个 Schema JSON、P0-3 manifest 结构、2 类车辆 sidecar、
2 个有效与 2 个无效 fixture、8 个选项、2 个模板、16 个唯一组合、
4 个视角、64 个图片期望
```

### UE C++、Build.cs 或配置

- [ ] Editor Target 编译通过。
- [ ] Runtime 模块没有新增 Editor-only 依赖。
- [ ] 公开接口和生命周期、线程、坐标约定有必要注释。
- [ ] 涉及引擎边界时重跑对应探针。

Editor 编译命令：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  ConfigurationSystemEditor Win64 Development `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

若本机 UE 安装位置不同，只替换引擎根路径，不改变项目路径、Target 或配置后省略验证。

### Primary Asset 或 Cook

- [ ] Editor 启动扫描能发现资产。
- [ ] Development 包能枚举并异步加载 Data Asset 与软引用。
- [ ] Shipping 包能枚举并异步加载 Data Asset 与软引用。
- [ ] 三个环境发现数一致。

完整步骤见 [Primary Asset 探针](docs/technical-probes/PRIMARY_ASSET_PROBE.md)。测试资产创建与扫描必须使用两个独立 Editor 进程。

### Runtime Path Tracing

- [ ] Editor Game、Cooked Development、Cooked Shipping 都能切换模式。
- [ ] 精确样本数增长、相机变化归零、重新累计均有证据。
- [ ] 返回实时模式成功。
- [ ] 不支持硬件显示明确原因。

完整步骤见 [Runtime Path Tracing 探针](docs/technical-probes/PATH_TRACING_RUNTIME_PROBE.md)。每次 UE 小版本升级都重跑。

### Alpha 与 Bake

- [ ] 检测原始 Alpha 语义。
- [ ] 输出为 straight Alpha。
- [ ] 自发光/Bloom 没有因清理透明 RGB 丢失。
- [ ] 黑、白、灰、红和棋盘背景合成通过。
- [ ] manifest 包含尺寸、哈希和规范化字段。

完整步骤见 [Path Tracing Alpha 探针](docs/technical-probes/PATH_TRACING_ALPHA_PROBE.md)。

### 车辆层级与动作

- [ ] 必需 ComponentTag 唯一，且位于 `Movable` Pivot。
- [ ] 父子关系通过 `UVehicleHierarchyAuditor::AuditActor`。
- [ ] 动作支持中途反向和三轮重复，不累积终态误差。
- [ ] 真实车辆完成 Pivot、穿模和 Path Tracing 重置检查。
- [ ] 模型与动画 sidecar 通过 `node --test tools/validate-vehicle-sidecars.test.mjs`。

程序化探针通过不能替代真实车辆验收。完整门槛见 [车辆层级探针](docs/technical-probes/VEHICLE_HIERARCHY_PROBE.md) 和 [车辆资产接入规范](docs/VEHICLE_ASSET_REQUIREMENTS.md)。

### 项目地图与 Editor 隔离

- [ ] Development 和 Shipping 启动进入 `/Game/Maps/L_ConfigProbe`。
- [ ] C++ 硬引用、Primary Asset 和软纹理均可加载。
- [ ] 归档文件名扫描没有 Editor 模块。
- [ ] `FModuleManager` 报告 Editor 模块不存在且未加载。

完整命令见 [打包边界探针](docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md)。

## 提交与推送

一次提交只表达一个完整结果。共享契约及其多个消费者若必须同时变化，可以作为一个原子提交；不要把无关重命名、格式化或清理混入功能提交。

提交前检查：

- [ ] 变更范围与任务一致。
- [ ] 最小验证和受影响回归均已执行。
- [ ] 失败结果没有被删除、隐藏或写成通过。
- [ ] 提交信息描述行为变化，不只写“update”或“fix”。
- [ ] 推送后记录远端提交哈希。

建议顺序：

```powershell
git diff --check
node tools/validate-contracts.mjs
git status --short
git diff
git add <本步骤文件>
git commit -m "<type>: <结果>"
git push
```

用户明确要求“不提交”时，到 `git diff` 和验证为止；不要执行 `git add`、`git commit` 或 `git push`。

## 验证报告

提交说明或交付报告需要列出：

- 实际执行的命令和结果。
- 生成的 JSON、日志或测试摘要。
- 因环境限制未执行的验证。
- 仍待真实资产、目标 GPU、Shipping 或人工视觉判断的范围。
- 提交与推送状态。

不得用“理论上可用”代替未执行的测试，不得用旧 JSON 冒充本次结果，也不得把 Editor、Development 或占位资产结果外推到未验证环境。
