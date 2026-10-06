import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const toolsRoot = dirname(fileURLToPath(import.meta.url));
const releaseScript = resolve(toolsRoot, "release.ps1");

function runRelease(args = []) {
  return spawnSync(
    "powershell.exe",
    [
      "-NoLogo",
      "-NoProfile",
      "-NonInteractive",
      "-ExecutionPolicy",
      "Bypass",
      "-File",
      releaseScript,
      ...args,
    ],
    {
      cwd: resolve(toolsRoot, ".."),
      encoding: "utf8",
      windowsHide: true,
    },
  );
}

function runImportedPowerShell(command) {
  const escapedScript = releaseScript.replaceAll("'", "''");
  return spawnSync(
    "powershell.exe",
    [
      "-NoLogo",
      "-NoProfile",
      "-NonInteractive",
      "-ExecutionPolicy",
      "Bypass",
      "-Command",
      `$env:RELEASE_PS1_IMPORT_ONLY='1'; . '${escapedScript}'; ${command}`,
    ],
    { encoding: "utf8", windowsHide: true },
  );
}

test("All dry-run 编排 Web、Server、UE 与 Bake 全链路", () => {
  const result = runRelease(["-DryRun"]);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /targets=UE,ServerWeb,BakeWeb/);
  assert.match(result.stdout, /npm\.cmd test/);
  assert.match(result.stdout, /harness\\validate\.mjs/);
  assert.match(result.stdout, /validate-contracts\.mjs/);
  assert.match(result.stdout, /npm\.cmd exec vite/);
  assert.match(result.stdout, /-gather/);
  assert.match(result.stdout, /RunUAT\.bat" BuildCookRun/);
  assert.match(result.stdout, /generate-published-configurations\.mjs/);
  assert.match(result.stdout, /npm\.cmd run validate:bake/);
  assert.match(result.stdout, /start-server\.ps1/);
  assert.match(result.stdout, /release-manifest\.json target=ServerWeb/);
  assert.match(result.stdout, /release-manifest\.json target=UE/);
  assert.match(result.stdout, /release-manifest\.json target=BakeWeb/);
  assert.match(result.stdout, /atomically promote/);
  assert.equal(
    (result.stdout.match(/npm\.cmd exec vite/g) ?? []).length,
    1,
    "组合目标必须只构建一次 Web artifact",
  );
  assert.match(result.stdout, /reuse shared Web artifact/);
});

test("组合目标只执行选中的 UE 和 ServerWeb", () => {
  const result = runRelease([
    "-Target",
    "UE,ServerWeb",
    "-DryRun",
    "-SkipTests",
  ]);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /targets=UE,ServerWeb/);
  assert.match(result.stdout, /RunUAT\.bat" BuildCookRun/);
  assert.match(result.stdout, /portable bundle without renders/);
  assert.doesNotMatch(result.stdout, /generate-published-configurations\.mjs/);
  assert.doesNotMatch(result.stdout, /npm\.cmd test/);
});

test("ServerWeb 仅在显式 IncludeRenders 时复制 renders", () => {
  const defaultResult = runRelease([
    "-Target", "ServerWeb", "-DryRun", "-SkipTests",
  ]);
  assert.equal(defaultResult.status, 0, defaultResult.stderr);
  assert.match(defaultResult.stdout, /without renders/);

  const includedResult = runRelease([
    "-Target", "ServerWeb", "-DryRun", "-SkipTests", "-IncludeRenders",
  ]);
  assert.equal(includedResult.status, 0, includedResult.stderr);
  assert.match(includedResult.stdout, /including renders/);
});

test("便携启动脚本在无 renders 时回退到内置 bake.valid", () => {
  const source = readFileSync(releaseScript, "utf8");
  assert.match(source, /Test-Path.*package\/renders/);
  assert.match(
    source,
    /\$env:BAKE_ROOT = Join-Path \$PSScriptRoot "package\/contracts\/fixtures\/bake\.valid"/,
  );
});

test("便携启动脚本将配置存储放到用户 LocalAppData 数据目录", () => {
  const source = readFileSync(releaseScript, "utf8");
  assert.match(source, /GetFolderPath\("LocalApplicationData"\)/);
  assert.match(source, /CONFIGURATION_STORE_V2_PATH/);
  assert.match(source, /ConfigurationSystem[\\/]data[\\/]configurations-v2\.json/);
});

test("DryRun 不创建 Bake 输出目录", () => {
  const output = join(
    tmpdir(),
    `configuration-release-dry-run-${process.pid}-${Date.now()}`,
  );
  rmSync(output, { recursive: true, force: true });
  const result = runRelease([
    "-Target", "BakeWeb", "-DryRun", "-SkipTests", "-Output", output,
  ]);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(existsSync(output), false);
});

test("原子晋升失败会恢复旧发布", () => {
  const result = runImportedPowerShell(`
    $root = Join-Path ([IO.Path]::GetTempPath()) ('release-rollback-' + [Guid]::NewGuid().ToString('N'));
    $old = Join-Path $root 'current';
    $new = Join-Path $root 'staging';
    New-Item -ItemType Directory -Path $old,$new -Force | Out-Null;
    [IO.File]::WriteAllText((Join-Path $old 'marker.txt'), 'old');
    [IO.File]::WriteAllText((Join-Path $new 'marker.txt'), 'new');
    try {
      Move-ReleaseDirectory -StagingPath $new -Destination $old -SimulateFailureAfterBackup;
      exit 91;
    } catch {
      if (([IO.File]::ReadAllText((Join-Path $old 'marker.txt'))) -ne 'old') { exit 92 }
      if (-not (Test-Path -LiteralPath $new)) { exit 93 }
    } finally {
      Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
  `);
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
});

test("组合发布事务在后续晋升失败时逆序恢复所有旧发布", () => {
  const result = runImportedPowerShell(`
    $root = Join-Path ([IO.Path]::GetTempPath()) ('release-transaction-' + [Guid]::NewGuid().ToString('N'));
    $first = Join-Path $root 'first';
    $second = Join-Path $root 'second';
    $firstStage = Join-Path $root 'first-stage';
    $secondStage = Join-Path $root 'second-stage';
    New-Item -ItemType Directory -Path $first,$second,$firstStage,$secondStage -Force | Out-Null;
    [IO.File]::WriteAllText((Join-Path $first 'marker.txt'), 'first-old');
    [IO.File]::WriteAllText((Join-Path $second 'marker.txt'), 'second-old');
    [IO.File]::WriteAllText((Join-Path $firstStage 'marker.txt'), 'first-new');
    [IO.File]::WriteAllText((Join-Path $secondStage 'marker.txt'), 'second-new');
    try {
      $items = @(
        [pscustomobject]@{ StagingPath = $firstStage; Destination = $first },
        [pscustomobject]@{ StagingPath = $secondStage; Destination = $second }
      );
      Publish-ReleaseTransaction -Items $items -SimulateFailureAtIndex 1;
      exit 91;
    } catch {
      if (([IO.File]::ReadAllText((Join-Path $first 'marker.txt'))) -ne 'first-old') { exit 92 }
      if (([IO.File]::ReadAllText((Join-Path $second 'marker.txt'))) -ne 'second-old') { exit 93 }
      if (-not (Test-Path -LiteralPath $firstStage)) { exit 94 }
      if (-not (Test-Path -LiteralPath $secondStage)) { exit 95 }
    } finally {
      Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
  `);
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
});

test("release manifest 记录 commit、目标与逐文件 SHA256", () => {
  const root = mkdtempSync(join(tmpdir(), "configuration-release-manifest-"));
  try {
    const payloadPath = join(root, "payload.txt");
    writeFileSync(payloadPath, "payload", "utf8");
    const escapedRoot = root.replaceAll("'", "''");
    const result = runImportedPowerShell(
      `Write-ReleaseManifest -Root '${escapedRoot}' -ReleaseTarget 'ServerWeb'`,
    );
    assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
    const manifest = JSON.parse(
      readFileSync(join(root, "release-manifest.json"), "utf8"),
    );
    assert.match(manifest.gitCommit, /^[0-9a-f]{40}$/);
    assert.match(manifest.generatedAtUtc, /^\d{4}-\d{2}-\d{2}T/);
    assert.equal(typeof manifest.dirty, "boolean");
    assert.equal(manifest.target, "ServerWeb");
    assert.deepEqual(manifest.files.map(({ path }) => path), ["payload.txt"]);
    assert.match(manifest.files[0].sha256, /^[0-9a-f]{64}$/);
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test("BakeWeb 透传 profile、mode、shard、publication、input 和 output", () => {
  const result = runRelease([
    "-Target",
    "BakeWeb",
    "-DryRun",
    "-SkipTests",
    "-Profile",
    "debug",
    "-Mode",
    "shard",
    "-Shard",
    "1/4",
    "-Publication",
    "sc01-candidate",
    "-Input",
    "contracts/fixtures/sc01.catalog.draft.v2.json",
    "-Output",
    "staging/custom-bake",
  ]);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /--mode shard --shard 1\/4/);
  assert.match(result.stdout, /sc01-candidate/);
  assert.match(result.stdout, /ConfigurationBakeProfile=debug/);
  assert.match(result.stdout, /ConfigurationBakeInput=.*published-configurations\.json/);
  assert.match(result.stdout, /ConfigurationBakeStaging=.*custom-bake/);
});

test("BakeWeb estimate 只生成估算，不误跑 UE Bake 或校验", () => {
  const result = runRelease([
    "-Target",
    "BakeWeb",
    "-DryRun",
    "-SkipTests",
    "-Mode",
    "estimate",
  ]);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /--mode estimate/);
  assert.match(result.stdout, /UE Bake and validation are skipped/);
  assert.doesNotMatch(result.stdout, /UnrealEditor\.exe/);
  assert.doesNotMatch(result.stdout, /validate:bake/);
});

test("拒绝未知目标", () => {
  const result = runRelease(["-Target", "Unknown", "-DryRun"]);
  assert.notEqual(result.status, 0);
  assert.match(`${result.stdout}\n${result.stderr}`, /Unknown Target/);
});

test("拒绝越界 shard", () => {
  const result = runRelease([
    "-Target",
    "BakeWeb",
    "-DryRun",
    "-Mode",
    "shard",
    "-Shard",
    "2/2",
  ]);
  assert.notEqual(result.status, 0);
  assert.match(`${result.stdout}\n${result.stderr}`, /index.*count/);
});

test("拒绝在非 ServerWeb 目标使用 IncludeRenders", () => {
  const result = runRelease([
    "-Target",
    "BakeWeb",
    "-DryRun",
    "-IncludeRenders",
  ]);
  assert.notEqual(result.status, 0);
  assert.match(`${result.stdout}\n${result.stderr}`, /only valid.*ServerWeb/);
});

test("拒绝把 Bake 输出写入源码或仓库根目录", () => {
  for (const output of [".", "source/release-bake", "contracts/release-bake"]) {
    const result = runRelease([
      "-Target", "BakeWeb", "-DryRun", "-SkipTests", "-Output", output,
    ]);
    assert.notEqual(result.status, 0, output);
    assert.match(
      `${result.stdout}\n${result.stderr}`,
      /must not replace (?:protected repository content|the repository root)/,
    );
  }
});

test("拒绝覆盖无 ownership sentinel 的仓库外既有 Bake 目录", () => {
  const result = runImportedPowerShell(`
    $root = Join-Path ([IO.Path]::GetTempPath()) ('foreign-bake-' + [Guid]::NewGuid().ToString('N'));
    New-Item -ItemType Directory -Path $root -Force | Out-Null;
    try {
      Assert-SafeBakeDestination -Destination $root;
      exit 91;
    } catch {
      if ($_.Exception.Message -notmatch 'ownership sentinel') { exit 92 }
    } finally {
      Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
  `);
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
});

test("允许覆盖带 ownership sentinel 的仓库外既有 Bake 目录", () => {
  const result = runImportedPowerShell(`
    $root = Join-Path ([IO.Path]::GetTempPath()) ('owned-bake-' + [Guid]::NewGuid().ToString('N'));
    New-Item -ItemType Directory -Path $root -Force | Out-Null;
    [IO.File]::WriteAllText((Join-Path $root '.configuration-system-bake-output'), 'owned');
    try {
      Assert-SafeBakeDestination -Destination $root;
    } finally {
      Remove-Item -LiteralPath $root -Recurse -Force -ErrorAction SilentlyContinue
    }
  `);
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
});
