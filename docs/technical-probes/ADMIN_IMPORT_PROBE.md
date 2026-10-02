# Editor 管理员导入探针

状态：首版通过

验证引擎：Unreal Engine 5.8.1

## 边界

- 功能只编译进 `ConfigurationSystemEditor`。
- 入口为 `Tools > Configuration System 管理员导入` 的 Nomad Tab。
- 模型和动画 FBX 可分别选择对应 sidecar。
- C++ 在导入前检查 sidecar 关键字段、坐标系、FBX 2020.2 导出约束、授权、文件存在性、非空、字节数和 SHA-256。
- 动画 sidecar 按所选 FBX 文件名匹配 `artifacts` 条目；模型使用 `artifacts.fbx`。
- 预检失败时批准按钮不可用；输入变化会立即废弃旧预检。
- 批准后只导入 `/Game/Configurator/_ImportStaging/<session>`，禁用覆盖；没有发布到正式资产目录的代码路径。
- 每次预检和导入结果写入 `Saved/ConfigurationSystem/AdminImportReports/<session>.json`。

这是接入门禁，不替代完整 JSON Schema 校验、FBX 层级审计、动画 Skeleton 绑定或暂存资产的人工批准/发布流程。

## 自动验证

Automation Test：

```text
ConfigurationSystem.Editor.AdminImport.Preflight
```

覆盖：

1. 内置流式 SHA-256 对标准向量计算正确。
2. 合规模型和动画 sidecar 与真实 FBX 字节数/哈希通过。
3. 暂存路径严格包含唯一会话。
4. 预检 JSON 报告成功写出。
5. sidecar SHA-256 与文件不一致时拒绝。

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

使用 `-AdminImportPreflightProbe`，并传入一组或两组参数：

```text
-AdminModelFbx=<path>
-AdminModelSidecar=<path>
-AdminAnimationFbx=<path>
-AdminAnimationSidecar=<path>
```

模型或动画的一组参数缺任意一个都会失败。探针不执行导入，通过返回 `0`，失败返回 `7`。
