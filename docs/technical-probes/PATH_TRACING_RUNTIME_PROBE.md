# Runtime Path Tracing 与进度能力探针

状态：通过

验证引擎：Unreal Engine 5.8.1，Changelist 56057345

验证硬件：NVIDIA GeForce RTX 5080，驱动 581.95

验证平台：Windows 11、D3D12 SM6

## 目标

验证 UE5.8 Windows Runtime 是否能够：

1. 检测硬件、RHI、Shader Platform 与项目光追配置。
2. 在 Editor Game、Development 和 Shipping 中切换到 Path Tracing。
3. 通过公开 API 读取当前样本数和目标样本数。
4. 在相机变化后观察采样归零。
5. 在归零后重新累积。

## 项目设置

`Config/DefaultEngine.ini` 启用：

```ini
[/Script/Engine.RendererSettings]
r.RayTracing=True
r.PathTracing=True
r.SkinCache.CompileShaders=True
r.PathTracing.ProgressDisplay=True
```

Windows 使用 DX12 和 SM6。

## 公开接口

探针只使用公开 Runtime 头文件和导出接口：

```cpp
GameViewport->SetViewMode(VMI_PathTracing);

GameViewport->ViewModeIndex = VMI_PathTracing;
ApplyViewMode(VMI_PathTracing, true, GameViewport->EngineShowFlags);

const uint32 CurrentSample =
    View.State->GetPathTracingSampleIndex();

const uint32 TargetSample =
    View.State->GetPathTracingSampleCount();
```

采样值通过 `FSceneViewExtensionBase::SetupView` 读取。`FSceneViewStateInterface` 的两个进度接口声明在公开的 `SceneManagement.h`，探针不包含 Renderer Private 头文件。

进度公式：

```text
progress = clamp(currentSample / max(targetSample, 1), 0, 1)
```

## Runtime 切换限制

`UGameViewportClient::SetViewMode` 的 UE5.8 实现会限制 Game 构建中的 Debug ViewMode：

- Editor Game 可以直接切换。
- Cooked Development 在未启用 Debug ViewMode 时回退到 Lit。
- Shipping 会无条件把 `SetViewMode` 请求回退到 Lit。

探针验证了公开的直接适配路径：

```cpp
GameViewport->ViewModeIndex = VMI_PathTracing;
ApplyViewMode(VMI_PathTracing, true, GameViewport->EngineShowFlags);
```

该路径在 Cooked Development 和 Shipping 中均成功，并产生真实 Path Tracing 采样。项目后续应把它封装在 `UCarRenderModeService`，不能直接依赖控制台命令。

由于该适配路径绕过了 `SetViewMode` 内部守卫，每次 UE 小版本升级都必须重跑本探针。

## 自动验证流程

探针将样本目标临时设为 16，缩短验证时间：

1. 等待 GameViewport 和 World。
2. 检查 RHI 与 Path Tracing 能力。
3. 请求 Path Tracing ViewMode。
4. 等待公开样本索引至少增长 3。
5. 将 PlayerController 的 Yaw 增加 5°。
6. 验证样本索引下降。
7. 验证样本索引再次增长至少 2。
8. 恢复 `VMI_Lit` 实时模式。
9. 写入 JSON 并按结果退出。

## 结果

| 阶段 | 标准 SetViewMode | 公开直接适配 | 精确进度 | 首次累积 | 相机后归零 | 重新累积 | 返回实时 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Editor Game Development | 通过 | 未使用 | 通过 | 0 → 3 | 3 → 0 | 0 → 2 | 通过 |
| Cooked Development | 被守卫回退 | 通过 | 通过 | 0 → 3 | 3 → 0 | 0 → 2 | 通过 |
| Cooked Shipping | 被守卫回退 | 通过 | 通过 | 0 → 3 | 3 → 0 | 0 → 2 | 通过 |

三种运行环境均满足：

```text
compiledWithRayTracing = true
GRHISupportsRayTracing = true
GRHISupportsRayTracingShaders = true
IsRayTracingEnabled = true
platformSupportsPathTracing = true
exactProgressApiAvailable = true
```

## 产品实现结论

### 模式切换

`UCarRenderModeService` 应统一管理 Lit 与 Path Tracing：

- 进入 Path Tracing 时设置 ViewModeIndex 并调用 `ApplyViewMode`。
- 返回实时模式时应用 `VMI_Lit`。
- 切换前检查硬件、RHI、Shader Platform 和项目设置。
- 不支持时禁用按钮并给出具体原因。

### 进度条

可以实现精确进度条，不需要使用不确定进度：

- `currentSample` 读取 `GetPathTracingSampleIndex()`。
- `targetSample` 读取 `GetPathTracingSampleCount()`。
- 相机、材质、灯光或车辆变化会使 `currentSample` 下降到 0。
- UI 检测到下降时立即把进度归零。
- `currentSample >= targetSample` 时显示完成。

### 线程边界

通过 Scene View Extension 在 `SetupView` 阶段读取 ViewState，并把快照提供给游戏线程 UI。不要从普通 UMG Tick 直接访问 Renderer 内部状态。

## 重复执行

### Editor Game

```powershell
$Arguments = @(
  'D:\ConfigurationSystem\ue\ConfigurationSystem.uproject',
  '-game',
  '-unattended',
  '-NoSplash',
  '-NoSound',
  '-windowed',
  '-ResX=320',
  '-ResY=180',
  '-PathTracingProbe',
  '-PathTracingProbeTimeout=120',
  '-PathTracingProbeOutput=D:\ConfigurationSystem\ue\Saved\PathTracingProbe\EditorGame-5.8.1.json'
)

Start-Process `
  -FilePath 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  -ArgumentList $Arguments `
  -WorkingDirectory 'D:\ConfigurationSystem\ue' `
  -Wait
```

### Cooked 包

Development 和 Shipping 使用与 P0-1 相同的 `BuildCookRun` 流程。运行时增加：

```text
-PathTracingProbe
-PathTracingProbeTimeout=120
-PathTracingProbeOutput=<输出 JSON>
```

Windows GUI 程序必须使用 `Start-Process -Wait` 运行，避免自动化终端在探针结束前返回。

## 风险与限制

- 本次只验证 RTX 5080 和当前驱动，不代表所有 DXR GPU 均满足性能要求。
- 项目正式车辆、玻璃、灯光和复杂材质可能显著降低采样速度，但不改变进度 API 结论。
- 当前测试场景使用引擎 Entry 地图，验证的是 Runtime 能力，不是最终视觉正确性。
- Renderer 自带 `r.PathTracing.ProgressDisplay` 可保留为调试选项，正式 UI 使用公开 ViewState 数据。
- Shipping 依赖直接 `ApplyViewMode` 适配，必须纳入引擎升级回归测试。

## 结论

P0-2 在 UE5.8.1、D3D12 SM6、RTX 5080 上通过。Runtime Path Tracing 可在 Development 和 Shipping 中工作，且当前样本数与目标样本数存在稳定公开接口，可以实现精确进度条和状态变化后的进度重置。
