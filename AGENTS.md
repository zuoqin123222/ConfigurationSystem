# Agent Bootstrap

本文件只负责入口发现，项目规则统一维护在 `harness/`。

1. 开始任何任务前先读取 `harness/AGENTS.md`，并按其中的任务路由读取专项规则。
2. 执行打包、发布、Cook、Stage、Shipping 或 Bake 前，必须重新读取最新的
   `harness/UE.md` 和 `docs/technical-probes/UNIFIED_RELEASE_PIPELINE.md`。
3. 正式发布只能从仓库根目录调用 `tools/release.ps1`，先运行 `-DryRun`；
   禁止用临时 `RunUAT`、`BuildCookRun` 或零散 `npm run build` 替代统一发布入口。
4. 长时间构建前后都要复核 Git HEAD 与工作区状态；若构建期间发生变化（HEAD 或工作树），
   当前产物作废并重新发布。
