import {
  RENDER_VIEW_IDS,
  type Catalog,
  type Health,
  type ResolveRenderRequest,
  type ResolveRenderResponse,
} from './types'

const STABLE_ID_PATTERN = /^[a-z0-9]+(?:-[a-z0-9]+)*$/
const CONFIGURATION_KEY_PATTERN =
  /^paint-[a-z0-9-]+__wheel-[a-z0-9-]+__interior-[a-z0-9-]+__frame-[a-z0-9-]+$/

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

function isCatalog(value: unknown): value is Catalog {
  if (!value || typeof value !== 'object') return false
  const data = value as Partial<Catalog>
  return (
    data.schemaVersion === '1.0.0' &&
    data.currency === 'CNY' &&
    typeof data.catalogVersion === 'string' &&
    !!data.vehicle &&
    Array.isArray(data.parts) &&
    data.parts.length === 4 &&
    data.parts.every((part) => Array.isArray(part.options) && part.options.length > 0) &&
    Array.isArray(data.templates) &&
    Array.isArray(data.renderViews) &&
    data.renderViews.length === 4
  )
}

export async function fetchCatalog(signal?: AbortSignal): Promise<Catalog> {
  const response = await fetch('/api/v1/catalog', {
    headers: { Accept: 'application/json' },
    signal,
  })
  if (!response.ok) {
    throw await createApiError(response, `目录服务返回 ${response.status}`)
  }
  const data: unknown = await response.json()
  if (!isCatalog(data)) {
    throw new Error('目录数据格式不受支持')
  }
  return data
}

function isHealth(value: unknown): value is Health {
  if (!value || typeof value !== 'object') return false
  const data = value as Partial<Health>
  return (
    data.status === 'ok' &&
    typeof data.catalogVersion === 'string' &&
    STABLE_ID_PATTERN.test(data.catalogVersion) &&
    typeof data.publicationVersion === 'string' &&
    STABLE_ID_PATTERN.test(data.publicationVersion)
  )
}

export async function fetchHealth(signal?: AbortSignal): Promise<Health> {
  const response = await fetch('/health', {
    headers: { Accept: 'application/json' },
    signal,
  })
  if (!response.ok) {
    throw await createApiError(response, `健康检查返回 ${response.status}`)
  }
  const data: unknown = await response.json()
  if (!isHealth(data)) {
    throw new Error('健康检查数据格式不受支持')
  }
  return data
}

export async function fetchInitialData(
  signal?: AbortSignal,
): Promise<{ catalog: Catalog; publicationVersion: string }> {
  const healthRequest = fetchHealth(signal)
  const catalogRequest = fetchCatalog(signal)
  const [health, catalog] = await Promise.all([healthRequest, catalogRequest])
  if (health.catalogVersion !== catalog.catalogVersion) {
    throw new ApiError('目录版本与当前发布版本不一致，请重新加载', 409, 'VERSION_CONFLICT')
  }
  return { catalog, publicationVersion: health.publicationVersion }
}

function isResolveRenderResponse(value: unknown): value is ResolveRenderResponse {
  if (!value || typeof value !== 'object') return false
  const data = value as Partial<ResolveRenderResponse>
  return (
    typeof data.configurationKey === 'string' &&
    CONFIGURATION_KEY_PATTERN.test(data.configurationKey) &&
    typeof data.renderViewId === 'string' &&
    RENDER_VIEW_IDS.includes(data.renderViewId as ResolveRenderResponse['renderViewId']) &&
    typeof data.imageUrl === 'string' &&
    data.imageUrl.startsWith('/assets/renders/')
  )
}

export async function resolveRender(
  request: ResolveRenderRequest,
  signal?: AbortSignal,
): Promise<ResolveRenderResponse> {
  const response = await fetch('/api/v1/renders/resolve', {
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
  if (!isResolveRenderResponse(data)) {
    throw new Error('图片解析数据格式不受支持')
  }
  return data
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
