# ConfigurationSystem

汽车选配系统，包含 UE5.8 Windows 桌面端、响应式 Web 选配端和配套 Server。

## 规划文档

- [MVP 实施计划](docs/MVP_PLAN.md)
- [MVP 多重验证报告](docs/MVP_PLAN_REVIEW.md)
- [P0-1 Primary Asset 扫描与 Cook 探针](docs/technical-probes/PRIMARY_ASSET_PROBE.md)
- [P0-2 Runtime Path Tracing 与进度探针](docs/technical-probes/PATH_TRACING_RUNTIME_PROBE.md)
- [P0-3 Path Tracing 透明出图探针](docs/technical-probes/PATH_TRACING_ALPHA_PROBE.md)
- [MVP 可视化评审版](automotive-configurator-mvp-plan/automotive-configurator-mvp-plan.html)

## 项目组成

- **UE5.8 桌面端**：主工程，提供完整的车辆选配效果与车辆实时渲染。
- **Web 端**：适配手机竖屏和大屏，通过 Server 下发的烘焙图片实现车辆选配。
- **Server**：管理并下发预先烘焙的车辆配置图片，为 Web 端选配流程提供服务。

## 目录规划

```text
ConfigurationSystem/
├── ue/       # UE5.8 Windows 桌面端 C++ 主工程
├── web/      # 响应式 Web 选配端
└── server/   # 图片资源与选配服务
```

## UE 桌面端

工程入口为 `ue/ConfigurationSystem.uproject`，当前基础配置包括：

- UE5.8 C++ Runtime 与 Editor Target。
- Enhanced Input 输入系统。
- Windows DX12 与 Shader Model 6。
- Lumen 全局光照和反射、Nanite、虚拟阴影贴图。
- 禁用静态光照，以实时车辆展示为默认方向。

首次打开前，请确认已安装 UE5.8 和对应的 Visual Studio C++ 工具链。右键 `ConfigurationSystem.uproject` 生成 Visual Studio 项目文件，编译 `ConfigurationSystemEditor` 后打开工程。

当前目录是初始 UE 骨架，目标目录迁移、业务模块、Web 技术栈和 Server 方案见 MVP 实施计划。
