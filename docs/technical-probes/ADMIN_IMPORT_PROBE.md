# Editor 管理员导入探针

状态：v2 骨骼车辆入口已实现；待 UE 5.8.1 环境复验

验证引擎：Unreal Engine 5.8.1

## 边界

- 功能只编译进 `ConfigurationSystemEditor`。
- 入口为 `Tools > Configuration System 管理员导入` 的 Nomad Tab。
- UI 首选单个骨骼车辆 FBX + v2 sidecar；模型和动画分离的 v1 入口保留兼容。
- v2 sidecar 固定 `kind=rigged-vehicle`、`schemaVersion=2.0.0`，声明骨骼、
  一条完整 sequence，以及该 sequence 内的非循环 clip 帧范围。
- C++ 在导入前检查 sidecar 关键字段、坐标系、FBX 2020.2 导出约束、授权、文件存在性、非空、字节数和 SHA-256。
- 动画 sidecar 按所选 FBX 文件名匹配 `artifacts` 条目；模型使用 `artifacts.fbx`。
- 预检失败时批准按钮不可用；输入变化会立即废弃旧预检。
- 批准后只导入 `/Game/Configurator/_ImportStaging/<session>`，禁用覆盖；没有发布到正式资产目录的代码路径。
- v2 导入强制 `SkeletalMesh`、`ImportAnimations=true`、`ExportedTime`，并关闭
  材质、贴图和 PhysicsAsset 自动创建；导入后要求恰有一个 SkeletalMesh、
  有效 Skeleton、一个 AnimSequence，且网格与动画共享 Skeleton。
- 每次预检和导入结果写入 `Saved/ConfigurationSystem/AdminImportReports/<session>.json`。

这是接入门禁，不替代完整 JSON Schema 校验、FBX 层级审计、动画 Skeleton 绑定或暂存资产的人工批准/发布流程。

## 自动验证

Automation Test：

```text
ConfigurationSystem.Editor.AdminImport.Preflight
```

覆盖：

1. 内置流式 SHA-256 对标准向量计算正确。
2. 合规模型、动画及骨骼车辆 v2 sidecar 与真实 FBX 字节数/哈希通过。
3. 暂存路径严格包含唯一会话。
4. 预检 JSON 报告成功写出。
5. sidecar SHA-256 与文件不一致时拒绝。
6. v2 越界/循环 clip 被拒绝，且 `UFbxImportUI` 固定为骨骼网格和完整导出时间。

执行：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Editor.AdminImport.Preflight;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

本机验证结果：

```text
Test Completed. Result={Success}
Path={ConfigurationSystem.Editor.AdminImport.Preflight}
TEST COMPLETE. EXIT CODE: 0
```

## 无界面探针

使用 `-AdminImportPreflightProbe`。新入口参数：

```text
-AdminRiggedVehicleFbx=<path>
-AdminRiggedVehicleSidecar=<path>
```

兼容入口参数：

```text
-AdminModelFbx=<path>
-AdminModelSidecar=<path>
-AdminAnimationFbx=<path>
-AdminAnimationSidecar=<path>
```

任一组参数缺任意一个都会失败。探针不执行导入，通过返回 `0`，失败返回 `7`。
