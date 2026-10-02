import {
  RENDER_VIEW_IDS,
  type CatalogV2,
  type ConfigurationV2,
  type LegacyCatalog,
  type LegacyRender,
  type RenderViewId,
  type ResolveRenderV2Response,
  type SaveConfigurationRequest,
  type Selections,
} from './types'

export class ApiError extends Error {
  constructor(
    message: string,
    readonly status: number,
    readonly code?: string,
  ) {
    super(message)
    this.name = 'ApiError'
  }
}

function isCatalogV2(value: unknown): value is CatalogV2 {
  if (!value || typeof value !== 'object') return false
  const data = value as Partial<CatalogV2>
  return (
    data.schemaVersion === '2.0.0' &&
    data.lifecycle === 'draft' &&
    data.currency === 'CNY' &&
    typeof data.catalogVersion === 'string' &&
    !!data.vehicle &&
    data.vehicle.basePriceMinor === null &&
    data.vehicle.quotable === false &&
    Array.isArray(data.selectionOrder) &&
    data.selectionOrder.length > 0 &&
    Array.isArray(data.categories) &&
    Array.isArray(data.components) &&
    Array.isArray(data.surfaces) &&
    Array.isArray(data.materialFamilies) &&
    Array.isArray(data.options) &&
    data.selectionOrder.every((surfaceId) =>
      data.surfaces?.some((surface) => surface.surfaceId === surfaceId) &&
      data.options?.some((option) => option.surfaceId === surfaceId),
    )
  )
}

export async function fetchCatalog(signal?: AbortSignal): Promise<CatalogV2> {
  const response = await fetch('/api/v2/catalog', {
    headers: { Accept: 'application/json' },
    signal,
  })
  if (!response.ok) {
    throw await createApiError(response, `目录服务返回 ${response.status}`)
  }
  const data: unknown = await response.json()
  if (!isCatalogV2(data)) {
    throw new Error('目录数据格式不受支持')
  }
  return data
}

export async function fetchLegacyCatalog(signal?: AbortSignal): Promise<LegacyCatalog | null> {
  const response = await fetch('/api/v1/catalog', {
    headers: { Accept: 'application/json' },
    signal,
  })
  if (!response.ok) return null
  const data: unknown = await response.json()
  if (!data || typeof data !== 'object') return null
  const catalog = data as Partial<LegacyCatalog>
  return Array.isArray(catalog.parts) && Array.isArray(catalog.renderViews)
    ? catalog as LegacyCatalog
    : null
}

export async function fetchInitialData(
  signal?: AbortSignal,
): Promise<{ catalog: CatalogV2; legacyCatalog: LegacyCatalog | null }> {
  const [catalog, legacyCatalog] = await Promise.all([
    fetchCatalog(signal),
    fetchLegacyCatalog(signal).catch(() => null),
  ])
  return { catalog, legacyCatalog }
}

function isResolveRenderV2Response(value: unknown): value is ResolveRenderV2Response {
  if (!value || typeof value !== 'object') return false
  const data = value as Partial<ResolveRenderV2Response>
  return (
    data.schemaVersion === '2.0.0' &&
    typeof data.configurationId === 'string' &&
    typeof data.renderKey === 'string' &&
    (data.renderViewId === undefined ||
      RENDER_VIEW_IDS.includes(data.renderViewId as RenderViewId)) &&
    (data.imageUrl === undefined || typeof data.imageUrl === 'string')
  )
}

export async function resolveRender(
  request: { catalogVersion: string; vehicleId: string; selections: Selections; renderViewId?: string },
  signal?: AbortSignal,
): Promise<ResolveRenderV2Response> {
  const response = await fetch('/api/v2/renders/resolve', {
    method: 'POST',
    headers: {
      Accept: 'application/json',
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(request),
    signal,
  })
  if (!response.ok) {
    throw await createApiError(response, `图片解析服务返回 ${response.status}`)
  }
  const data: unknown = await response.json()
  if (!isResolveRenderV2Response(data)) {
    throw new Error('图片解析数据格式不受支持')
  }
  return data
}

function isConfiguration(value: unknown): value is ConfigurationV2 {
  if (!value || typeof value !== 'object') return false
  const data = value as Partial<ConfigurationV2>
  return (
    data.schemaVersion === '2.0.0' &&
    typeof data.configurationId === 'string' &&
    typeof data.renderKey === 'string' &&
    typeof data.revision === 'number' &&
    !!data.selections &&
    !!data.priceResult &&
    data.priceResult.totalPriceMinor === null &&
    data.priceResult.quoteAllowed === false
  )
}

export async function fetchConfiguration(
  configurationId: string,
  signal?: AbortSignal,
): Promise<ConfigurationV2> {
  const response = await fetch(`/api/v2/configurations/${encodeURIComponent(configurationId)}`, {
    headers: { Accept: 'application/json' },
    signal,
  })
  if (!response.ok) {
    throw await createApiError(response, `配置读取返回 ${response.status}`)
  }
  const data: unknown = await response.json()
  if (!isConfiguration(data)) throw new Error('配置数据格式不受支持')
  return data
}

export async function saveConfiguration(
  request: SaveConfigurationRequest,
  signal?: AbortSignal,
): Promise<ConfigurationV2> {
  const updating = !!request.configurationId && request.revision !== undefined
  const response = await fetch(
    updating
      ? `/api/v2/configurations/${encodeURIComponent(request.configurationId!)}`
      : '/api/v2/configurations',
    {
      method: updating ? 'PUT' : 'POST',
      headers: {
        Accept: 'application/json',
        'Content-Type': 'application/json',
        ...(updating ? {} : { 'Idempotency-Key': createIdempotencyKey(request) }),
      },
      body: JSON.stringify(request),
      signal,
    },
  )
  if (!response.ok) {
    throw await createApiError(response, `配置保存返回 ${response.status}`)
  }
  const data: unknown = await response.json()
  if (!isConfiguration(data)) throw new Error('配置数据格式不受支持')
  return data
}

function createIdempotencyKey(request: SaveConfigurationRequest): string {
  const input = `${request.catalogVersion}:${request.vehicleId}:${JSON.stringify(request.selections)}`
  let hash = 2166136261
  for (let index = 0; index < input.length; index += 1) {
    hash ^= input.charCodeAt(index)
    hash = Math.imul(hash, 16777619)
  }
  return `web-${(hash >>> 0).toString(16)}`
}

export async function resolveLegacyProxy(
  catalog: LegacyCatalog,
  renderViewId: RenderViewId,
  signal?: AbortSignal,
): Promise<LegacyRender | null> {
  const selections = Object.fromEntries(
    catalog.parts.map((part) => [part.partId, part.options[0]?.optionId]),
  )
  if (Object.values(selections).some((optionId) => !optionId)) return null
  const healthResponse = await fetch('/health', { headers: { Accept: 'application/json' }, signal })
  if (!healthResponse.ok) return null
  const health = await healthResponse.json() as { publicationVersion?: unknown }
  if (typeof health.publicationVersion !== 'string') return null
  const response = await fetch('/api/v1/renders/resolve', {
    method: 'POST',
    headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
    body: JSON.stringify({
      catalogVersion: catalog.catalogVersion,
      publicationVersion: health.publicationVersion,
      vehicleId: catalog.vehicle.vehicleId,
      selections,
      renderViewId,
    }),
    signal,
  })
  if (!response.ok) return null
  const data = await response.json() as Partial<LegacyRender>
  return typeof data.imageUrl === 'string' ? data as LegacyRender : null
}

async function createApiError(response: Response, fallback: string): Promise<ApiError> {
  try {
    const data: unknown = await response.json()
    if (data && typeof data === 'object') {
      const error = data as { code?: unknown; message?: unknown }
      if (typeof error.message === 'string') {
        return new ApiError(
          error.message,
          response.status,
          typeof error.code === 'string' ? error.code : undefined,
        )
      }
    }
  } catch {
    // 非 JSON 错误响应使用稳定的本地提示。
  }
  return new ApiError(fallback, response.status)
}
