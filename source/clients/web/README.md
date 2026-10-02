# Web 车辆选配端

基于 Vite、React 和 TypeScript 的响应式中文汽车选配界面。大屏使用右侧选配面板，手机竖屏使用底部抽屉式面板；展厅背景完全由 CSS/SVG 绘制。

## 功能

- 启动时并行请求 `GET /health` 与 `GET /api/v1/catalog`，从健康检查响应取得 `publicationVersion`，并校验目录版本一致。
- 四个固定分区切换，展示中文名、差价和选项预览图。
- 一键应用两个目录模板。
- 按 `paint → wheel → interior → frame` 固定顺序生成 canonical key。
- 实时计算基础价与所有选项差价的总价。
- 配置或视角变化时调用 `POST /api/v1/renders/resolve`，严格使用响应中的 `imageUrl`，不在客户端拼接渲染地址。
- 图片解析期间展示加载态；404 显示当前配置缺图提示；409 提示重新加载目录。
- 新的解析请求通过 `AbortController` 中止旧请求，并忽略迟到响应。
- 主图和选项预览图加载失败时显示中文占位。

canonical key 与价格始终根据本地选择即时计算，不等待图片解析。

## 本地运行

要求 Node.js 18 或更高版本。

```powershell
npm install
npm run dev
```

开发服务器默认把 `/health`、`/api` 和 `/assets` 代理到 `http://localhost:8080`。如服务地址不同，请修改 `vite.config.ts`。

## 验证

```powershell
npm test
npm run build
```

API 测试覆盖启动并行请求、`publicationVersion`、版本冲突、resolve 请求体、服务端 `imageUrl` 与错误状态。UI 测试覆盖加载/重试、选配、模板、视角、本地 key/价格即时更新、解析加载、404、409、请求中止、迟到响应隔离及图片加载失败占位。

构建产物输出到仓库根目录的 `package/clients/web/`，不提交到 Git。
