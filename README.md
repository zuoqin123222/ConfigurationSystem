# ConfigurationSystem

汽车选配系统，包含 UE5.8 Windows 桌面端、响应式 Web 选配端和配套 Server。

## 文档

- [系统架构与三端边界](docs/ARCHITECTURE.md)
- [AI 协作工作流](docs/AI_WORKFLOW.md)
- [架构决策记录](docs/DECISIONS.md)
- [贡献与验证指南](CONTRIBUTING.md)
- [MVP 实施计划](docs/MVP_PLAN.md)
- [MVP 多重验证报告](docs/MVP_PLAN_REVIEW.md)
- [P0-1 Primary Asset 扫描与 Cook 探针](docs/technical-probes/PRIMARY_ASSET_PROBE.md)
- [P0-2 Runtime Path Tracing 与进度探针](docs/technical-probes/PATH_TRACING_RUNTIME_PROBE.md)
- [P0-3 Path Tracing 透明出图探针](docs/technical-probes/PATH_TRACING_ALPHA_PROBE.md)
- [P0-4 车辆层级与可逆动作探针](docs/technical-probes/VEHICLE_HIERARCHY_PROBE.md)
- [P0-5 Development/Shipping 打包边界探针](docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md)
- [车辆资产接入规范](docs/VEHICLE_ASSET_REQUIREMENTS.md)
- [DCC 车辆模型制作与导出指南](docs/DCC_VEHICLE_MODELING_EXPORT_GUIDE.md)
- [Runtime 内容包契约与安全挂载](docs/CONTENT_PACK_RUNTIME.md)
- [三端数据与 API 契约](contracts/README.md)
- [MVP 可视化评审版](automotive-configurator-mvp-plan/automotive-configurator-mvp-plan.html)

## 项目组成

- **UE5.8 桌面端**：主工程，提供完整的车辆选配效果与车辆实时渲染。
- **Web 端**：适配手机竖屏和大屏，通过 Server 下发的烘焙图片实现车辆选配。
- **Server**：管理并下发预先烘焙的车辆配置图片，为 Web 端选配流程提供服务。

## 目录规划

```text
ConfigurationSystem/
├── source/                  # Git 管理的源码
│   ├── clients/
│   │   ├── ue/              # UE5.8 Windows 桌面端 C++ 主工程
│   │   └── web/             # 响应式 Web 选配端
│   └── server/              # 图片资源与选配服务
├── contracts/               # 三端共享 Schema、fixture 与 OpenAPI
├── tools/                   # 仓库级验证工具
├── docs/                    # 架构、决策、流程与探针证据
└── package/                 # Git 忽略的本地构建与发布产物
    ├── clients/
    │   ├── ue/
    │   └── web/
    ├── server/
    └── renders/
```

`contracts/` 与 `tools/` 当前位于仓库根目录；请勿使用早期计划中的 `source/contracts/` 或 `source/tools/`。`source/` 只保存可审查源码，`package/` 只保存可重建的本地产物。

## 契约验证

安装 Node.js 18 或更高版本后，在仓库根目录执行：

```powershell
node tools/validate-contracts.mjs
```

验证器检查 Schema、车辆模型/动画 sidecar 的 UE 坐标、FBX 2020.2、节点与标签、Pivot、材质槽、LOD、动画、授权和 SHA-256 约束，以及产品 ID 与引用、canonical key、16 个唯一配置、4 个视角和 64 个图片期望。

车辆 sidecar 的独立验证和测试命令：

```powershell
node tools/validate-vehicle-sidecars.mjs
node --test tools/validate-vehicle-sidecars.test.mjs
node --test tools/validate-content-pack.test.mjs
```

UE 编译、Cook、Development/Shipping 探针命令见 [贡献与验证指南](CONTRIBUTING.md)。

## UE 桌面端

工程入口为 `source/clients/ue/ConfigurationSystem.uproject`，当前基础配置包括：

- UE5.8 C++ Runtime 与 Editor Target。
- Enhanced Input 输入系统。
- Windows DX12 与 Shader Model 6。
- Lumen 全局光照和反射、Nanite、虚拟阴影贴图。
- 禁用静态光照，以实时车辆展示为默认方向。

首次打开前，请确认已安装 UE5.8 和对应的 Visual Studio C++ 工具链。工程文件位于 `source/clients/ue/ConfigurationSystem.uproject`。可生成 Visual Studio 项目文件后编译 `ConfigurationSystemEditor`，也可使用 `CONTRIBUTING.md` 中基于 `Build.bat` 的实际命令。

当前 UE 工程是初始骨架，业务模块、Web 技术栈和 Server 方案见 MVP 实施计划。
