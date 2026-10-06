import assert from 'node:assert/strict'
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path, { dirname } from 'node:path'
import test from 'node:test'
import { fileURLToPath } from 'node:url'

import { assertSafePaths, deployEmbedded } from './deploy-embedded.mjs'

const packageRoot = path.resolve(
  dirname(fileURLToPath(import.meta.url)),
  '../../../../package',
)

async function createWebPackage(root) {
  await mkdir(path.join(root, 'assets'), { recursive: true })
  await writeFile(
    path.join(root, 'index.html'),
    '<link rel="stylesheet" crossorigin href="./assets/app.css"><body><script type="module" crossorigin src="./assets/app.js"></script></body>',
  )
  await writeFile(path.join(root, 'assets/app.css'), 'body{color:#fff}')
  await writeFile(path.join(root, 'assets/app.js'), 'globalThis.ready=true')
}

test('拒绝 package 允许根之外的源目录', async () => {
  const source = await mkdtemp(path.join(tmpdir(), 'deploy-source-'))
  const target = path.join(packageRoot, `.deploy-target-${process.pid}-${Date.now()}`)
  try {
    await createWebPackage(source)
    await assert.rejects(
      deployEmbedded({ webPackage: source, embeddedPackage: target }),
      /Web package must be within the allowed package root/,
    )
  } finally {
    await rm(source, { recursive: true, force: true })
    await rm(target, { recursive: true, force: true })
  }
})

test('拒绝源目标相同或互为祖先目录', async () => {
  const source = path.join(packageRoot, `.deploy-overlap-${process.pid}-${Date.now()}`)
  try {
    await createWebPackage(source)
    await assert.rejects(
      deployEmbedded({
        webPackage: source,
        embeddedPackage: path.join(source, 'embedded'),
      }),
      /must not overlap/,
    )
  } finally {
    await rm(source, { recursive: true, force: true })
  }
})

test('允许 release 脚本生成的 UE WebUI 同级 staging 目标', () => {
  const source = path.join(packageRoot, '.web-artifact.release-test', 'online')
  const embeddedStaging = path.resolve(
    dirname(fileURLToPath(import.meta.url)),
    '../../ue/Content/.WebUI.release-0123456789abcdef0123456789abcdef',
  )
  assert.doesNotThrow(() => {
    assertSafePaths(source, embeddedStaging)
  })
})

test('目标替换失败时从同级备份恢复旧目录', async () => {
  const root = path.join(packageRoot, `.deploy-rollback-${process.pid}-${Date.now()}`)
  const source = path.join(root, 'source')
  const target = path.join(root, 'target')
  try {
    await createWebPackage(source)
    await mkdir(target, { recursive: true })
    await writeFile(path.join(target, 'marker.txt'), 'old')
    await assert.rejects(
      deployEmbedded({
        webPackage: source,
        embeddedPackage: target,
        simulateFailureAfterBackup: true,
      }),
      /Simulated deployment failure/,
    )
    assert.equal(await readFile(path.join(target, 'marker.txt'), 'utf8'), 'old')
  } finally {
    await rm(root, { recursive: true, force: true })
  }
})
