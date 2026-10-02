# UE5 客户端

Windows 桌面客户端基于 Unreal Engine 5.8，以 Runtime C++ 模块 `ConfigurationSystem` 为业务核心，Editor-only 资产生成和审计工具位于 `ConfigurationSystemEditor`。

## 配置状态

`UCarConfigurationState` 负责：

- `paint`、`wheel`、`interior`、`frame` 四分区选择。
- 固定顺序 canonical key。
- 基础价与选项价差汇总。
- 模板原子应用。
- 非法分区、跨分区选项和未知模板拒绝。
- Blueprint 可订阅的 `OnChanged` 事件。

当前目录数据仍由程序化探针构造；正式材质 Data Asset 接入后，应从 Asset Manager 生成初始化数据，不得在 UMG 中复制价格或配置键规则。

## 验证

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  ConfigurationSystemEditor Win64 Development `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

配置状态探针参数：

```text
-ConfigurationStateProbe
-ConfigurationStateProbeOutput=<结果 JSON>
```

探针验证 8 个选项、2 个模板、16 个唯一配置键、价格、事件次数和非法输入不改变状态。

## Editor-only 管理员导入

在 Unreal Editor 中打开 `Tools > Configuration System 管理员导入`。模型和动画可以单独或同时接入，每一类都必须选择一个 `.fbx` 和对应的 sidecar `.json`。

工作流固定为：

1. 选择 FBX 与 sidecar。
2. 点击“运行 C++ 预检”。
3. 确认关键字段、授权中的 `unreal-import`、文件存在性、字节数和 SHA-256 均通过。
4. 点击“批准并导入暂存区”。
5. 在暂存资产人工验收完成后，再通过后续发布流程移动到正式目录；首版工具本身不提供正式发布按钮。

每次预检生成新会话，导入目标只能是：

```text
/Game/Configurator/_ImportStaging/<session>
```

选择发生任何变化都会使已有预检失效。导入任务设置 `bReplaceExisting=false`，且代码会再次检查预检结果和暂存路径边界。JSON 报告写入：

```text
Saved/ConfigurationSystem/AdminImportReports/<session>.json
```

报告包含引擎版本、会话、暂存路径、每个文件的声明/实际字节数与 SHA-256、错误列表、导入状态和导入对象路径。

### 自动化测试

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound `
  '-ExecCmds=Automation RunTests ConfigurationSystem.Editor.AdminImport.Preflight;Quit' `
  '-TestExit=Automation Test Queue Empty' -log
```

测试覆盖标准 SHA-256 向量、有效模型/动画 sidecar、固定暂存路径、报告写出和哈希不匹配拒绝。

### 命令行预检探针

不打开导入界面即可验证真实交付包：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -Unattended -NullRHI -NoSplash -NoSound -AdminImportPreflightProbe `
  '-AdminModelFbx=D:\Intake\car.fbx' `
  '-AdminModelSidecar=D:\Intake\car.json' -log
```

动画参数为 `-AdminAnimationFbx` 与 `-AdminAnimationSidecar`，可和模型参数同时使用。探针通过返回 `0`，失败返回 `7`，并始终尝试写出 JSON 报告。
