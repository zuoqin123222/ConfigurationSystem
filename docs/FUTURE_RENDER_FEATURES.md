# 后续渲染能力约束

本文记录已确认但本阶段不实现的 Web 渲染能力，避免当前目录、出图和发布契约阻塞未来升级。

## 多视角 Alpha 混合

Web 确定产品视角后，可以在相邻烘焙视角之间通过 Alpha 有效区域做遮罩混合，降低直接切图的跳变感。

约束：

- 每个视角保持独立、不可变的源图；
- 混合只发生在 Alpha 有效区域，透明背景不得污染 RGB；
- manifest 记录可混合的相邻视角、混合方向和遮罩版本；
- 找不到相邻图或遮罩时回退为现有淡化切换；
- 不改变完整产品 `configurationId`。

## 透明区域裁剪

发布前允许计算非透明像素包围盒并裁掉纯透明边缘，以减少单图尺寸和网络占用。

每张裁剪图必须记录：

```text
sourceWidth
sourceHeight
cropX
cropY
cropWidth
cropHeight
anchorX
anchorY
```

Web 在合成时按原始画布尺寸和偏移恢复车辆位置，不能因为各视角包围盒不同导致车体跳动。SHA-256 针对裁剪后的发布文件计算；裁剪算法版本进入 manifest。

## 内饰 CubeMap 环视

未来增加两个独立入口：

- 主驾视角；
- 副驾视角。

每个入口支持 360 度 CubeMap 或等距柱状全景环视，并预留热点、视野角限制和高低清分级加载。

建议 manifest 字段：

```text
interiorViewId
projection
faces / panoramaUrl
resolution
format
initialYaw
initialPitch
minPitch
maxPitch
hotspots
```

CubeMap 属于内饰体验资源，不与外部四视角 PNG 混为同一 `renderViewId`。低性能设备可回退为主驾/副驾静态图。
