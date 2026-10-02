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
