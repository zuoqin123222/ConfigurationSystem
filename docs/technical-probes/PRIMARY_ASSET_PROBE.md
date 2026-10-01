# Primary Asset 扫描与 Cook 探针

状态：通过  
验证引擎：Unreal Engine 5.8.1，Changelist 56057345  
验证平台：Windows 64-bit  
验证配置：Editor Development、Game Development、Game Shipping

## 目标

验证一个没有被地图硬引用的 Primary Data Asset 及其软引用纹理能够：

1. 在 Editor 启动时被 Asset Manager 配置发现。
2. 进入 Windows Cook。
3. 在 Development 包中被枚举和异步加载。
4. 在 Shipping 包中被枚举和异步加载。

## 测试资产

```text
/Game/PrimaryAssetProbe/DA_ProbeUnreferenced
/Game/PrimaryAssetProbe/T_ProbeUnreferenced
```

`DA_ProbeUnreferenced` 是 `UPrimaryAssetProbeData` 实例。测试纹理只通过带有 `AssetBundles="Probe"` 的 `TSoftObjectPtr<UTexture2D>` 引用；项目地图没有引用两个测试资产。

Primary Asset 扫描规则位于 `Config/DefaultGame.ini`：

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="PrimaryAssetProbe",AssetBaseClass="/Script/ConfigurationSystem.PrimaryAssetProbeData",bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/PrimaryAssetProbe")),SpecificAssets=,Rules=(Priority=1,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))
```

## 实现边界

- 不带 `-PrimaryAssetProbe` 时，不注册探针回调，不改变正常启动路径。
- 带开关时，运行时只读取 Asset Manager 启动扫描结果，不调用 `ScanPathsForPrimaryAssets` 补扫。
- 探针使用 `LoadPrimaryAssets` 和 `Probe` Bundle 异步加载。
- 结果以 JSON 写出；没有发现资产、加载失败或写入失败时返回非零状态。
- 测试资产生成命令只存在于 `ConfigurationSystemEditor` 模块，不进入 Game 包。

## 重复执行

以下命令使用本机 UE5.8 安装位置。包输出目录可替换为任意临时目录。

### 编译 Editor

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  ConfigurationSystemEditor Win64 Development `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -WaitMutex -NoHotReloadFromIDE
```

### 创建测试资产

仅在测试资产缺失或需要刷新内容时执行：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -unattended -NoSplash -NullRHI `
  '-ExecCmds=PrimaryAssetProbe.CreateTestAssets,QUIT_EDITOR' -log
```

创建资产和运行扫描必须使用两个独立 Editor 进程，避免创建后手动补扫掩盖启动配置错误。

### Editor 验证

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -unattended -NoSplash -NullRHI -PrimaryAssetProbe `
  '-PrimaryAssetProbeOutput=D:\ConfigurationSystem\source\clients\ue\Saved\PrimaryAssetProbe\Editor-5.8.1.json' `
  -log
```

### Development Cook

```powershell
$ProbeRoot = Join-Path $env:TEMP 'ConfigurationSystemPrimaryAssetProbe'

& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' `
  BuildCookRun `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -noP4 -platform=Win64 -clientconfig=Development `
  -build -cook -stage -pak -archive `
  "-archivedirectory=$ProbeRoot\Development" `
  -unattended -utf8output '-map=/Engine/Maps/Entry'
```

### Shipping Cook

```powershell
$ProbeRoot = Join-Path $env:TEMP 'ConfigurationSystemPrimaryAssetProbe'

& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat' `
  BuildCookRun `
  '-Project=D:\ConfigurationSystem\source\clients\ue\ConfigurationSystem.uproject' `
  -noP4 -platform=Win64 -clientconfig=Shipping `
  -build -cook -stage -pak -archive `
  "-archivedirectory=$ProbeRoot\Shipping" `
  -unattended -utf8output '-map=/Engine/Maps/Entry'
```

运行包时应使用 `Start-Process -Wait`，否则 GUI 子系统程序可能在自动化终端返回后仍未完成探针：

```powershell
$Arguments = @(
  '-unattended',
  '-NoSplash',
  '-NoSound',
  '-windowed',
  '-ResX=64',
  '-ResY=64',
  '-PrimaryAssetProbe',
  "-PrimaryAssetProbeOutput=$ProbeRoot\Development\DevelopmentProbe.json"
)

Start-Process `
  -FilePath "$ProbeRoot\Development\Windows\ConfigurationSystem\Binaries\Win64\ConfigurationSystem.exe" `
  -ArgumentList $Arguments `
  -WorkingDirectory "$ProbeRoot\Development\Windows" `
  -Wait
```

Shipping 可执行文件名为 `ConfigurationSystem-Win64-Shipping.exe`。

## 结果

| 阶段 | 引擎 | 配置 | Editor | Cooked Data | 枚举数 | Data Asset | 软纹理 |
|---|---|---|---:|---:|---:|---:|---:|
| Editor | 5.8.1 | Development | 是 | 否 | 1 | 通过 | 通过 |
| Windows 包 | 5.8.1 | Development | 否 | 是 | 1 | 通过 | 通过 |
| Windows 包 | 5.8.1 | Shipping | 否 | 是 | 1 | 通过 | 通过 |

满足：

```text
Editor发现数 == Development发现数 == Shipping发现数 == 1
```

Development 和 Shipping 均由 UE5.8.1 完成 Build、Cook、Stage、Pak 和 Archive，随后从包内运行时 Asset Registry 枚举并异步加载成功。

## 发现的问题

### Asset Manager 配置文件

首次实现把 `[/Script/Engine.AssetManagerSettings]` 写入 `DefaultEngine.ini`，运行时启动枚举为 0。该设置属于 Game 配置，迁移到 `DefaultGame.ini` 后启动扫描生效。

### 补扫会制造假阳性

首次实现曾在探针内调用 `ScanPathsForPrimaryAssets`，Editor 会显示通过，但它无法证明项目启动配置正确。最终实现删除补扫，只接受启动时注册结果。

### GUI 程序等待

直接从自动化终端调用 Windows GUI 可执行文件时，父命令可能提前返回。使用 `Start-Process -Wait` 后，JSON 和退出状态可以稳定采集。

## 结论

P0-1 在 UE5.8.1 上通过。后续正式材质库可采用同一模式：

- 材质选项继承 `UPrimaryDataAsset`。
- 目录注册写入 `DefaultGame.ini`。
- 实际材质和预览图使用软引用及 Asset Bundle。
- 生产类型使用明确 Cook Rule。
- 每次发布同时执行 Development 与 Shipping 运行时枚举/加载验证。
