# Path Tracing 透明出图探针

状态：通过，但原始输出必须规范化

验证引擎：Unreal Engine 5.8.1，Changelist 56057345

验证硬件：NVIDIA GeForce RTX 5080，D3D12 SM6

输出格式：BGRA8 PNG、RGBA16F EXR

## 目标

验证 UE5.8 Path Tracing 是否能输出可供 Web 合成的透明车辆图，并覆盖：

- 不透明材质。
- 半透明材质。
- 自发光及加法光晕。
- PNG 与 EXR。
- Alpha 裁切。
- 黑、白、灰、红和棋盘背景合成。

## 测试场景

探针在 Editor `-game` Runtime World 中创建：

| 位置 | 材质 | 验证目标 |
|---|---|---|
| 左 | Engine DefaultMaterial | 不透明覆盖 |
| 中 | Engine M_SimpleTranslucent | 半透明覆盖 |
| 右 | Engine EmissiveMeshMaterial | 自发光和加法光晕 |

相机固定为 640×360，关闭 Path Tracing Denoiser，等待 16 个样本后同时回读 BGRA8 与 RGBA16F。

## 原始输出结论

原始 Viewport 回读能够写出 PNG 和 EXR，但不能直接交付 Web：

```text
rawBackgroundAlpha = 255
rawBackgroundAlphaRgba16f = 1
coverageInverted = true
normalizationRequired = true
```

当前管线中的 Alpha 是反向透明度：

- 背景接近 1。
- 不透明表面接近 0。
- 半透明表面位于 0 和 1 之间。

直接把该 Alpha 当成 Web coverage 会导致背景不透明、不透明车身消失。

此外，加法自发光可能产生“Alpha 表示全透明，但 RGB 有能量”的像素。直接清零透明 RGB 会删除车灯和 Bloom。

## 规范化流程

正式出图必须执行以下步骤：

1. 使用四角 Alpha 平均值识别背景语义。
2. 背景 Alpha 大于 0.5 时执行 `coverage = 1 - rawAlpha`。
3. 对 coverage 接近 0、但 RGB 明显非零的加法像素构造 synthetic Alpha。
4. 将加法像素 RGB 从预乘贡献反解为 straight RGB。
5. 清零剩余 coverage 为 0 的 RGB。
6. 只使用规范化 coverage 计算裁切范围。
7. 输出 `alphaMode=straight` 的 PNG 和 EXR。
8. 在五种背景上生成合成预览。

加法恢复的简化公式：

```text
syntheticAlpha = clamp(max(R, G, B), 0, 1)
straightRGB = additiveRGB / syntheticAlpha
```

该方案用于 MVP 单层透明图。如果后续要求更强的灯光可控性，建议把自发光/Bloom 输出为独立 additive 图层。

## 验证结果

| 项目 | BGRA8 | RGBA16F |
|---|---:|---:|
| 原始背景 Alpha | 255 | 1.0 |
| Alpha 反转 | 是 | 是 |
| 覆盖测试物体数 | 3 | 3 |
| 自发光恢复像素 | 43229 | 28609 |
| 规范化透明 RGB 污染 | 0 | 0 |
| 规范化内容宽度 | 506 | 506 |
| Web 就绪 | 是 | 是 |

最终结果：

```text
finalSampleIndex = 16
targetSampleCount = 16
alphaMode = straight
webReady = true
passed = true
```

## 合成验证

- [透明裁切 PNG](assets/p0-3/PathTracingAlphaProbe-Cropped-Normalized.png)
- [黑色背景](assets/p0-3/PathTracingAlphaProbe-Cropped-Normalized-Black.png)
- [白色背景](assets/p0-3/PathTracingAlphaProbe-Cropped-Normalized-White.png)
- [灰色背景](assets/p0-3/PathTracingAlphaProbe-Cropped-Normalized-Gray.png)
- [红色背景](assets/p0-3/PathTracingAlphaProbe-Cropped-Normalized-Red.png)
- [棋盘背景](assets/p0-3/PathTracingAlphaProbe-Cropped-Normalized-Checkerboard.png)

棋盘背景验证了不透明、半透明和自发光区域都参与合成；黑色背景用于确认光晕能量没有在规范化时被删除。

## 输出文件

探针保留原始、裁切、规范化和裁切规范化四组输出：

- `PathTracingAlphaProbe.png`
- `PathTracingAlphaProbe.exr`
- `PathTracingAlphaProbe-Cropped.png`
- `PathTracingAlphaProbe-Cropped.exr`
- `PathTracingAlphaProbe-Normalized.png`
- `PathTracingAlphaProbe-Normalized.exr`
- `PathTracingAlphaProbe-Cropped-Normalized.png`
- `PathTracingAlphaProbe-Cropped-Normalized.exr`

机器可读报告：

- [P0-3 JSON](assets/p0-3/path-tracing-alpha-probe-results.json)

## 产品实现结论

### Editor 出图

Editor Bake Pipeline 不得直接发布 Renderer 原始 PNG。每张图必须经过：

```text
Render
→ Alpha 语义识别
→ Coverage 规范化
→ 自发光恢复
→ 透明 RGB 清理
→ Alpha 裁切
→ 多背景验证
→ 发布
```

manifest 至少记录：

- `alphaMode: straight`
- `coverageInverted`
- `normalizationRequired`
- `glowRecoveredPixels`
- 原始尺寸与裁切尺寸。
- PNG 和 EXR 哈希。
- 五背景验证结果。

### Web 合成

Web 使用规范化后的 straight-alpha PNG，不使用原始 PNG。CSS 或 Canvas 采用普通 source-over 合成即可。

如果后续拆分灯光图层：

- 车身使用 straight-alpha source-over。
- 车灯/Bloom 使用独立 additive 或 screen 图层。

## 限制

- 本次使用 Engine 测试材质，不等同于最终车辆玻璃和车灯材质。
- 当前探针从 Game Viewport 回读；正式批量出图采用 MRQ/MRG 时必须重跑同样的 Alpha 语义和像素校验。
- 16 SPP 只用于技术验证，不能代表交付质量。
- 测试场景没有地面阴影；是否保留阴影必须在正式 Bake 规范中单独定义。
- synthetic Alpha 是 MVP 单图折中方案，需要在真实车灯 Bloom 上调节阈值。

## 结论

P0-3 在 UE5.8.1 上通过。Path Tracing 可以生成包含不透明、半透明和自发光内容的 PNG/EXR，但原始 Alpha 不能直接供 Web 使用。完成反向 Coverage 转换、自发光恢复、透明 RGB 清理和 Alpha 裁切后，输出可作为 Web straight-alpha 图片。
