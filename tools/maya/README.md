# Maya 2025 FBX 规范化工具

`normalize_fbx_maya2025.py` 只处理清单明确指定的、用户已经导出的 FBX。它不会搜索样例资产目录，也不会读取或复制清单目录之外的文件。

## 准备清单

复制 `contracts/fixtures/reference-asset-normalization.example.json` 到接入工作目录。`input.path` 与 `output.path` 均相对该清单所在目录，禁止绝对路径和 `..`。

填写真实来源、许可、输入文件大小与 SHA-256：

```powershell
$inputFbx = 'D:\Intake\input\vehicle-exported.fbx'
(Get-Item $inputFbx).Length
(Get-FileHash -Algorithm SHA256 $inputFbx).Hash.ToLowerInvariant()
```

`nodeRenames` 是唯一允许的自动重命名来源。若无法确认目标语义，保持为空并由资产作者补充，不得猜测。

## 执行

输出文件必须尚不存在：

```powershell
& 'D:\Program Files\Autodesk\Maya2025\bin\mayapy.exe' `
  'D:\ConfigurationSystem\tools\maya\normalize_fbx_maya2025.py' `
  --manifest 'D:\Intake\reference-asset-normalization.json'
```

成功时标准输出包含输出文件大小、SHA-256 和根节点。脚本会：

1. 校验 Maya 版本、清单字段、相对路径、授权用途及输入哈希；
2. 新建空场景并只导入 `input.path`；
3. 固定厘米和 Z Up，删除导入的相机与灯光；
4. 执行显式节点重命名，建立或整理唯一根节点；
5. 冻结旋转与缩放，按清单决定是否三角化；
6. 以二进制 FBX 2020 导出到新的 `output.path`。

脚本不会覆盖输入或已有输出。失败返回非零退出码。

## 自建几何 canary

canary 在指定目录内自行创建 Cube、相机和灯光，先导出测试 FBX，再走完整规范化流程并重新导入核验。它不需要、也不会读取外部样例资产。

```powershell
& 'D:\Program Files\Autodesk\Maya2025\bin\mayapy.exe' `
  'D:\ConfigurationSystem\tools\maya\normalize_fbx_maya2025.py' `
  --canary-dir 'D:\ConfigurationSystem\.tmp\maya-fbx-canary'
```

预期 JSON 包含：

```json
{
  "rootNode": "Vehicle_Root",
  "canary": "passed"
}
```

canary 仅证明 Maya/FBX 插件和程序化规范化链路可运行，不替代真实车辆的层级、Pivot、材质、穿模和 Unreal 导入验收。

## SC01 40 surface 材质槽重建

`build_sc01_surface_slots_maya2025.py` 读取独立
`vehicle-surface-binding` 1.0 契约，严格按固定 `selectionOrder` 处理 38 条
规则。每条规则必须显式指定目标 Mesh、当前来源材质 ID 和面范围；工具会先
确认这些面当前确实属于该来源材质，再创建唯一 `sc01_*` 材质与
shadingEngine。规则重叠、节点/材质不存在、面越界、输入哈希不符或输出已存在
都会失败，不进行名称猜测，也不覆盖输入。

```powershell
& 'D:\Program Files\Autodesk\Maya2025\bin\mayapy.exe' `
  'D:\ConfigurationSystem\tools\maya\build_sc01_surface_slots_maya2025.py' `
  --binding 'D:\Intake\vehicle-surface-binding.json' `
  --output 'D:\Intake\output\sc01-surface-ready.fbx'
```

输出固定为二进制 FBX 2020，并打印每个 surface 的面数、输出大小和 SHA-256。
仓库正向 fixture 只验证规则形状与 40 surface 完整性；其中节点、来源材质、
面范围及哈希不对应任何真实车辆，不能直接用于生产重建。
