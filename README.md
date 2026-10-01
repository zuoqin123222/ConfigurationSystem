# ConfigurationSystem

汽车选配系统，包含 UE5.8 Windows 桌面端、响应式 Web 选配端和配套 Server。

## 项目组成

- **UE5.8 桌面端**：主工程，提供完整的车辆选配效果与车辆实时渲染。
- **Web 端**：适配手机竖屏和大屏，通过 Server 下发的烘焙图片实现车辆选配。
- **Server**：管理并下发预先烘焙的车辆配置图片，为 Web 端选配流程提供服务。

## 目录规划

```text
ConfigurationSystem/
├── ue/       # UE5.8 Windows 桌面端主工程
├── web/      # 响应式 Web 选配端
└── server/   # 图片资源与选配服务
```

具体开发、构建和部署方式将在各子工程确定技术方案后分别补充。
