# Server

基于 TypeScript 与 Fastify 的车辆配置和静态渲染服务。服务直接读取仓库根目录
`contracts/fixtures/`，不复制契约数据。

## 使用

要求 Node.js 20 或更高版本：

```powershell
npm install
npm run build
npm test
npm start
```

环境变量：

- `HOST`：监听地址，默认 `0.0.0.0`。
- `PORT`：监听端口，默认 `3000`。
- `RENDER_ROOT`：图片根目录。其下应为
  `<publicationVersion>/<vehicleId>/<configurationKey>/<renderViewId>.png`；
  默认指向仓库根目录的 `package/renders`。

## API

- `GET /health`：返回 catalog 与 publication 版本。
- `GET /api/v1/catalog`：返回 `catalog.mvp.json`。
- `POST /api/v1/renders/resolve`：校验 catalog/publication 版本、车型、四分区
  selections 和渲染视角，返回 canonical key、视角及图片 URL。
- `GET /assets/renders/:publicationVersion/:vehicleId/:configurationKey/:renderViewId.png`：
  安全读取 `RENDER_ROOT` 内的 PNG。

resolve 请求示例：

```json
{
  "catalogVersion": "mvp-v1",
  "publicationVersion": "mvp-v1",
  "vehicleId": "demo-car",
  "selections": {
    "paint": "paint-red",
    "wheel": "wheel-sport",
    "interior": "interior-dark",
    "frame": "frame-black"
  },
  "renderViewId": "front"
}
```

非法输入返回固定的 `{ "code": "...", "message": "..." }` 错误体；不存在的
配置、车型或图片返回 `404`，版本不一致返回 `409`。图片路径在 `realpath`
后再次做根目录边界检查，阻止路径穿越和符号链接逃逸。

可部署产物输出到 `package/server/`，不提交到 Git。
