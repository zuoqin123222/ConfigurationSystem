# 参考资产政策

状态：原子阶段已实现。适用于 `source/clients/ue/SourceAssets/` 中供审查、重建和追溯使用的外部参考资产。

## 1. 目录边界

`SourceAssets` 只保存经过授权、可追溯、可由工具重新处理的源侧参考文件，不是 Unreal Content 目录，也不是构建产物目录。

允许进入：

- 用户或资产方已导出的 `.fbx`；
- 授权文本、来源说明和校验清单；
- 贴图、预览图及 DCC 交换格式；
- 能证明来源、许可和处理参数的 JSON。

禁止进入：

- `.uasset`：只能位于 Unreal 项目的 `Content/`；
- `.umap`：只能位于 Unreal 项目的 `Content/`；
- `.tps`：TexturePacker 工程或引擎附带二进制不得作为参考资产入库；
- `Binaries/`、`DerivedDataCache/`、`Intermediate/`、`Saved/` 和打包输出；
- 无授权证据、来源不明、仅从本机其他目录发现的资产。

扩展名门禁大小写不敏感，并递归检查全部子目录。符号链接不跟随，避免扫描逃逸到目录边界之外。

## 2. 来源与授权

每项参考资产必须有明确的权利人、许可记录和允许用途。用于 Maya 规范化时，清单的 `permittedUses` 至少包含 `normalize` 与 `unreal-import`。未知授权不得用占位文本替代，也不得仅因文件在本机可见就读取、复制或纳入仓库。

自动化只能处理用户明确指定、已经导出的 FBX。禁止遍历个人素材库、共享盘或其他样例目录寻找输入；清单路径必须相对清单文件，且禁止绝对路径和 `..`。

### Epic Automotive Configurator

- 本机参考工程固定为 `D:\ProjectData\UE\Project\AutomotiveConfigurator`，只作为隔离的外部研究源，不属于本仓库。
- Fab 页面声明该样例仅用于 Unreal Engine 产品，并标记 `Allows usage with AI: No`。被许可方已于 2026-10-03 确认专项书面授权有效，授权覆盖将指定样例作为 AI 输入用于内部研发、分析和测试；该豁免不扩展到其他 Fab 资产。
- 专项授权仍禁止二次分发、公开分享、转授权、公开训练数据集和素材再发布。当前 GitHub 远端可公开访问，因此样例源文件、规范化 FBX/Maya 文件、导入后的 `.uasset`、验证地图和包含授权几何的截图必须保留在本地忽略目录，不得提交或推送。
- 授权证明原文只在本地受控目录归档；公开仓库仅记录许可边界和证明文件 SHA-256，不记录被许可方邮箱等个人信息。证明文件 SHA-256 为 `70c7d51b9357f138e7573c1da38442876942796302fdb72e82633526b8a6efe5`。
- 禁止把样例的 `.uasset`、`.umap`、`.tps`、Blueprint、Control Rig、材质、材质函数、纹理、环境地图、音频或项目配置提交、推送或公开分发。
- 在专项授权覆盖的内部本机环境中，允许把车辆运行所需的官方材质、材质函数和纹理按原始 `/Game` 路径复制到 Git 忽略的 `Content/References/AutomotiveMats/`，仅供本地 Editor、测试和 Shipping 使用。接入时必须保留依赖闭包；为兼容重建后的 SkeletalMesh，可只修改母材质的 `Used with Skeletal Mesh` 使用标志，不得借机改写官方视觉参数。
- 上述本机授权内容必须通过 `.git/info/exclude` 或等效本地排除规则隔离；公开仓库只能保存对象路径、加载逻辑、Cook 规则和不含资产正文的验证代码。缺少本机授权内容时，测试和发布必须明确失败，不得静默回退到 UE 默认材质。
- 车辆几何只有在授权允许且显式导出为 FBX 后，才能作为本工具的输入；输入 FBX 仍需经过 Maya 2025 规范化、sidecar、管理员暂存导入和 UE GUI 验收。
- 当前本机 `AuthorizedAudiA5` 只作为已授权的内部代理车辆，不是 SC01 正式资产，不得用于对外暗示车型、品牌或授权关系。
- Alcantara、Ultrasuede（奥司维）、超纤皮、牛皮、布艺等 Shader 只允许研究其公开表现目标和通用 PBR 原理；当前工程必须使用自己的节点网络、参数、纹理和命名从零创建。
- 环境只能研究构图、光照层次和曝光策略；盐湖地图、体积云材质、地表网格和纹理不得直接迁移。
- 当前可继续用于自动化开发的车辆代理资产是授权明确的自建几何和 CC0 资产，不依赖该 Epic 样例。

## 3. 规范化与不可变输入

- 输入 FBX 的字节数和 SHA-256 必须与清单一致。
- 输入与输出路径必须不同，`output.overwrite` 固定为 `false`。
- 工具不得修改、移动或删除输入 FBX。
- 输出固定使用 Maya 2025、厘米、Z Up、二进制 FBX 2020。
- 节点重命名只能来自显式 `nodeRenames`，不得根据名称相似度猜测。
- 清理相机和灯光、冻结旋转与缩放、是否三角化等行为必须在清单中明示。
- 规范化通过不代表授权真实性、车辆 Pivot 物理位置或最终视觉质量通过。

清单契约和示例：

- `contracts/schemas/reference-asset-normalization.schema.json`
- `contracts/fixtures/reference-asset-normalization.example.json`

Maya 2025 运行方式见 `tools/maya/README.md`。

## 4. 接入流程

1. 资产负责人把明确授权且已导出的 FBX 放入受控接入目录。
2. 计算输入文件字节数和 SHA-256，复制示例并填写真实清单；不得伪造来源字段。
3. 使用 Maya 2025 `mayapy.exe` 执行规范化。
4. 检查输出 FBX 的命令行 JSON 摘要，并在空场景重新导入核对层级、尺寸、Pivot 和材质槽。
5. 需要纳入 `SourceAssets` 时运行目录门禁。
6. 继续执行车辆 sidecar 与 Unreal 管理员导入预检；本政策不替代这些门禁。

## 5. 验证

```powershell
node tools/validate-source-assets.mjs
node --test tools/validate-source-assets.test.mjs
node tools/validate-contracts.mjs
```

任何一项失败都必须停止接入。不得通过改扩展名、忽略目录、关闭测试或覆盖原始 FBX 绕过门禁。
