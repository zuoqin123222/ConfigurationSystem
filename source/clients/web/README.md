# Web 车辆选配端

基于 Vite、React 和 TypeScript 的响应式中文汽车选配界面。大屏使用右侧选配面板，手机竖屏使用底部抽屉式面板；展厅背景完全由 CSS/SVG 绘制。

## 功能

- 在线 HTTP(S) 页面从 Server 请求 v2/v1 catalog；UE 的 `file:` 内嵌视图直接读取
  bundle 内 catalog，不发起 health、catalog、configuration 或 render 请求。
- 四个固定分区切换，展示中文名、差价和选项预览图。
- 一键应用两个目录模板。
- 按 `paint → wheel → interior → frame` 固定顺序生成 canonical key。
- 实时计算基础价与所有选项差价的总价。
- 配置或视角变化时调用 `POST /api/v1/renders/resolve`，严格使用响应中的 `imageUrl`，不在客户端拼接渲染地址。
- 图片解析期间展示加载态；404 显示当前配置缺图提示；409 提示重新加载目录。
- 新的解析请求通过 `AbortController` 中止旧请求，并忽略迟到响应。
- 主图和选项预览图加载失败时显示中文占位。
- 奥司维、Alcantara、超纤皮和牛皮的色卡使用按价格分组的紧凑色彩条；数值变化即
  提交并更新实拍预览。多材料区域使用单行材料标签且只挂载当前材料的预览与色卡；
  免费标配色排在首条首位，织布和织物羊毛只在被选中时显示纹理图片卡。
- UI 价格显示完整选装合计，即 `unitPriceMinor × quantity`；例如清单中的
  `2880 × 2` 显示为 `¥5,760`，不再只显示单侧或单件价格。
- 当前材料标签以加粗文字和底部指示线标记；免费默认色使用材料族配置的真实色卡预览，
  但免费状态只属于当前表面的标配 option，不随相同黑色色号跨材料族继承。
- 独立 Web 页严格复用 UE 桌面布局常量：顶部 76 px、右侧面板 480 px、车辆舞台
  18 px 外边距和 24 px 圆角；2560 × 1440 下舞台内容区为 2044 × 1328。
- v2 resolve 返回 `imageUrl` 时直接显示对应 Bake 图片；仅在 v2 图片尚未发布时
  兼容回退到 v1 代理图。
- UE controls 视图提供显式 `Path Tracing` 开关，并仅通过白名单 bridge 切换
  `realtime` / `path-tracing`。
- 分享生成紧凑的 `SC01CFG2.` 自包含配置字符串和高纠错二维码，可从文本或 URL
  `config` 参数导入；读取端兼容旧版 `SC01CFG1.`。页面不保存草稿，启动时始终进入
  默认配置。

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

一次 `npm run build` 将同一构建产物输出到仓库根目录
`package/clients/web/`，并同步到 UE 的 `Content/WebUI/` 供后续 NonUFS 打包。
两个目录均不提交到 Git，也不得手工修改。
