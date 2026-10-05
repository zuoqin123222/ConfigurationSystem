# ConfigurationSystem 项目 Harness

更新时间：2026-10-05

本文件只保留已由当前契约、代码或用户确认的事实。分支、HEAD、工作树和发布版本等易变信息由每次会话实时检查，不写成长期约束。

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

`draft` 与 `quotable=false` 不表示基础价未知；正式订单报价仍关闭。

当前关键选配规则：

- A 柱默认 `a-pillar-woven`，显示“织布”，固定黑色且免费；“织布”与“织物羊毛”是不同材料。
- 车顶默认 `roof-woven-standard`，显示“织布”，固定黑色且免费。
- 仪表台回中标默认 `ip-center-mark-uncovered-black`；奥司维、Alcantara、超纤皮、牛皮及其色卡均为 100 元。
- 座椅回中标默认 `seat-headrest-mark-ultrasuede`；四类材料及其色卡均免费。
- 座椅 `seat-shell-back` 显示名为“背板”，默认 `seat-shell-carbon-original` /“高光原色碳纤维”。
- `seat-shell-custom` 显示“自定义取色”，支持亮面与雾面。
- 所有自定义颜色入口使用 `source/clients/web/public/sc01/option-icons/rainbow.svg` 的满幅彩虹方块。
- 产品流程为外饰 → 内饰 → 性能配置 → 其他个性化；Web 不在顶部增加材料筛选。
- 消费者主名称始终为 `SC01`。“江铃 羿驰”只能作为生产或资质信息。

## 工程边界

- 最终目标是非敞篷 SC01；正式模型尚未到位，代理车辆不得冒充最终车型。
- 正式车辆接入必须经过授权与哈希检查、Maya 2025 规范化、sidecar、管理员暂存导入和 UE GUI 验收。
- `contracts/` 是跨端协议来源；SC01 配置使用完整 `surfaceId → optionId` 选择和 `selectionOrder`。
- UE5.8 Runtime C++ 负责状态、相机、动画、材质绑定、Path Tracing、内容包和受限 Bridge；Editor 模块负责资产生成、导入和审计。
- React/Vite 是选配内容的唯一 UI 实现；UE 不复制目录和价格逻辑。
- Fastify Server 托管生产 Web、目录、API 和不可变图片发布。
- Web 图片发布必须使用新 `publicationVersion`，完整校验后原子切换；失败时保留旧版本。

## 已验证流程

- 跨端修改：契约 → 消费端 → 集成测试。
- UE 视觉修改：自动化/编译 → UE5.8 GUI → Shipping 实机。
- 资产接入：授权 FBX → Maya 2025 → sidecar → 暂存导入 → GUI 验收。
- 大版本发布：跨端测试 → Shipping → Web build → 图片 Bake → manifest/像素校验 → 原子发布。
- Git：先检查状态和工作树；每个完整结果单独提交，不夹带来源不明文件。

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

```powershell
node tools/validate-harness.mjs
node tools/validate-contracts.mjs
git diff --check
git status --short
```

| 变更 | 必须执行 |
|---|---|
| Web | `npm test`、`npm run build`、真实浏览器验收 |
| Server | `npm test`、HTTP smoke |
| UE C++ | Editor 编译、对应自动化 |
| UE 资产/视觉 | 对应自动化、UE5.8 GUI |
| 发布 | Windows Shipping、Web build、Bake 与发布校验 |

具体 UE 命令从对应探针文档读取。编译通过不能替代 Cook、Shipping 或视觉验收。

## 长期未完成项

- 正式 SC01 模型到位后的 38 surface 映射、层级、材质、动画和正式出图验收。
- 多视角 Alpha atlas、透明区域裁剪和偏移记录。
- 主驾/副驾 CubeMap 360 度内饰环视。

这些项目不得被代理车辆或程序化探针结果标记为已完成。

产品事实或验证入口变化时，同步更新本文件和 `tools/validate-harness.mjs`。不确定、临时或仅存在于未合并分支的信息不写入 Harness。
