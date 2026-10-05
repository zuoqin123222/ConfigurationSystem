# UE、FBX、资产与打包规则

适用于 `source/clients/ue/` 及所有车辆资产、Maya、Cook、Shipping 和 Bake 任务。

## 必读路由

| 任务 | 必读文件 |
|---|---|
| FBX 制作、骨骼、动画、LOD、材质槽 | `docs/DCC_VEHICLE_MODELING_EXPORT_GUIDE.md` |
| 车辆层级、Pivot、ComponentTag、导入验收 | `docs/VEHICLE_ASSET_REQUIREMENTS.md` |
| Maya 规范化 | `tools/maya/README.md` |
| 外部资产与授权 | `docs/REFERENCE_ASSET_POLICY.md` |
| 内容包与热更新 | `docs/CONTENT_PACK_RUNTIME.md` |
| Cook/Development/Shipping 边界 | `docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md` |
| Bake 与发布 | `docs/technical-probes/BAKE_RELEASE_VALIDATION.md` |

完整参数以这些文件为准，不从对话记忆重写命令。

## UE 目录

| 目录 | 用途 | 规则 |
|---|---|---|
| `Source/ConfigurationSystem/` | Runtime C++ | 不依赖 `UnrealEd`、`ToolMenus` 等 Editor-only 模块 |
| `Source/ConfigurationSystemEditor/` | 导入、资产生成、审计 | 创建和保存 `.uasset`、地图的代码只放这里 |
| `Content/SC01/` | SC01 Catalog、材料库和正式项目资产 | 稳定路径；变更需验证 Primary Asset 与 Cook |
| `Content/Maps/` | 展厅、探针和灯光地图 | 新地图需同步默认入口、`MapsToCook` 和测试 |
| `Content/Library/` | 项目共享环境和材质资源 | 不放车型专属源文件 |
| `Content/Configurator/_ImportStaging/` | 管理员导入暂存 | 不等于正式发布；通过验收后才能晋升 |
| `Content/Configurator/AuthorizedAudiA5/` | 内部授权代理车 | 不是正式 SC01；授权和 Git/LFS 边界按政策执行 |
| `SourceAssets/` | FBX、DCC 源文件、sidecar、授权记录 | 不是 UE Content；先过来源、授权和哈希门禁 |
| `Config/` | 引擎、Game、输入和窗口配置 | Asset Manager 与 Cook 规则必须进入受控配置 |
| `Binaries/`、`Intermediate/`、`Saved/` | UE 生成物 | 不提交、不作为源码事实 |
| 根 `package/clients/ue/` | Windows 发布物 | 被 Git 忽略；仅保存验证后的可重建产物 |

不得在文件管理器中直接移动或改名 `.uasset` 来替代引擎重定向和引用修复。真实资产状态通过 UE5.8 Editor 检查。

## FBX 生产

固定流程：

```text
确认授权和来源
→ 保留不可变输入并计算字节数、SHA-256
→ DCC 制作/修复
→ Maya 2025 规范化
→ 导出 FBX 2020.2 + sidecar
→ sidecar 和契约验证
→ 管理员预检与暂存导入
→ UE 层级、材质、动画和视觉验收
→ 批准后进入正式资产或 Provider
```

核心约束：

- 厘米；`+X` 向前、`+Y` 向右、`+Z` 向上；根节点位于原点，车轮落地。
- DCC 节点使用 `Vehicle_Root` 风格；UE ComponentTag 使用 `Vehicle.Root` 风格。
- 新交付优先使用单个骨骼车辆 FBX，包含 SkeletalMesh、Skeleton 和一条完整 AnimSequence。
- 动画统一 30 fps；片段由 sidecar 帧范围定义，非目标骨骼在片段开始前复位。
- Pivot 位于真实铰链或轮心；卡钳使用静止骨骼，不随车轮滚动。
- 材质槽使用稳定语义名称，禁止 `MaterialSlot_0` 一类无语义名称。
- LOD 的材质槽集合保持一致；不得用导入旋转补偿错误坐标。
- 未知节点映射、授权或价格信息不得猜测；停止并保留失败证据。

验证入口：

```powershell
node tools/validate-vehicle-sidecars.mjs
node --test tools/validate-vehicle-sidecars.test.mjs
node tools/validate-source-assets.mjs
node tools/validate-contracts.mjs
```

JSON 通过不代表 FBX 内容、Pivot、穿模、材质和授权真实性通过，必须补做 UE GUI 验收。

## 打包流程

产品包默认地图为 `/Game/Maps/L_ConfigShowroom`；`L_ConfigProbe` 只用于打包边界探针。`DefaultGame.ini` 必须显式维护 `MapsToCook`、Primary Asset 扫描和确需动态加载的 `DirectoriesToAlwaysCook`。

每次打包按顺序执行：

1. 检查 Git 状态、当前提交和工作树。
2. 运行 Harness、契约及受影响的 Web/Server/UE 自动化。
3. 编译 `ConfigurationSystemEditor Win64 Development`。
4. 使用 UAT `BuildCookRun` 完成 Build/Cook/Stage/Pak/IoStore/Archive。
5. 检查 UAT 退出码、Cook 警告、归档文件和 Editor 模块泄漏。
6. 启动归档后的 `ConfigurationSystem.exe`，验证窗口、地图、车辆、Web 面板和受影响功能。
7. 大版本或视觉变更重新生成 Web build 和 Bake 图片。
8. 校验 manifest、数量、尺寸、色彩、Alpha/RGBA 和 SHA-256 后原子发布。

Shipping 不得包含 `ConfigurationSystemEditor`。Editor 编译通过不代表 Cook 或 Shipping 通过；命令行探针通过也不代表 GUI 视觉通过。

## 资产变更验收

- 层级、Pivot、骨骼、材质槽、ComponentTag、Mobility：UE Editor 检查并记录路径和字段。
- 车门、机盖、后备箱、轮胎与卡钳：验证方向、中途反向和重复动作。
- 车漆、玻璃、灯具、内饰：同时检查 Lit 与 Path Tracing。
- 相机、构图、穿模、Bloom、CEF 和输入：真实 GUI/Shipping 验收。
- 任何地图、Primary Asset、软引用或动态加载路径变化：验证 Cook 后仍可加载。
