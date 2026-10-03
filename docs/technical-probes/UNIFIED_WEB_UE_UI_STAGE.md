# Web 与 UE 统一选配界面阶段

## 目标

SC01 的 Web 与 UE 客户端采用同一套选配信息架构和视觉语言。两端的差异只位于车辆预览能力：

- Web 使用按视角解析的静态图片。
- UE 使用可拖拽、缩放和切换预设镜头的实时视口。
- UE 的镜头、车辆动画、灯光、渲染模式、复位和全屏控制位于实时视口底部。
- UE 专属画质设置位于实时视口右上角，不覆盖右侧网页选配面板。

布局参考 Porsche 911 与 Range Rover 在线选装器的媒体区、配置步骤、场景控制和右侧摘要关系，不复制其品牌资产或视觉标识。

## 实现

Web 继续作为选配内容的唯一实现，独立站点和 UE 内嵌模式共用目录、筛选、材料、颜色、保存和分享逻辑。独立 Web 只显示当前静态资源能够真实支持的镜头与浏览器全屏按钮；灯光和车辆动画等需要组合渲染资源的能力保持隐藏。

UE 保留右侧 480 px 的 `UWebBrowser` 选配面板，并在原生 UMG 层增加底部横向控制栏。控制栏包含镜头、动画、灯光、渲染、复位和全屏，所有操作都调用目标状态接口，不使用重复消息可能反转状态的 Toggle 协议。

内嵌 Web 通过 `UConfiguratorWebBridge` 只暴露 `ApplyConfigurationJson`。输入限制为 64 KiB，并校验根字段、surface/option 字符串、材质 Variant、车漆颜色和 0 到 1 的参数范围。通过校验后调用 SC01 v2 状态的原子事务接口，已有材质绑定器随状态广播更新实时车辆。

## 验证

- Web Vitest：4 个测试文件、28 个用例通过。
- Web TypeScript 与 Vite 生产构建通过，输出已更新到 `package/clients/web/`。
- `ConfigurationSystemEditor Win64 Development` 编译通过。
- `ConfigurationSystem.Runtime.ConfiguratorPanel.WebDirection` 自动化测试通过。
- Shipping `BuildCookRun` 成功，最终包已更新到 `package/clients/ue/Windows/`。
- 真实 Shipping 窗口确认右侧 Web 面板、左侧实时视口、视口底部横向控制栏和视口右上角齿轮同时存在。

GUI 证据：

- `docs/technical-probes/assets/ue-unified-ui-settings.png`

## 边界

Web 端的灯光和车辆动画将在对应组合图片或视频资源覆盖足够时再开放。UE 端继续使用实时能力，不要求为 Web 预先生成指数级组合资源。

当前代理车辆的造型、材质与镜头只用于功能验证。正式 SC01 模型到位后仍需按管理员暂存导入、表面映射、Pivot、材质槽、动画、LOD、穿模和 Path Tracing 流程重新验收。
