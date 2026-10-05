# ConfigurationSystem 当前事实

更新时间：2026-10-05

本文件只保留已由当前契约、代码或用户确认的事实。分支、HEAD、工作树和发布版本等易变信息由每次会话实时检查。

## SC01 产品

机器可读源：`contracts/fixtures/sc01.catalog.draft.v2.json`

原始业务来源为 `docs/product-data/sc01/source/` 中的《SC01-定制选配清单》和《SC01-选配色卡》，文件身份由 `docs/product-data/sc01/source-manifest.json` 的 SHA-256 固定。不得只根据颜色名称推断显示色，也不得覆盖原件。

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

确定规则：

- 对外主名称为 `SC01`；“江铃 羿驰”只作为生产或资质信息。
- 最终目标是非敞篷 SC01。
- 正式模型尚未到位，代理车辆(奥迪A5)不得冒充最终车型。
- 流程为外饰 → 内饰 → 性能配置 → 其他个性化。
- A 柱默认 `a-pillar-woven`，“织布”、黑色、免费。
- 车顶默认 `roof-woven-standard`，“织布”、黑色、免费。
- 仪表台回中标默认 `ip-center-mark-uncovered-black`；四类材料及色卡均为 100 元。
- 座椅回中标四类材料及色卡均免费。
- 座椅背板默认 `seat-shell-carbon-original` /“高光原色碳纤维”。
- `seat-shell-custom` 为“自定义颜色”，支持亮面与雾面。
- 自定义颜色入口使用满幅彩虹方块。

## 工程边界

- `contracts/`：跨端 Schema、稳定 ID、fixture 与 OpenAPI。
- `source/clients/ue/`：UE5.8 Runtime/Editor 工程。
- `source/clients/web/`：React/Vite 统一选配内容。
- `source/server/`：Fastify API、Web 托管和图片发布。
- `tools/`：仓库级生成器和验证器。
- `docs/`：架构、决策、来源与验证证据。
- `package/`：被忽略的本地构建和发布产物。

SC01 配置使用完整 `surfaceId → optionId` 选择和 `selectionOrder`。稳定 ID 不按中文名称、UI 顺序、数组下标或 UE 路径推断。

UE 管理员模式是车辆、动画和内容包的受控接入口：所有输入先预检并进入暂存或候选状态，验收通过后才能激活，不能直接污染当前可用版本。

## 已验证流程

- 跨端修改：契约 → 消费端 → 集成测试。
- UE 视觉修改：自动化/编译 → UE5.8 GUI → Shipping 实机。
- 资产接入：授权 FBX → Maya 2025 → sidecar → 暂存导入 → GUI 验收。
- 大版本发布：跨端测试 → Shipping → Web build → 图片 Bake → manifest/像素校验 → 原子发布。
- Git：先检查状态和工作树；每个完整结果单独提交，不夹带来源不明文件。

## 任务路由

| 任务 | 主要位置 | 最低验证 |
|---|---|---|
| 产品字段、价格、选项 | `contracts/fixtures/`、三端消费者 | 契约 + 三端受影响测试 |
| Web UI/交互 | `source/clients/web/` | 测试、build、真实浏览器 |
| Server/API | `source/server/`、OpenAPI | 测试、HTTP smoke |
| UE 状态/Bridge | UE Runtime C++ | 编译、自动化、GUI |
| FBX/车辆/打包 | `harness/UE.md` 路由的规范 | 对应门禁、GUI、Shipping |
| Bake/发布 | Bake C++、Server 发布工具 | 任务、图片、manifest、原子发布 |

## 长期未完成

- 正式 SC01 模型的 38 surface 映射、层级、材质、动画和正式出图验收。
- 多视角 Alpha atlas、透明区域裁剪和偏移记录。
- 主驾/副驾 CubeMap 360 度内饰环视。

这些项目不得被代理车辆或程序化探针结果标记为已完成。
