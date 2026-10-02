# 批次 0 共享契约

本目录冻结 MVP 三端共享的数据形状、稳定 ID、配置键、发布组合与 Server API。契约数据只描述产品配置；UE 环境、交互镜头、车门、灯光、轮胎等运行时状态不进入配置键。

## 文件

- `schemas/catalog.schema.json`：车型、分区、选项、模板、交互镜头与渲染视角。
- `schemas/published-configurations.schema.json`：Web 可达的 16 个完整配置。
- `schemas/bake-manifest.schema.json`：烘焙器、Alpha 处理策略，以及图片归一化结果、尺寸、哈希与状态。
- `schemas/vehicle-model-sidecar.schema.json`：DCC 模型交付的坐标、FBX、层级、Pivot、材质槽、LOD、授权与哈希。
- `schemas/vehicle-animation-sidecar.schema.json`：动画 clip、目标节点、可逆性、模型引用、授权与动画 FBX 哈希。
- `schemas/content-pack-manifest.schema.json`：Runtime 内容包版本、兼容目标、白名单挂载点、pak 大小/SHA-256 与 PrimaryAssetId。
- `fixtures/catalog.mvp.json`：1 台车、4 个分区、每区 2 项、2 个模板。
- `fixtures/published-configurations.mvp.json`：2×2×2×2 笛卡尔积和 4 个固定视角。
- `fixtures/vehicle-*.valid.json`：可通过车辆 sidecar 契约的模型与动画样例。
- `fixtures/vehicle-*.invalid.json`：验证器必须拒绝的负向样例。
- `fixtures/content-pack.valid.json` / `content-pack.invalid.json`：内容包 manifest 正反样例。
- `openapi.yaml`：health、catalog、resolve 与静态 render 接口。

## ID 与配置键

所有 ID 使用小写 kebab-case，跨端只传稳定 ID，不按中文名、数组下标或 UE 资产路径推断：

```text
vehicleId: demo-car
partId: paint | wheel | interior | frame
optionId: <partId>-<name>
templateId: sport | luxury
uePrimaryAssetId: CarMaterialOption:<optionId>
renderViewId: front | front-left | side | rear-right
```

配置键必须按固定顺序连接选项 ID：

```text
paint__wheel__interior__frame
paint-red__wheel-sport__interior-dark__frame-black
```

不得按对象遍历结果、UI 顺序或本地化名称生成配置键。模板只是四个分区的预设，不产生额外组合。

## 价格与图片

金额单位为人民币分，且必须为整数：

```text
totalPriceMinor = basePriceMinor + 四个已选 option.priceDeltaMinor
```

MVP 发布集固定为 16 个唯一配置和 4 个视角，共 64 个图片期望。规范路径为：

```text
renders/<publicationVersion>/<vehicleId>/<configurationKey>/<renderViewId>.png
```

### P0-3 渲染与 Alpha 处理

每份 bake manifest 必须在顶层记录可复现的渲染和 Alpha 处理参数：

- `renderer.engineVersion`：实际使用的渲染引擎版本。
- `renderer.mode`：固定为 `path-tracing`。
- `renderer.samplesPerPixel`：每像素采样数，必须为正整数。
- `alphaProcessing.alphaMode`：固定为 `straight`，发布图片不得使用预乘 Alpha。
- `alphaProcessing.autoDetectCoverageInversion`：是否自动检测 coverage 反转。
- `alphaProcessing.clearTransparentRgb`：是否清理完全透明像素的 RGB。
- `alphaProcessing.glowPolicy`：发光像素恢复策略，只能为 `synthetic-alpha` 或 `separate-layer`。

每个 `status: ready` 的 render 除既有 PNG、sRGB、straight Alpha 元数据外，还必须记录 `width`、`height`、`sha256`、`coverageInverted`、`normalizationRequired` 和非负整数 `glowRecoveredPixels`。后三项分别说明是否检测到 coverage 反转、是否执行过归一化，以及恢复的发光像素数量。

## 版本与兼容

- `schemaVersion` 表示 JSON 结构版本；破坏性字段变更必须升级主版本。
- `catalogVersion` 表示目录内容版本；选项、价格或模板变化时升级。
- `publicationVersion` 标识一批完整且不可变的 64 张发布图片。
- 消费端必须拒绝未知主版本；版本不一致时 Server 返回 `409`，不得猜测映射。
- 新增可选字段属于向后兼容变更；删除字段、改名、改变含义或 ID 属于破坏性变更。
- 已发布 ID 和版本目录不可原地复用；需要调整时发布新版本。

## 验证

无需安装依赖，使用 Node.js 18 或更高版本：

```powershell
node tools/validate-contracts.mjs
```

验证器检查 6 个 Schema 文件是有效 JSON，检查 bake manifest 的 P0-3 结构、content-pack manifest 正反样例、车辆模型与动画 sidecar 的 Schema 及跨文件语义，并检查 ID 与模板引用、UE PrimaryAssetId、预览图、价格、16 个唯一笛卡尔组合、canonical key、4 个视角和 64 个图片期望。

车辆 sidecar 也可单独验证：

```powershell
node tools/validate-vehicle-sidecars.mjs
node --test tools/validate-vehicle-sidecars.test.mjs
node tools/validate-content-pack.mjs contracts/fixtures/content-pack.valid.json
node --test tools/validate-content-pack.test.mjs
```
