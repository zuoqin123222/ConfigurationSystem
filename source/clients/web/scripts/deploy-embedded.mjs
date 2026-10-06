import { createHash, randomUUID } from 'node:crypto'
import { cp, mkdir, readdir, readFile, rename, rm, stat, writeFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

const scriptPath = fileURLToPath(import.meta.url)
const scriptDirectory = path.dirname(scriptPath)
const repositoryRoot = path.resolve(scriptDirectory, '..', '..', '..', '..')
const packageRoot = path.join(repositoryRoot, 'package')
const defaultWebPackage = path.join(packageRoot, 'clients', 'web')
const defaultEmbeddedPackage = path.join(
  repositoryRoot,
  'source',
  'clients',
  'ue',
  'Content',
  'WebUI',
)
const catalogSource = path.join(
  repositoryRoot,
  'contracts',
  'fixtures',
  'sc01.catalog.draft.v2.json',
)

function normalizeForComparison(value) {
  const normalized = path.resolve(value)
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized
}

function isPathWithin(candidate, root) {
  const relative = path.relative(
    normalizeForComparison(root),
    normalizeForComparison(candidate),
  )
  return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative))
}

export function assertSafePaths(webPackage, embeddedPackage) {
  if (!isPathWithin(webPackage, packageRoot)) {
    throw new Error(`Web package must be within the allowed package root: ${packageRoot}`)
  }
  const normalizedTarget = normalizeForComparison(embeddedPackage)
  const normalizedDefaultTarget = normalizeForComparison(defaultEmbeddedPackage)
  const isReleaseStagingSibling = (
    normalizeForComparison(path.dirname(embeddedPackage))
      === normalizeForComparison(path.dirname(defaultEmbeddedPackage))
    && /^\.WebUI\.release-[0-9a-f]{32}$/i.test(path.basename(embeddedPackage))
  )
  const targetAllowed = isPathWithin(embeddedPackage, packageRoot)
    || normalizedTarget === normalizedDefaultTarget
    || isReleaseStagingSibling
  if (!targetAllowed) {
    throw new Error(
      `Embedded package must be within the package root, equal the UE WebUI root, or be its release staging sibling: ${embeddedPackage}`,
    )
  }
  if (
    isPathWithin(webPackage, embeddedPackage)
    || isPathWithin(embeddedPackage, webPackage)
  ) {
    throw new Error('Web package and embedded package must not overlap')
  }
}

async function manifest(root, directory = root) {
  const entries = await readdir(directory, { withFileTypes: true })
  const rows = []
  for (const entry of entries) {
    const entryPath = path.join(directory, entry.name)
    if (entry.isDirectory()) {
      rows.push(...await manifest(root, entryPath))
    } else if (entry.isFile()) {
      rows.push([
        path.relative(root, entryPath).replaceAll('\\', '/'),
        createHash('sha256').update(await readFile(entryPath)).digest('hex'),
      ])
    }
  }
  return rows.sort(([left], [right]) => left.localeCompare(right))
}

export async function deployEmbedded({
  webPackage = defaultWebPackage,
  embeddedPackage = defaultEmbeddedPackage,
  simulateFailureAfterBackup = false,
} = {}) {
  webPackage = path.resolve(webPackage)
  embeddedPackage = path.resolve(embeddedPackage)
  assertSafePaths(webPackage, embeddedPackage)

  const output = await stat(webPackage).catch(() => null)
  if (!output?.isDirectory()) {
    throw new Error(`Web build output does not exist: ${webPackage}`)
  }

  const catalogDirectory = path.join(webPackage, 'catalog')
  await mkdir(catalogDirectory, { recursive: true })
  await cp(catalogSource, path.join(catalogDirectory, 'sc01.catalog.v2.json'))

  // UE CEF 的 file:// 页面不允许加载本地 JS/CSS 子资源。基于同一次 Vite 构建生成
  // 内联入口；在线 Web 仍使用原始 index.html 与内容哈希资源。
  const indexPath = path.join(webPackage, 'index.html')
  const indexHtml = await readFile(indexPath, 'utf8')
  const scriptMatch = indexHtml.match(
    /<script type="module" crossorigin src="\.\/([^"]+)"><\/script>/,
  )
  const styleMatch = indexHtml.match(
    /<link rel="stylesheet" crossorigin href="\.\/([^"]+)">/,
  )
  if (!scriptMatch || !styleMatch) {
    throw new Error('Vite index no longer contains the expected JS/CSS references')
  }
  const script = (await readFile(path.join(webPackage, scriptMatch[1]), 'utf8'))
    .replace(/<\/script/gi, '<\\/script')
  const style = (await readFile(path.join(webPackage, styleMatch[1]), 'utf8'))
    .replace(/<\/style/gi, '<\\/style')
  const embeddedHtml = indexHtml
    .replace(scriptMatch[0], '')
    .replace(styleMatch[0], () => `<style>${style}</style>`)
    .replace('</body>', () => `<script>(()=>{${script}})();</script>\n  </body>`)
  if (embeddedHtml.includes(scriptMatch[0]) || embeddedHtml.includes(styleMatch[0])) {
    throw new Error('Embedded HTML still contains external JS/CSS entry tags')
  }
  await writeFile(path.join(webPackage, 'embedded.html'), embeddedHtml, 'utf8')

  const parent = path.dirname(embeddedPackage)
  const leaf = path.basename(embeddedPackage)
  const nonce = randomUUID().replaceAll('-', '')
  const temporaryPackage = path.join(parent, `.${leaf}.deploy-${nonce}`)
  const backupPackage = path.join(parent, `.${leaf}.backup-${nonce}`)
  const existingTarget = await stat(embeddedPackage).catch(() => null)
  let backedUp = false
  let activated = false

  await mkdir(parent, { recursive: true })
  try {
    await cp(webPackage, temporaryPackage, { recursive: true, errorOnExist: true })
    const onlineManifest = await manifest(webPackage)
    const stagedManifest = await manifest(temporaryPackage)
    if (JSON.stringify(onlineManifest) !== JSON.stringify(stagedManifest)) {
      throw new Error('Online and staged embedded Web deployments are not byte-identical')
    }
    if (existingTarget) {
      await rename(embeddedPackage, backupPackage)
      backedUp = true
    }
    if (simulateFailureAfterBackup) {
      throw new Error('Simulated deployment failure after backup')
    }
    await rename(temporaryPackage, embeddedPackage)
    activated = true

    const embeddedManifest = await manifest(embeddedPackage)
    if (JSON.stringify(onlineManifest) !== JSON.stringify(embeddedManifest)) {
      throw new Error('Online and embedded Web deployments are not byte-identical')
    }
    if (backedUp) {
      await rm(backupPackage, { recursive: true, force: true })
      backedUp = false
    }
    console.log(`Deployed ${onlineManifest.length} identical Web files to online and UE targets`)
    return onlineManifest.length
  } catch (error) {
    if (activated) {
      await rm(embeddedPackage, { recursive: true, force: true })
    }
    if (backedUp) {
      await rename(backupPackage, embeddedPackage)
      backedUp = false
    }
    throw error
  } finally {
    await rm(temporaryPackage, { recursive: true, force: true })
  }
}

function parseArguments(argv) {
  const argumentsByName = new Map()
  for (let index = 0; index < argv.length; index += 2) {
    const name = argv[index]
    const value = argv[index + 1]
    if (!name?.startsWith('--') || value === undefined) {
      throw new Error('Usage: deploy-embedded.mjs [--web-package path] [--embedded-package path]')
    }
    argumentsByName.set(name, value)
  }
  for (const name of argumentsByName.keys()) {
    if (name !== '--web-package' && name !== '--embedded-package') {
      throw new Error(`Unknown argument: ${name}`)
    }
  }
  return {
    webPackage: argumentsByName.get('--web-package') ?? defaultWebPackage,
    embeddedPackage: argumentsByName.get('--embedded-package') ?? defaultEmbeddedPackage,
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === scriptPath) {
  await deployEmbedded(parseArguments(process.argv.slice(2)))
}
