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
- `PORT`：监听端口，默认 `8080`，与 Web 开发代理一致。
- `BAKE_ROOT`：发布根目录。服务启动时从
  `renders/<publicationVersion>/bake-manifest.json` 加载并校验 manifest；
  默认指向仓库根目录的 `package`。
- `V2_BAKE_ROOT`：可选的独立 v2 Bake 资产根目录；默认读取根目录下
  `bake-manifest.json`，不影响 v1 发布根。
- `V2_BAKE_MANIFEST`：可选的 v2 manifest 绝对或相对路径；可与
  `V2_BAKE_ROOT` 组合，用于 manifest 不位于根目录时显式指定。

校验与原子发布：

```powershell
npm run validate:bake -- <bake-manifest.json> <资产根目录> [published-configurations.json]
npm run publish:bake -- <包含 bake-manifest.json 的源目录> <发布根目录>
```

发布流水线必须传入同次生成的 `published-configurations.json`，校验 manifest 的
`renderKey/configurationKey + renderViewId` 集合与计划完全一致；独立诊断旧产物时该
参数可省略。

## API

- `GET /health`：返回 catalog 与 publication 版本。
- `GET /api/v1/catalog`：返回 `catalog.mvp.json`。
- `POST /api/v1/renders/resolve`：校验 catalog/publication 版本、车型、四分区
  selections 和渲染视角，返回 canonical key、视角及图片 URL。
- `POST /api/v2/renders/resolve`：派生稳定 `renderKey`；配置 v2 Bake 后按
  `renderKey + renderViewId` 查找 ready 图片并返回 `imageUrl`。
- `GET /assets/renders/:publicationVersion/:vehicleId/:configurationKey/:renderViewId.png`：
  只读取启动时已通过 manifest 校验的 ready PNG。
- `GET /assets/v2/renders/:publicationVersion/:vehicleId/:renderKey/:renderViewId.png`：
  从独立 v2 根安全读取已校验图片，并阻止路径穿越和符号链接逃逸。

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
