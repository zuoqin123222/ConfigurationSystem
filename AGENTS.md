# ConfigurationSystem Agent Harness

本文件是新会话入口。确定的项目事实和任务路由见
[`docs/HARNESS.md`](docs/HARNESS.md)。

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
node tools/validate-harness.mjs
```

确认分支、HEAD、其他工作树和未提交文件后，只读取任务相关入口：

- 总览：`docs/HARNESS.md`
- 工作流：`docs/AI_WORKFLOW.md`
- 架构与决策：`docs/ARCHITECTURE.md`、`docs/DECISIONS.md`
- 契约：`contracts/README.md`
- UE/Web/Server：各自目录的 `README.md`
- 资产：`docs/REFERENCE_ASSET_POLICY.md`
- 发布：`docs/technical-probes/BAKE_RELEASE_VALIDATION.md`

不得覆盖或提交来源不明的现有改动。

## 控制上下文

先按路径、符号或稳定 ID 定向搜索，不要递归读取全库。默认排除：

- `package/`、`.git/`、`.trae-html-share-packages/`
- `Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`
- `node_modules/`、`dist/`、`coverage/`
- 临时烘焙目录和大批二进制资产

UE 二进制资产的层级、Pivot、材质槽和视觉效果通过探针与 UE5.8 GUI 确认。

## 实施规则

- `contracts/` 是跨端协议和稳定 ID 的唯一来源。
- 跨端变更先修改契约，再修改消费者，最后做集成验证。
- 稳定 ID 不得原地改义；兼容旧配置使用 alias 或版本迁移。
- UE 业务规则优先放在可测试的 C++；Runtime 不依赖 Editor-only 模块。
- React 是选配内容的唯一 UI 实现，UE 不复制目录和价格逻辑。
- `package/` 只保存可重建产物，不作为源码事实或提交内容。
- 不确定的产品、资产或授权信息不得写入 Harness，也不得自行推断。

外部资产必须先确认许可、来源和 SHA-256。车辆接入流程固定为：

```text
授权 FBX → Maya 2025 规范化 → sidecar → 管理员暂存导入 → UE GUI 验收
```

## 验证

- Harness/文档：`node tools/validate-harness.mjs`、`git diff --check`
- 契约：`node tools/validate-contracts.mjs`
- Web：`npm test`、`npm run build`
- Server：`npm test`
- UE C++：编译 `ConfigurationSystemEditor Win64 Development`
- UE 资产和视觉：对应自动化 + UE5.8 GUI
- 发布：Shipping 实机验证 + 图片重烘焙 + manifest/像素校验

编译通过不等于 Cook、Shipping 或视觉验收通过。默认在验证后创建单一目的提交并推送；用户明确要求不提交时除外。

产品事实、架构边界或验证入口变化时，同步更新 `docs/HARNESS.md` 和
`tools/validate-harness.mjs`。
