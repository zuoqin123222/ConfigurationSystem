# SC01 材质 Bake R3 最终验证

验证日期：2026-10-07  
分支：`feature/full-material-configurator`  
源提交：`b4a79cba9bc3f27b7e6d10f765ad12fdfd5f4cf7`  
Catalog：`sc01-draft-20261007`  
Publication：`sc01-materials-20261007-r3`

## 结论

R3 基于最新 `origin/main` 的 40-surface、171-option 目录重新生成，不复用旧 R2
publication。统一 `BakeWeb` 入口完成 Shipping Path Tracing Bake、Server manifest
校验、release manifest 生成与原子晋升。

R3 共包含 482 个 coverage 配置、4 个标准视角和 1928 张 PNG，全部任务为 `ready`。
自定义颜色继续参与配置与 UE 实时材质，但 5 个 `color-picker` 不进入有限预烘焙集。

## Git 基线

- Bake 前 HEAD 与工作树冻结；
- R3 源提交：`b4a79cba9bc3f27b7e6d10f765ad12fdfd5f4cf7`；
- 功能分支已 rebase 到 `origin/main/eef0013`；
- Bake 完成后再次 fetch，功能分支相对 main 为 0 落后、8 领先；
- Bake 期间 HEAD 与工作树未变化。

## Bake 参数

| 项目 | 结果 |
|---|---:|
| Engine | UE 5.8.1 |
| Renderer | Path Tracing |
| Samples | 512 spp |
| Denoiser | 开启 |
| 原始尺寸 | 2044 × 1328 |
| Coverage 配置 | 482 |
| Bake 选项 | 165 |
| 排除的 `color-picker` | 5 |
| Material variant | 352 |
| 视角 | 4 |
| Render | 1928 |

视角集合为 `front`、`front-left`、`side`、`rear-right`。

## Manifest 与图片

统一发布完成后，对最终晋升目录执行了第二次独立校验：

- manifest：1928/1928 `ready`，0 `failed`；
- 482 个唯一 `renderKey`；
- 1928 个唯一规范路径；
- 1928 个唯一图片 SHA-256；
- 1928 张图片均存在；
- 实际图片 SHA-256 与 manifest 全部一致；
- 0 个 metadata 异常；
- 所有图片均为 2044 × 1328、PNG、sRGB、straight Alpha；
- 图片正文合计 981,977,367 字节；
- `published-configurations.json` 声明 `expectedRenderCount=1928`；
- `release-manifest.json` 存在；
- Server `validate:bake` 对 plan、manifest 和图片正文校验通过。

## 自动化验证

- Harness：通过；
- 契约聚合：通过；
- Tools：79/79；
- Server：49/49；
- Web：7 个测试文件、96/96；
- Web TypeScript 与 Vite production build：通过；
- 在线 Web 与 UE `Content/WebUI`：393 个文件同次双部署；
- UE `ConfigurationSystemEditor Win64 Development`：编译通过；
- `ConfigurationSystem.Editor.AutomotiveCatalog.GenerateCatalogPrimaryAsset`：通过；
- `ConfigurationSystem.Editor.AutomotiveMaterials.MaterializeCatalogVariants`：通过；
- `ConfigurationSystem.Runtime.AutomotiveMaterials.Binder`：通过；
- `ConfigurationSystem.Editor.AdminImport.SurfaceBindingSlotLodAudit`：通过；
- Shader 独立等待窗口 canary `shard 40/482`：4/4 `ready`。

## 浏览器回归

使用当前 production Web build、当前 Server 和 R3 Bake 根，在独立端口执行真实浏览器
回归：

- 页面加载 `sc01-draft-20261007`；
- Catalog 为 40 surface、171 options；
- 入口脚本为当前 build 的 `assets/index-D8xRbju1.js`；
- 默认配置命中 R3 v2 `front-left` 图片；
- 默认图自然尺寸为 2044 × 1328；
- 切换到 `side` 后 URL 与视角同步；
- 红色切换银色车漆后命中新 R3 `renderKey`；
- 新目录中的头枕刺绣、门中板刺绣、铭牌和脚垫入口可见；
- 选择“赛车版脚垫”后命中新 R3 `renderKey`；
- 自定义车漆不请求预烘焙图，保留上一张发布图并写入实时调色参数；
- 1022 × 688 视口下 `scrollWidth == clientWidth == 1022`；
- 浏览器控制台无错误。

## 资产边界

当前展示车辆仍是明确标记的授权 Audi A5 代理车。Catalog 已完整声明 40 surface，
但代理骨骼资产当前只有 `CS_Validation_Paint` 与 `CS_Validation_Interior` 两个生产场景
可见槽。R3 证明配置、材质、Bake 和 Web 分发链路完整，不代表正式 SC01 几何或
40 个 surface 已在代理车上全部独立可见。

