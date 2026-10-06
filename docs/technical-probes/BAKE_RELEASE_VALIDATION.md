# Bake 与发布阶段验收

验收日期：2026-10-02  
代码基线：`cf4ce0f`  
发布版本：`mvp-v1`

## 验收范围

本阶段验证 UE 批量出图、不可变发布门禁、Server 资源分发、Web 图片切换，以及当前
代码基线的 Windows Shipping 打包。`mvp-v1` 使用
`Configurator.Resource.Temporary` 占位车辆，仅用于流水线与交互验收，不代表最终车辆
资产或最终产品画质。

## 当前 Bake 运行参数

发布配置不再写死为 16 个配置或 64 个任务。先从目录生成全部分区选项的笛卡尔积，
再乘以目录声明的全部 RenderView：

```powershell
node tools/generate-published-configurations.mjs `
  contracts/fixtures/catalog.mvp.json `
  contracts/fixtures/published-configurations.mvp.json `
  mvp-v1
```

生成器不会截取少量车漆或模板；任一分区无选项、目录无视角时会直接失败。UE 运行时还会
校验 `expectedRenderCount` 与实际任务数完全一致，并校验每个配置具有完整视角集合。

2K 桌面基准为 2560 × 1440：顶部 76 px、右侧配置栏 480 px、左舞台四周
18 px，因此实际可见左舞台是 **2044 × 1328（511:332）**，不是 16:9。Batch Bake
按这个实际舞台比例提供两档原始输出：

| 档位 | 分辨率 | Path Tracing SPP | 默认场景 |
| --- | ---: | ---: | --- |
| `debug` | 1022 × 664 | 64 | Editor/Development 等比半尺寸快速验证 |
| `shipping` | 2044 × 1328 | 512 | 正式 2K 左舞台原尺寸发布 |

命令行参数：

```text
-ConfigurationBakeProfile=debug|shipping
-ConfigurationBakePathTracing=true|false
-ConfigurationBakeSamples=<1..4096>
-ConfigurationBakeWidth=<640..7680>
-ConfigurationBakeHeight=<360..4320>
```

自定义宽高必须为 511:332。未显式指定档位时，Shipping 构建采用 `shipping`，其他构建采用
`debug`。Path Tracing 默认开启，同时启用 Denoiser、最高后处理抗锯齿质量和 100%
Screen Percentage；关闭后使用 Lit，并等待稳定帧再回读。PNG 回读显式执行线性到 sRGB
转换，manifest 记录渲染模式、采样数、原始宽高和 Denoiser 状态。Web 车辆图片使用
容器 `width: 100%`、`height: 100%` 与 `object-fit: contain`，不再使用 900/520 px
上限，也不裁切或二次放大填充。

### v1/v2 发布配置生成策略

- v1 使用 `--mode exhaustive`，完整枚举有限笛卡尔积。
- v2 默认只执行 `estimate`。SC01 当前完整空间为
  `391820820480000000000`（约 3.9e20）个配置，生成器明确拒绝 `exhaustive`。
- v2 `coverage` 从默认/首选基线生成去重集合，保证每个可生成 Bake 的
  `renderRelevant` 选项以及每个可被已定价色卡选项消费的 `materialVariant` 至少出现
  一次。`ui.control=color-picker` 的自定义颜色选项继续参与配置、UI 和 UE 实时材质，
  但不生成有限的预烘焙任务；该规则不排除 `swatch` 等普通 paint 色卡。基线严格复用
  `defaultSelections`，不会给无默认项的可选 surface 强行选择第一个选项，确保 Web
  初始状态可以直接命中渲染。当前 SC01 覆盖 151 个 Bake 选项、排除 3 个
  `color-picker`，并覆盖 352 个可用材料色卡，去重后得到 474 个配置；按 4 个视角为
  1896 个渲染任务。
- v2 `shard` 对 coverage 集合做确定性取模分片，`--shard=0/4` 到 `3/4` 合并后不重不漏。
- v2 coverage/shard 输出固定声明四个 `renderViewIds`，并对每项写出
  `configurationKey = renderKey`。`FConfigurationBakePlan::Load` 同时读取通用
  `selections/customizations`；执行时 v1 继续映射旧四分区，v2 则通过
  `UCarConfiguratorSubsystem` 取得 `UAutomotiveConfigurationState` 并调用
  `ApplyTransaction`，由状态广播驱动已绑定的材质 Binder。
- 输出及控制台同时记录完整规模、Bake 选项/排除的 `color-picker`/材料色卡覆盖数、
  渲染任务数和按 `--seconds-per-render` 计算的预计耗时。Server 校验 Bake manifest
  时还必须与同次生成的 published plan 对照任务身份和视角集合。

```powershell
node tools/generate-published-configurations.mjs `
  contracts/fixtures/sc01.catalog.draft.v2.json unused.json sc01-v2 `
  --mode estimate --views 4 --seconds-per-render 30

node tools/generate-published-configurations.mjs `
  contracts/fixtures/sc01.catalog.draft.v2.json staging/sc01-coverage.json sc01-v2 `
  --mode coverage --views 4 --seconds-per-render 30

node tools/generate-published-configurations.mjs `
  contracts/fixtures/sc01.catalog.draft.v2.json staging/sc01-shard-0.json sc01-v2 `
  --mode shard --shard=0/4 --views 4 --seconds-per-render 30
```

## 自动化结果

- 2026-10-05 在提交 `fe4834e` 上重新执行 Windows Shipping
  Build/Cook/Stage/Pak/Archive，AutomationTool `ExitCode=0`，归档总大小
  1,267,238,657 字节。归档 Shipping 二进制与构建输出 SHA-256 一致，确认包含
  `reset` 顶栏动作白名单，同时保持车辆资产与 `origin/main` 一致；真实启动后窗口
  标题为“汽车选配系统”且进程可响应。
- 2026-10-05 本分支验证：契约聚合校验、Web 68/68、Server 45/45、UE BatchBake
  6/6 通过，`ConfigurationSystemEditor Win64 Development` 编译成功。
- 2026-10-05 R2 发布候选：`sc01-web-shipping-20261005-r2` 使用 2044 × 1328
  Realtime 高质量档完成 477 套配置、1908 张 RGBA PNG；首轮 64 张 Woven Wool
  Shader 首次编译超时后独立补跑并原子合并，最终 1908/1908 `ready`。
- Server 对 R2 manifest 逐文件校验通过；1908 条路径唯一、1908 个 SHA-256 唯一，
  图片合计 1,173,513,611 字节。真实 `/api/v2/renders/resolve` 返回 R2 图片，
  默认配置与银色车漆均能命中 2044 × 1328 PNG。
- 真实 Web 页面验证舞台圆角为 20 px，车辆图以 `object-fit: contain` 填满舞台；
  UE controls 页面 `html`、`body`、`#root`、`.controls-view` 的横纵 overflow
  均为 `hidden`，控制条 `scrollWidth == clientWidth` 且
  `scrollHeight == clientHeight`。
- 当前自动化浏览器固定为 511 × 764 视口，无法留存真实 2560 × 1440 桌面截图；
  已分别验证 R2 原图自然尺寸 2044 × 1328、舞台比例、20 px 圆角和无 overflow。
- 使用 `-RenderOffscreen -ForceRes` 实跑早期 469 套 coverage 的 `0/469` canary，
  Lit Debug 模式成功输出 1 个配置 × 4 个视角，manifest 全部为 `ready`，原图均为
  1022 × 664 RGBA。该批次因错误地为无默认项 surface 强选首项而废弃，不属于 R2。
- 合并边界复核后，功能分支不再修改骨骼车辆 `.uasset`、`ConfiguratorVehicleActor`
  或其材质覆盖映射；这些车辆所有权文件与 `origin/main` 完全一致。主分支本地原有但
  被 `.gitignore` 排除的 `AuthorizedAudiA5` 分件和材质按原路径通过 Git LFS 纳入，
  并加入 AlwaysCook；此前迁移到 `VehicleProxy` 的实验材质及覆盖逻辑已移除。
- Batch 新增材质预检：可见骨骼车辆任一槽为空、使用 `DefaultMaterial` 或
  `WorldGridMaterial` 时，将全部任务写为 `failed` 且不输出 PNG；交互式 Path Tracing
  RTPSO 预热在 Batch 模式下跳过，避免 manifest 完成后的关机竞态崩溃。
- V2 Binder 已优先绑定可见骨骼车辆的 `CS_Validation_Paint` 与
  `CS_Validation_Interior` 命名槽。标准红/银车漆和两个内饰色卡 canary 已确认
  产生不同视觉结果；其他 surface 尚无独立骨骼槽，1908 张覆盖图不代表 38 个
  surface 都已具备独立视觉变化。
- UE Batch Bake：16 个 canonical configuration × 4 个 RenderView，共 64 个任务。
- Server：19 个测试全部通过。
- Web：18 个测试全部通过，Vite production build 成功。
- Bake manifest：64 个 `ready` RGBA PNG 全部通过尺寸、路径和 SHA-256 校验。
- HTTP 分发：64/64 PNG 可由 Server 返回。
- UE Shipping：Build/Cook/Stage/Pak/Archive 成功，AutomationTool ExitCode 为 0。

## GUI 回归

### Web

- 桌面布局验证四个视角：`front`、`front-left`、`side`、`rear-right`。
- 验证默认配置、银色车漆配置和豪华模板配置，图片 URL 均来自
  `/assets/renders/mvp-v1/<vehicle>/<canonical-key>/<view>.png`。
- 489 px 窄视口无横向溢出，展厅与选配面板按移动布局纵向排列。
- 故障注入时停止 Server，再切换到未缓存配置；选中状态立即更新，但旧图继续显示，
  没有空白闪烁。Server 恢复后重新触发选择，新图加载成功。

移动视口证据：

![Web mobile 489px](assets/bake-release/web-mobile-489px.jpg)

### UE Shipping

归档后的 `ConfigurationSystem.exe` 启动后产生可响应窗口，标题为“汽车选配系统”，
窗口尺寸为 1296 × 759。等待渲染稳定后，展厅、占位车辆、配置面板、模板、体验控制和
实时模式均可见。

![UE Shipping GUI](assets/bake-release/ue-shipping-gui.png)

本次主分支车辆保护后的 Shipping 实机画面：

![Shipping main A5](assets/bake-release/shipping-main-a5.png)

## 交付位置

- UE Windows Shipping：`package/clients/ue/Windows`
- Web production build：`package/clients/web`
- 不可变 Render 发布：`package/renders/mvp-v1`
- SC01 Web R2 Render 发布候选：`package/renders-v2-r2`
- Server 编译产物：`source/server/dist`

关键文件：

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| `package/clients/ue/Windows/ConfigurationSystem.exe` | 172032 | `bceb51f8f21c315ed663e62bd87e9eb75b1595534fbe5af5be9f049462ddacd5` |
| `package/clients/ue/Windows/ConfigurationSystem/Binaries/Win64/ConfigurationSystem-Win64-Shipping.exe` | 166851584 | `a64e610c0f224ee7b0c3d8fad9584aee48eb7d1abf41616d2cf1c37380c0e6d4` |
| `package/clients/ue/Windows/ConfigurationSystem/Content/Paks/ConfigurationSystem-Windows.pak` | 11379423 | `9edc2730a8d9174a9c208d43627a5840cd1f83b274e2790474c8a33016f930fc` |
| `package/clients/ue/Windows/ConfigurationSystem/Content/Paks/ConfigurationSystem-Windows.utoc` | 315243 | `53c42ef67683152f2e2a070c1074c2d763f850730042afc82c6a06ce65f131c5` |
| `package/clients/ue/Windows/ConfigurationSystem/Content/Paks/ConfigurationSystem-Windows.ucas` | 431325888 | `ac031f08f522c754d287dca1422e21cbefd114f8bbd9123e31ed70c353a6ebdd` |
| `package/clients/web/index.html` | 471 | `eab368f98568da480e39ae196631bce5c9a029c8f3aadf457f68a3259b28dbe5c` |
| `package/renders/mvp-v1/bake-manifest.json` | 37092 | `80bbd1b64790bd37f252c443e6c0e805662251b9a3aa468573494e68de7d293e` |

64 张 PNG 合计 19,774,171 字节。

## 发布边界

- `mvp-v1` 不可覆盖；正式车辆接入后必须发布新版本目录。
- R2 当前使用的仍是明确标记的代理 Audi A5 骨骼车辆，不得作为正式 SC01 造型资产发布。
- `package/renders-v2-r2` 为约 1.17 GB 的不可变图片候选并受 Git 忽略；源码分支不承载
  图片正文，正式上线前需同步到 Render 存储并保持 publicationVersion、manifest、
  相对路径与 SHA-256 不变。
- 历史 `mvp-v1` 验收图片仍是 640 × 360 的流水线证据，不代表当前默认输出参数。
- 正式发布前必须重新检查真实车辆层级、Pivot、材质槽、玻璃、穿模、动作和构图，并
  用 `shipping` 档位重新生成全部配置与视角。
- Runtime 仅接收通过 manifest、版本、路径、大小和 SHA-256 校验的标准 `.pak`。
