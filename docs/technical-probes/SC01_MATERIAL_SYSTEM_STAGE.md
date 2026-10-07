# SC01 材质体系阶段 3–4

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

## 阶段 4：surface binding 实时事务

`UAutomotiveMaterialBinder` 现直接消费 catalog 的 `vehicleSurfaceBinding`：

- 一个 `surfaceId` 可映射一个或多个稳定命名槽；绑定时保存各槽原材质，清除可选
  selection 时恢复原材质。当前 A5 代理为 Catalog 40 surface 各提供一个唯一可见目标。
- Web bridge 使用 `ApplyConfigurationTransactionJson` 返回强类型 JSON 回执，包含
  `code`、`configurationId`、已应用 `surfaceId`、明确 unsupported `surfaceId` 和
  实际命中的槽名。当前 proxy capability 完整覆盖 40 surface，逐项事务的
  `unsupportedSurfaceIds` 均为空。
- 事务先校验完整配置、binding、运行时槽和全部目标材质，再一次提交状态并更新槽；
  缺 MI/variant 时保持配置身份和当前显示材质不变。
- `variantId` 直接使用阶段 3 物化 MI；材料族/固定色与自定义车漆使用按槽缓存的 MID。
  自定义车漆更新同一 MID 及 1×1 transient 颜色纹理，不生成或保存烘焙资产。
- 阶段 3 生成器同时为被复用的 Substrate 母材质持久化
  `SkeletalMesh` usage，使物化 MI 可用于当前骨骼代理车；不复制或改写材质图。

机器证据：

- [UE5.8 DX12 GUI 槽映射截图](assets/sc01-materials-stage4/mapped-slots.png)
- [GUI 探针事务与实际材质路径](assets/sc01-materials-stage4/gui-report.json)

GUI 报告在真实 Game viewport 中按 Catalog `selectionOrder` 遍历 40 surface；
每项选择不同于当前值的 option，并在支持时附加 material variant 或自定义色，
经 `UAutomotiveMaterialBinder::ApplyTransaction` 验证唯一可见组件、唯一
`A5Proxy_*` 槽、材质前后变化和空 unsupported 集。报告逐项记录
`component/slot/before/after/change`。截图中的车辆仍是内部授权 Audi A5 代理，
不代表正式 SC01 视觉验收。

## 验证

- `ConfigurationSystemEditor Win64 Development`：通过；
- `ConfigurationSystem.Editor.AutomotiveMaterials.MaterializeCatalogVariants`：通过；
- 结果：17 个材料族、352 个 MI、352 个颜色/花纹纹理、错误 0；
- 重复生成：不创建重名资产，原位刷新；
- `ConfigurationSystem.Editor.AutomotiveCatalog`：通过；
- Overview 实时 Lit 与 Path Tracing GPU 出图：完成。
- `ConfigurationSystem.Runtime.AutomotiveMaterials.Binder`：通过，覆盖 variant、
  清除恢复、缺 MI 原子拒绝、40 个唯一可见槽、空 unsupported、自定义车漆 MID 复用。
- Web：88 项 Vitest 通过，生产 build 通过。
- UE5.8.1 DX12 `AutomotiveMaterialGuiProbe`：通过，真实 Game viewport 逐项命中
  40 个唯一可映射槽并确认 40 项材质变化。
