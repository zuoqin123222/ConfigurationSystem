# SC01 材质 Bake R2 验证

验证日期：2026-10-07  
分支：`feature/full-material-configurator`  
Bake 源提交：`5f736688aa051f1e67fb4eb4bcb6e13b08e8d0f2`  
Publication：`sc01-materials-20261007-r2`

## 结论

该记录对应 `sc01-draft-20260121` 的 38-surface 历史目录。后续 main 已升级为
`sc01-draft-20261007`、40 surface、482 个 coverage 配置和 1928 个任务，因此 R2
不得作为新目录的最终 publication；本文件只保留旧批次的可复验证据。

`BakeWeb` 统一发布入口完成 Shipping coverage Bake、Server manifest 校验和原子晋升。
R2 共包含 474 个覆盖配置、4 个标准视角和 1896 张 Path Tracing PNG；全部任务为
`ready`，没有缺图、失败任务或 SHA-256 不一致。

该批次仍使用授权 Audi A5 代理车辆，只验证材质系统、配置身份、出图和 Web 分发链路，
不代表正式 SC01 造型或 38 个 surface 均已有独立代理几何。

## Bake 参数

| 项目 | 结果 |
|---|---:|
| Engine | UE 5.8.1 |
| Renderer | Path Tracing |
| Samples | 512 spp |
| Denoiser | 开启 |
| 原始尺寸 | 2044 × 1328 |
| 配置数 | 474 |
| 视角数 | 4 |
| Render 数 | 1896 |
| 自定义颜色 | 排除 3 个 `color-picker` |

视角集合固定为 `front`、`front-left`、`side`、`rear-right`。

## Manifest 复核

统一发布完成后，另行对最终晋升目录执行独立复核：

- manifest：1896/1896 `ready`，0 `failed`；
- 474 个唯一 `renderKey`；
- 1896 个唯一规范路径；
- 1896 个唯一图片 SHA-256；
- 1896 张图片均存在，且实际 SHA-256 与 manifest 一致；
- 所有图片均为 2044 × 1328、PNG、sRGB、straight Alpha；
- 图片正文合计 967,979,950 字节；
- `published-configurations.json` 声明 `expectedRenderCount=1896`；
- `release-manifest.json` 存在；
- Server `validate:bake` 对 plan、manifest 与图片正文重新校验通过。

## 失败修复

首轮全量 Bake 在 160 张后终止，剩余 1736 项统一报告“等待 Bake 材质 Shader 编译
超时”。根因是 Shader 等待使用整批运行开始时间，而不是当前 Shader 编译批次的开始
时间。

修复后：

- Shader 编译按每批独立计时；
- 编译完成时重置当前渲染任务计时；
- `ConfigurationSystem.Runtime.BatchBake` 7/7 自动化通过；
- 原失败边界 `shard 40/474` canary 为 4/4 `ready`；
- R2 全量运行越过第 160 张并最终完成 1896/1896。

## Web 自动化

`source/clients/web`：

- Vitest：7 个测试文件、89/89 测试通过；
- TypeScript 与 Vite production build 通过；
- 同一次构建部署 393 个相同文件到在线 Web 和 UE `Content/WebUI`；
- 生产入口脚本为 `assets/index-CbKBNjz8.js`。

## 浏览器回归

使用当前 Server、当前 production Web build 和 R2 Bake 根进行真实浏览器验证：

- 干净首次访问使用 catalog 默认配置，命中 R2 默认 `renderKey`；
- 默认车辆图为 2044 × 1328 v2 PNG；
- `front-left` 切换到 `side` 后 URL 和图片视角同步变化；
- 红色切换银色车漆后命中新 R2 `renderKey`；
- 方向盘从奥司维切换到 `Alcantara 9036` 后命中新 R2 `renderKey`；
- 自定义车漆保留上一张已发布图片，并显示“v2 渲染图片不存在”；自定义参数仍写入本地
  状态，符合“自定义颜色暂不 Bake、后续实时调色”的边界；
- 未发布的历史草稿组合显示明确缺图提示，不错误回退到旧 v1 代理图；
- 总览和分享入口可用；
- 1022 × 688 浏览器视口下 `scrollWidth == clientWidth == 1022`；
- 全新标签页控制台无错误。

## 验证入口

```powershell
.\tools\release.ps1 -Target BakeWeb -DryRun -Profile shipping `
  -Mode coverage -Publication sc01-materials-20261007-r2 `
  -Output staging\sc01-materials-20261007-r2

.\tools\release.ps1 -Target BakeWeb -Profile shipping `
  -Mode coverage -Publication sc01-materials-20261007-r2 `
  -Output staging\sc01-materials-20261007-r2

npm --prefix source/server run validate:bake -- `
  staging/sc01-materials-20261007-r2/bake-manifest.json `
  staging/sc01-materials-20261007-r2 `
  staging/sc01-materials-20261007-r2/published-configurations.json

npm --prefix source/clients/web test
npm --prefix source/clients/web run build
```
