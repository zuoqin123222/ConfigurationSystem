# ConfigurationSystem

汽车选配系统，包含 UE5.8 Windows 桌面端、响应式 Web 选配端和配套 Server。

## 文档

- [AI 项目 Harness](harness/AGENTS.md)
- [系统架构与三端边界](docs/ARCHITECTURE.md)
- [AI 协作工作流](docs/AI_WORKFLOW.md)
- [架构决策记录](docs/DECISIONS.md)
- [贡献与验证指南](CONTRIBUTING.md)
- [MVP 实施计划](docs/MVP_PLAN.md)
- [MVP 多重验证报告](docs/MVP_PLAN_REVIEW.md)
- [P0-1 Primary Asset 扫描与 Cook 探针](docs/technical-probes/PRIMARY_ASSET_PROBE.md)
- [P0-2 Runtime Path Tracing 与进度探针](docs/technical-probes/PATH_TRACING_RUNTIME_PROBE.md)
- [P0-3 Path Tracing 透明出图探针](docs/technical-probes/PATH_TRACING_ALPHA_PROBE.md)
- [P0-4 车辆层级与可逆动作探针](docs/technical-probes/VEHICLE_HIERARCHY_PROBE.md)
- [P0-5 Development/Shipping 打包边界探针](docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md)
- [统一发布流水线验证](docs/technical-probes/UNIFIED_RELEASE_PIPELINE.md)
- [车辆资产接入规范](docs/VEHICLE_ASSET_REQUIREMENTS.md)
- [DCC 车辆模型制作与导出指南](docs/DCC_VEHICLE_MODELING_EXPORT_GUIDE.md)
- [参考资产政策](docs/REFERENCE_ASSET_POLICY.md)
- [外部参考资产与 Maya 规范化验证](docs/technical-probes/REFERENCE_ASSET_PIPELINE.md)
- [SC01 v2 契约验证](docs/technical-probes/SC01_V2_CONTRACT.md)
- [SC01 Server v2 验证](docs/technical-probes/SERVER_V2_STAGE.md)
- [SC01 Web v2 验证](docs/technical-probes/WEB_V2_STAGE.md)
- [SC01 全量目录与色卡验证](docs/technical-probes/SC01_FULL_CATALOG_STAGE.md)
- [UE SC01 v2 数据驱动验证](docs/technical-probes/UE_SC01_V2_STAGE.md)
- [SC01 材质体系验证](docs/technical-probes/SC01_MATERIAL_SYSTEM_STAGE.md)
- [后续 Alpha 裁剪、混合与内饰 CubeMap 约束](docs/FUTURE_RENDER_FEATURES.md)
- [Runtime 内容包契约与安全挂载](docs/CONTENT_PACK_RUNTIME.md)
- [三端数据与 API 契约](contracts/README.md)
- [MVP 可视化评审版](automotive-configurator-mvp-plan/automotive-configurator-mvp-plan.html)

## 项目组成

- **UE5.8 桌面端**：主工程，提供完整的车辆选配效果与车辆实时渲染。
- **Web 端**：适配手机竖屏和大屏，通过 Server 下发的烘焙图片实现车辆选配。
- **Server**：管理并下发预先烘焙的车辆配置图片，为 Web 端选配流程提供服务。

## 目录规划

```text
ConfigurationSystem/
├── source/                  # Git 管理的源码
│   ├── clients/
│   │   ├── ue/              # UE5.8 Windows 桌面端 C++ 主工程
│   │   └── web/             # 响应式 Web 选配端
│   └── server/              # 图片资源与选配服务
├── contracts/               # 三端共享 Schema、fixture 与 OpenAPI
├── tools/                   # 仓库级验证工具
├── docs/                    # 架构、决策、流程与探针证据
└── package/                 # Git 忽略的本地构建与发布产物
    ├── clients/
    │   ├── ue/
    │   └── web/
    ├── server/
    └── renders/
```

`contracts/` 与 `tools/` 当前位于仓库根目录；请勿使用早期计划中的 `source/contracts/` 或 `source/tools/`。`source/` 只保存可审查源码，`package/` 只保存可重建的本地产物。

## 统一发布

Windows PowerShell 5.1 或更高版本可从仓库根目录执行统一发布入口：

```powershell
.\tools\release.ps1 -Target All
.\tools\release.ps1 -Target UE,ServerWeb -SkipTests
.\tools\release.ps1 -Target ServerWeb -IncludeRenders
.\tools\release.ps1 -Target BakeWeb -Profile debug -Mode shard -Shard 0/4 `
  -Publication sc01-v2 -Input contracts/fixtures/sc01.catalog.draft.v2.json `
  -Output staging/sc01-v2-shard-0
```

- `Target` 支持 `UE`、`ServerWeb`、`BakeWeb`、`All`，也支持逗号或加号分隔的组合。
- `-DryRun` 只打印计划，不运行命令或写入产物；`-SkipTests` 只跳过测试，不跳过构建和
  Bake manifest 校验。
- 正式运行默认要求 Git 工作树干净，使发布清单能唯一对应提交；仅限本地试验时可显式
  使用 `-AllowDirty`，此时清单会记录 `dirty=true`。
- 构建或校验失败时默认保留 staging 供诊断；确认不需要失败证据时可传
  `-CleanFailedStaging`。
- `ServerWeb`、`UE` 和非 `estimate` 的 `BakeWeb` 都先在最终目录同卷的临时目录完成
  构建、校验和清单生成。组合发布必须等待全部目标 prepare 成功后才统一事务晋升；
  任一晋升失败会按逆序恢复所有已替换目标，构建或校验失败不会覆盖旧发布。
- 同一次命令同时包含 `UE` 与 `ServerWeb` 时，Web 只构建一次共享 artifact；两个目标
  复制该 artifact 的 online/embedded 入口，不会各自重新构建。
- `UE` 固定使用官方 UE 5.8 路径（可用 `-EngineRoot` 覆盖），先执行 Web 同源构建和
  UBT `-gather`，再执行 UAT `BuildCookRun`，归档到 `package/clients/ue/`。
- `ServerWeb` 生成 `package/server-web/` 可搬运目录，包含 production Server 依赖、
  contracts、Web 静态文件和 `start-server.ps1`；默认不复制体积较大的
  `package/renders/`，需要随包交付时显式传 `-IncludeRenders`。无 renders 的包启动时
  自动把 `BAKE_ROOT` 指向内置 `contracts/fixtures/bake.valid`，可直接做健康检查和
  示例联调。launcher 将可变的 v2 配置存储写入当前用户
  `%LOCALAPPDATA%\ConfigurationSystem\data\configurations-v2.json`，不会修改安装目录。
- `BakeWeb` 调用现有配置生成器、UE Batch Bake 和 Server manifest 校验。`Mode`
  支持 `exhaustive`、`estimate`、`coverage`、`shard`；`estimate` 只输出规模估算，
  不执行 UE Bake。
- 每个成功晋升的目标根目录都包含 `release-manifest.json`，记录当前 Git commit、
  UTC 生成时间、工作树状态、目标名以及除清单自身外每个发布文件的相对路径和 SHA256。
- `BakeOutput` 不允许指向仓库根目录、源码、契约、工具、文档或 Harness 目录。覆盖
  已存在目录时，目标必须位于 `staging/`、`package/renders/` 下，或包含
  `.configuration-system-bake-output` ownership sentinel；新 Bake 输出会自动写入该
  sentinel。
- Web embedded 部署只接受 `package/` 下的构建源，目标只允许位于 `package/` 下或为
  UE `Content/WebUI`；源与目标不得相同或互为祖先。部署使用目标同级临时目录和备份
  原子替换，失败时恢复旧目标。

先用以下命令检查完整发布计划：

```powershell
.\tools\release.ps1 -Target All -DryRun
node --test tools/release.test.mjs
node --test source/clients/web/scripts/deploy-embedded.node-tests.mjs
```

## 契约验证

安装 Node.js 18 或更高版本后，在仓库根目录执行：

```powershell
node harness/validate.mjs
node tools/validate-contracts.mjs
```

验证器检查 11 个 Schema、车辆模型/动画 sidecar，以及 v1 产品 ID、目录全部选项的
笛卡尔积、全部视角和动态图片期望；同时检查 SC01 v2 草案的多层引用、正反配置、
稳定 `configurationId`/`renderKey` 黄金向量和禁止报价结果。

生成完整 v1 发布配置（不会截取颜色或模板）：

```powershell
node tools/generate-published-configurations.mjs --mode exhaustive
```

v2 不允许直接展开 SC01 约 3.9e20 个组合，支持规模/耗时估算、renderRelevant
选项覆盖和确定性分片：

```powershell
node tools/generate-published-configurations.mjs contracts/fixtures/sc01.catalog.draft.v2.json unused.json sc01-v2 --mode estimate
node tools/generate-published-configurations.mjs contracts/fixtures/sc01.catalog.draft.v2.json staging/sc01-coverage.json sc01-v2 --mode coverage
node tools/generate-published-configurations.mjs contracts/fixtures/sc01.catalog.draft.v2.json staging/sc01-shard-0.json sc01-v2 --mode shard --shard=0/4
node --test tools/generate-published-configurations.test.mjs
```

车辆 sidecar 的独立验证和测试命令：

```powershell
node tools/validate-vehicle-sidecars.mjs
node --test tools/validate-vehicle-sidecars.test.mjs
node --test tools/validate-content-pack.test.mjs
node --test tools/validate-source-assets.test.mjs
node --test tools/validate-sc01-v2.test.mjs
node tools/validate-source-assets.mjs
```

UE 编译、Cook、Development/Shipping 探针命令见 [贡献与验证指南](CONTRIBUTING.md)。

## UE 桌面端

工程入口为 `source/clients/ue/ConfigurationSystem.uproject`，当前基础配置包括：

- UE5.8 C++ Runtime 与 Editor Target。
- Enhanced Input 输入系统。
- Windows DX12 与 Shader Model 6。
- Lumen 全局光照和反射、Nanite、虚拟阴影贴图。
- 禁用静态光照，以实时车辆展示为默认方向。

首次打开前，请确认已安装 UE5.8 和对应的 Visual Studio C++ 工具链。工程文件位于 `source/clients/ue/ConfigurationSystem.uproject`。可生成 Visual Studio 项目文件后编译 `ConfigurationSystemEditor`，也可使用 `CONTRIBUTING.md` 中基于 `Build.bat` 的实际命令。

当前 UE 工程是初始骨架，业务模块、Web 技术栈和 Server 方案见 MVP 实施计划。
