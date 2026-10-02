import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  fetchCatalog,
  fetchInitialData,
  resolveRender,
} from './api'
import { catalogFixture } from './test/catalogFixture'

const healthFixture = {
  status: 'ok' as const,
  catalogVersion: catalogFixture.catalogVersion,
  publicationVersion: 'publication-v1',
}

function jsonResponse(body: object, status = 200) {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: () => Promise.resolve(body),
  }
}

describe('fetchCatalog', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('从契约路径加载并返回合法目录', async () => {
    const fetchMock = vi.fn().mockResolvedValue(jsonResponse(catalogFixture))
    vi.stubGlobal('fetch', fetchMock)

    await expect(fetchCatalog()).resolves.toEqual(catalogFixture)
    expect(fetchMock).toHaveBeenCalledWith('/api/v1/catalog', {
      headers: { Accept: 'application/json' },
      signal: undefined,
    })
  })

  it('服务错误时提供可读错误', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse({}, 503)))

    await expect(fetchCatalog()).rejects.toThrow('目录服务返回 503')
  })

  it('拒绝不满足四分区契约的数据', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
      ok: true,
      json: () => Promise.resolve({ ...catalogFixture, parts: [] }),
    }))

    await expect(fetchCatalog()).rejects.toThrow('目录数据格式不受支持')
  })
})

describe('fetchInitialData', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('并行请求健康状态和目录并返回 publicationVersion', async () => {
    let releaseHealth!: () => void
    let releaseCatalog!: () => void
    const healthPending = new Promise<void>((resolve) => { releaseHealth = resolve })
    const catalogPending = new Promise<void>((resolve) => { releaseCatalog = resolve })
    const fetchMock = vi.fn((url: string) => {
      if (url === '/health') {
        return healthPending.then(() => jsonResponse(healthFixture))
      }
      return catalogPending.then(() => jsonResponse(catalogFixture))
    })
    vi.stubGlobal('fetch', fetchMock)

    const resultPromise = fetchInitialData()
    expect(fetchMock).toHaveBeenCalledTimes(2)
    expect(fetchMock.mock.calls.map(([url]) => url)).toEqual(['/health', '/api/v1/catalog'])

    releaseCatalog()
    releaseHealth()
    await expect(resultPromise).resolves.toEqual({
      catalog: catalogFixture,
      publicationVersion: 'publication-v1',
    })
  })

  it('健康状态与目录版本不一致时拒绝继续', async () => {
    vi.stubGlobal('fetch', vi.fn((url: string) => Promise.resolve(
      jsonResponse(url === '/health' ? { ...healthFixture, catalogVersion: 'older-v1' } : catalogFixture),
    )))

    await expect(fetchInitialData()).rejects.toMatchObject({
      status: 409,
      code: 'VERSION_CONFLICT',
    })
  })
})

describe('resolveRender', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('按契约提交完整版本、配置和视角，并使用服务端 imageUrl', async () => {
    const response = {
      configurationKey: 'paint-red__wheel-sport__interior-dark__frame-black',
      renderViewId: 'front-left' as const,
      imageUrl: '/assets/renders/publication-v1/demo-car/paint-red__wheel-sport__interior-dark__frame-black/front-left.png',
    }
    const fetchMock = vi.fn().mockResolvedValue(jsonResponse(response))
    vi.stubGlobal('fetch', fetchMock)
    const request = {
      catalogVersion: 'mvp-v1',
      publicationVersion: 'publication-v1',
      vehicleId: 'demo-car',
      selections: {
        paint: 'paint-red',
        wheel: 'wheel-sport',
        interior: 'interior-dark',
        frame: 'frame-black',
      },
      renderViewId: 'front-left' as const,
    }

    await expect(resolveRender(request)).resolves.toEqual(response)
    expect(fetchMock).toHaveBeenCalledWith('/api/v1/renders/resolve', {
      method: 'POST',
      headers: {
        Accept: 'application/json',
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(request),
      signal: undefined,
    })
  })

  it('保留 409 状态和服务端错误信息', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse({
      code: 'VERSION_CONFLICT',
      message: 'publication version changed',
    }, 409)))

    const promise = resolveRender({
      catalogVersion: 'mvp-v1',
      publicationVersion: 'publication-v1',
      vehicleId: 'demo-car',
      selections: {
        paint: 'paint-red',
        wheel: 'wheel-sport',
        interior: 'interior-dark',
        frame: 'frame-black',
      },
      renderViewId: 'front',
    })
    await expect(promise).rejects.toMatchObject({
      message: 'publication version changed',
      status: 409,
      code: 'VERSION_CONFLICT',
    })
  })
})
