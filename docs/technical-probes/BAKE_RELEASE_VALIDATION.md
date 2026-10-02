# Bake 与发布阶段验收

验收日期：2026-10-02  
代码基线：`cf4ce0f`  
发布版本：`mvp-v1`

## 验收范围

本阶段验证 UE 批量出图、不可变发布门禁、Server 资源分发、Web 图片切换，以及当前
代码基线的 Windows Shipping 打包。`mvp-v1` 使用
`Configurator.Resource.Temporary` 占位车辆，仅用于流水线与交互验收，不代表最终车辆
资产或最终产品画质。

## 自动化结果

- UE Batch Bake：16 个 canonical configuration × 4 个 RenderView，共 64 个任务。
- Server：19 个测试全部通过。
- Web：18 个测试全部通过，Vite production build 成功。
- Bake manifest：64 个 `ready` RGBA PNG 全部通过尺寸、路径和 SHA-256 校验。
- HTTP 分发：64/64 PNG 可由 Server 返回。
- UE Shipping：Build/Cook/Stage/Pak/Archive 成功，AutomationTool ExitCode 为 0。

## GUI 回归

### Web

- 桌面布局验证四个视角：`front`、`front-left`、`side`、`rear-right`。
- 验证默认配置、银色车漆配置和豪华模板配置，图片 URL 均来自
  `/assets/renders/mvp-v1/<vehicle>/<canonical-key>/<view>.png`。
- 489 px 窄视口无横向溢出，展厅与选配面板按移动布局纵向排列。
- 故障注入时停止 Server，再切换到未缓存配置；选中状态立即更新，但旧图继续显示，
  没有空白闪烁。Server 恢复后重新触发选择，新图加载成功。

移动视口证据：

![Web mobile 489px](assets/bake-release/web-mobile-489px.jpg)

### UE Shipping

归档后的 `ConfigurationSystem.exe` 启动后产生可响应窗口，标题为“汽车选配系统”，
窗口尺寸为 1296 × 759。等待渲染稳定后，展厅、占位车辆、配置面板、模板、体验控制和
实时模式均可见。

![UE Shipping GUI](assets/bake-release/ue-shipping-gui.png)

## 交付位置

- UE Windows Shipping：`package/clients/ue/Windows`
- Web production build：`package/clients/web`
- 不可变 Render 发布：`package/renders/mvp-v1`
- Server 编译产物：`source/server/dist`

关键文件：

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| `package/clients/ue/Windows/ConfigurationSystem.exe` | 172032 | `bceb51f8f21c315ed663e62bd87e9eb75b1595534fbe5af5be9f049462ddacd5` |
| `package/clients/ue/Windows/ConfigurationSystem/Content/Paks/ConfigurationSystem-Windows.pak` | 11374123 | `ad62192a9c58557a164c7be2a10a6b6037f028c727e921d42443e1f7c1b6e38d` |
| `package/clients/web/index.html` | 456 | `23a2801c16fc0ed3e236182c7978f04252769329c98148bdf4dbfd3ef6058abb` |
| `package/renders/mvp-v1/bake-manifest.json` | 37092 | `80bbd1b64790bd37f252c443e6c0e805662251b9a3aa468573494e68de7d293e` |

64 张 PNG 合计 19,774,171 字节。

## 发布边界

- `mvp-v1` 不可覆盖；正式车辆接入后必须发布新版本目录。
- 当前图片规格为 640 × 360、16 SPP，仅用于流水线验收。
- 正式发布前必须重新检查真实车辆层级、Pivot、材质槽、玻璃、穿模、动作和构图，并
  使用正式分辨率与采样数重新生成 64 图。
- Runtime 仅接收通过 manifest、版本、路径、大小和 SHA-256 校验的标准 `.pak`。
