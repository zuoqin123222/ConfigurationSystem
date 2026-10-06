# SC01 材质体系阶段 3

## 结论

阶段 3 已在 UE 5.8.1 中审计仓库自带
`/Game/SubstrateMaterials/Maps/Overview`，为 catalog 的 17 个材料族建立显式母材质映射，
并物化 352 个 `UMaterialInstanceConstant`。实现不创建 `M_SC01_*` 母材质，也不复制
`SubstrateMaterials` 资产；所有 Parent 路径均位于 `/Game/SubstrateMaterials/`。

机器证据：

- [17 族映射和数量审计](assets/sc01-materials-stage3/material-audit.json)
- [Overview 实时 Lit](assets/sc01-materials-stage3/overview-realtime.png)
- [Overview Path Tracing](assets/sc01-materials-stage3/overview-pathtracing.png)

## 物化规则

- 输出：`/Game/SC01/Materials/Variants/<materialFamilyId>/MI_SC01_*`
- 颜色纹理：`/Game/SC01/Materials/VariantTextures/<materialFamilyId>/T_SC01_*`
- `DA_SC01MaterialLibrary` 索引 17 个 Parent 和 352 个 MI。
- 336 个纯色色卡使用 catalog `ui.sortColorHex` 生成 1×1 sRGB 纹理，写入
  `Diffuse Color Map` 或 `Color Map`，保留母材质的法线、粗糙度和绒毛响应。
- 16 个织物羊毛直接使用仓库内 PDF 缩略图的 PNG 派生源，写入 `Color Map`；
  方向固定为缩略图方向（`Rotation=0`），按花型分组设置尺度：
  `SQUARES=1.25`、`PEPITA=1.15`、`SOLM=1.0`、`MADRAS=0.9`、
  `TARTAN=0.85`、`FLANELL STREIFEN=0.8`。
- 每个 MI 写入 `SC01.VariantId`、`SC01.MaterialFamilyId`、
  `SC01.ThumbnailUrl` 元数据；羊毛额外写入尺度与方向。

## 17 个材料族 Parent

完整对象路径见 JSON 审计。选择原则如下：

| 材料族 | Overview 复用方向 |
|---|---|
| paint | metallic glint paint |
| aluminum-alloy | aluminum |
| magnesium-alloy | dark matte titanium 近似金属基线 |
| carbon-fiber | carbon fiber OPBR |
| metal | generic metal |
| ppg | dielectric paint |
| ultrasuede | charcoal suede |
| alcantara | black suede |
| leather | leather OPBR |
| microfiber | plastic leather OPBR |
| eva | rubber OPBR |
| woven-fabric | fabric OPBR |
| woven-wool | fabric weave OPBR |
| felt | fabric velvet OPBR |
| spray | dielectric paint |
| carpet | carpet |
| rubber | rubber OPBR |

`magnesium-alloy` 当前使用 Overview 中最接近的深色哑光钛作为物理近似；
该映射是明确的审计选择，不宣称其化学成分等同镁合金。

## UE 5.8 视觉审计

使用同一 Overview 关卡、DX12 和 RTX 5080 对照：

- Lit：成功出图，但原样地图在 game viewport 中显示 SkyDome coverage 警告；
- Path Tracing：64 spp，关闭 progress overlay，并将曝光补偿调到 `+4 EV` 后出图；
- 两张图均保留原始地图构图，不把 Overview 样例冒充 SC01 正式车辆或最终内饰验收。

本阶段完成的是母材质选择、MI 物化和 Overview 对照。正式 SC01 模型尚未到位，
因此不能对真实 UV、曲率、座椅花纹物理尺寸或整车 Path Tracing 效果作最终结论。

## 验证

- `ConfigurationSystemEditor Win64 Development`：通过；
- `ConfigurationSystem.Editor.AutomotiveMaterials.MaterializeCatalogVariants`：通过；
- 结果：17 个材料族、352 个 MI、352 个颜色/花纹纹理、错误 0；
- 重复生成：不创建重名资产，原位刷新；
- `ConfigurationSystem.Editor.AutomotiveCatalog`：通过；
- Overview 实时 Lit 与 Path Tracing GPU 出图：完成。
