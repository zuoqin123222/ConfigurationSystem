# Source

本目录存放 Git 管理的产品源码。

- `clients/ue/`：UE5.8 Windows 桌面端 C++ 工程。
- `clients/web/`：响应式 Web 选配端。
- `server/`：图片资源与选配服务。

本地构建和发布产物统一输出到仓库根目录下被 Git 忽略的 `package/`，不要写入源码目录。

三端共享的 ID、配置键、价格、出图 manifest 和 HTTP 接口以根目录 `contracts/` 为唯一契约来源。
