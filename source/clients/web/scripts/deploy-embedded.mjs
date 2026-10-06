import { cp, mkdir, rm, stat } from 'node:fs/promises'
import { createHash } from 'node:crypto'
import { readdir, readFile, writeFile } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url))
const repositoryRoot = path.resolve(scriptDirectory, '..', '..', '..', '..')
const webPackage = path.join(repositoryRoot, 'package', 'clients', 'web')
const catalogSource = path.join(
  repositoryRoot,
  'contracts',
  'fixtures',
  'sc01.catalog.draft.v2.json',
)
const embeddedPackage = path.join(
  repositoryRoot,
  'source',
  'clients',
  'ue',
  'Content',
  'WebUI',
)

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

await rm(embeddedPackage, { recursive: true, force: true })
await mkdir(path.dirname(embeddedPackage), { recursive: true })
await cp(webPackage, embeddedPackage, { recursive: true })

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

const onlineManifest = await manifest(webPackage)
const embeddedManifest = await manifest(embeddedPackage)
if (JSON.stringify(onlineManifest) !== JSON.stringify(embeddedManifest)) {
  throw new Error('Online and embedded Web deployments are not byte-identical')
}

console.log(`Deployed ${onlineManifest.length} identical Web files to online and UE targets`)
