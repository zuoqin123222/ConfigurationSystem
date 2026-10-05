# ConfigurationSystem Harness

本目录是 AI 新会话的统一入口。项目确定事实见
[`PROJECT.md`](PROJECT.md)，UE、FBX、资产目录和打包规则见
[`UE.md`](UE.md)。

## 优先级

冲突时依次采用：

1. 用户当前指令。
2. 当前工作树的代码、契约、测试和运行结果。
3. 最新且有证据的项目记录。
4. 未被取代的 ADR 和项目文档。

其他对话的结论必须先在当前工作树中核对，不能直接视为已合入。

## 启动检查

```powershell
git status --short --branch
git worktree list
git log --all --date-order -n 20 --oneline --decorate
node harness/validate.mjs
```

确认分支、HEAD、其他工作树和未提交文件后，只读取任务相关内容。不得覆盖或提交来源不明的现有改动。

## 任务触发路由

| 任务关键词 | 必须读取 |
|---|---|
| SC01 产品、价格、选项、UI | `harness/PROJECT.md`、`contracts/README.md` |
| UE、C++、Content、地图、材质、相机、动画 | `harness/UE.md`、`source/clients/ue/README.md` |
| FBX、Maya、骨骼、Pivot、LOD、材质槽 | `harness/UE.md`、`docs/DCC_VEHICLE_MODELING_EXPORT_GUIDE.md`、`docs/VEHICLE_ASSET_REQUIREMENTS.md` |
| 外部资产、授权、代理车辆 | `harness/UE.md`、`docs/REFERENCE_ASSET_POLICY.md` |
| Cook、Stage、Shipping、打包 | `harness/UE.md`、`docs/technical-probes/PACKAGING_BOUNDARY_PROBE.md` |
| Bake、图片、publication、发布 | `harness/UE.md`、`docs/technical-probes/BAKE_RELEASE_VALIDATION.md` |
| Web | `harness/PROJECT.md`、`source/clients/web/README.md` |
| Server、API | `harness/PROJECT.md`、`source/server/README.md` |

## 控制上下文

先按路径、符号或稳定 ID 定向搜索，不要递归读取全库。默认排除：

- `package/`、`.git/`、`.trae-html-share-packages/`
- `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`
- `node_modules/`、`dist/`、`coverage/`
- 临时烘焙目录和大批二进制资产

UE 二进制资产的层级、Pivot、材质槽和视觉效果通过探针与 UE5.8 GUI 确认。

## 实施与验证

- `contracts/` 是跨端协议和稳定 ID 的唯一来源。
- 跨端变更按“契约 → 消费者 → 集成验证”执行。
- 稳定 ID 不得原地改义；兼容旧配置使用 alias 或版本迁移。
- UE 业务规则优先放在可测试的 C++；Runtime 不依赖 Editor-only 模块。
- React 是选配内容的唯一 UI 实现，UE 不复制目录和价格逻辑。
- `package/` 只保存可重建产物，不作为源码事实或提交内容。
- 不确定的产品、资产或授权信息不得写入 Harness，也不得自行推断。

最低验证：

- Harness/文档：`node harness/validate.mjs`、`git diff --check`
- 契约：`node tools/validate-contracts.mjs`
- Web：`npm test`、`npm run build`
- Server：`npm test`
- UE：Editor 编译、对应自动化、必要的 UE5.8 GUI 验收
- 发布：Shipping 实机、Web build、Bake 与 manifest/像素校验

编译通过不等于 Cook、Shipping 或视觉验收通过。默认在验证后创建单一目的提交并推送；用户明确要求不提交时除外。
