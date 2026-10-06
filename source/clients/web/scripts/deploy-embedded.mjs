import { cp, mkdir, rm, stat } from 'node:fs/promises'
import { createHash } from 'node:crypto'
import { readdir, readFile } from 'node:fs/promises'
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
