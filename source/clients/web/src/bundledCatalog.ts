import catalogJson from '../../../../contracts/fixtures/sc01.catalog.draft.v2.json'
import { isCatalogV2 } from './api'
import type { CatalogV2 } from './types'

/**
 * 与源码一起打入 Web bundle 的只读目录。UE 的 file:// 页面只使用此值；
 * 在线页面仍通过 Server 获取目录，避免改变线上发布语义。
 */
if (!isCatalogV2(catalogJson)) {
  throw new Error('包内目录数据格式不受支持')
}

export const bundledCatalog: CatalogV2 = catalogJson

export function usesBundledCatalog(protocol = window.location.protocol): boolean {
  return protocol === 'file:'
}

export function resolveStaticAssetUrl(
  url: string | null | undefined,
  search = window.location.search,
  protocol = window.location.protocol,
  baseUri = document.baseURI,
): string | undefined {
  if (!url || /^(?:data|blob):/i.test(url)) return url ?? undefined
  const resolvedUrl = protocol === 'file:' && url.startsWith('/')
    ? new URL(url.slice(1), baseUri).toString()
    : url
  const revision = new URLSearchParams(search).get('assetRevision')
  if (!revision) return resolvedUrl
  const separator = resolvedUrl.includes('?') ? '&' : '?'
  return `${resolvedUrl}${separator}v=${encodeURIComponent(revision)}`
}
