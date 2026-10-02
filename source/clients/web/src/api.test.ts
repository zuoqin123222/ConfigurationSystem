import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  fetchCatalog,
  fetchConfiguration,
  fetchInitialData,
  resolveRender,
  saveConfiguration,
} from './api'
import { catalogFixture, legacyCatalogFixture } from './test/catalogFixture'

function jsonResponse(body: object, status = 200) {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: () => Promise.resolve(body),
  }
}

const selections = {
  'exterior-body-cover': 'body-cover-red',
  'wheel-material': 'wheel-aluminum-alloy',
  'steering-wheel-skin': 'steering-skin-ultrasuede-black',
}

const storedConfiguration = {
  schemaVersion: '2.0.0',
  catalogVersion: catalogFixture.catalogVersion,
  vehicleId: 'sc01',
  configurationId: 'cfg-123456789012345678901234',
  renderKey: 'sc01__draft__render-123456789012345678901234',
  selections,
  revision: 1,
  priceResult: { totalPriceMinor: null, quoteAllowed: false, blockingReasons: ['PRICE_UNCONFIRMED'] },
  createdAt: '2026-10-03T00:00:00.000Z',
  updatedAt: '2026-10-03T00:00:00.000Z',
} as const

describe('v2 API', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('从 /api/v2/catalog 加载动态草案目录', async () => {
    const fetchMock = vi.fn().mockResolvedValue(jsonResponse(catalogFixture))
    vi.stubGlobal('fetch', fetchMock)

    await expect(fetchCatalog()).resolves.toEqual(catalogFixture)
    expect(fetchMock).toHaveBeenCalledWith('/api/v2/catalog', {
      headers: { Accept: 'application/json' },
      signal: undefined,
    })
  })

  it('主目录成功时允许 v1 代理目录不可用', async () => {
    vi.stubGlobal('fetch', vi.fn((url: string) => Promise.resolve(
      url === '/api/v2/catalog'
        ? jsonResponse(catalogFixture)
        : jsonResponse({}, 404),
    )))

    await expect(fetchInitialData()).resolves.toEqual({
      catalog: catalogFixture,
      legacyCatalog: null,
    })
  })

  it('拒绝缺少必选表面选项的目录', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(jsonResponse({
      ...catalogFixture,
      options: catalogFixture.options.filter((option) => option.surfaceId !== 'wheel-material'),
    })))

    await expect(fetchCatalog()).rejects.toThrow('目录数据格式不受支持')
  })

  it('解析 v2 renderKey 且允许响应不含图片', async () => {
    const response = {
      schemaVersion: '2.0.0',
      catalogVersion: catalogFixture.catalogVersion,
      vehicleId: 'sc01',
      configurationId: storedConfiguration.configurationId,
      renderKey: storedConfiguration.renderKey,
      renderViewId: 'front-left',
    }
    const fetchMock = vi.fn().mockResolvedValue(jsonResponse(response))
    vi.stubGlobal('fetch', fetchMock)

    await expect(resolveRender({
      catalogVersion: catalogFixture.catalogVersion,
      vehicleId: 'sc01',
      selections,
      renderViewId: 'front-left',
    })).resolves.toEqual(response)
    expect(fetchMock.mock.calls[0][0]).toBe('/api/v2/renders/resolve')
  })

  it('创建配置使用幂等键，更新配置携带 revision', async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(jsonResponse(storedConfiguration, 201))
      .mockResolvedValueOnce(jsonResponse({ ...storedConfiguration, revision: 2 }))
    vi.stubGlobal('fetch', fetchMock)

    await saveConfiguration({
      catalogVersion: catalogFixture.catalogVersion,
      vehicleId: 'sc01',
      selections,
    })
    expect(fetchMock.mock.calls[0][0]).toBe('/api/v2/configurations')
    expect(fetchMock.mock.calls[0][1].headers['Idempotency-Key']).toMatch(/^web-/)

    await saveConfiguration({
      catalogVersion: catalogFixture.catalogVersion,
      vehicleId: 'sc01',
      selections,
      configurationId: storedConfiguration.configurationId,
      revision: 1,
    })
    expect(fetchMock.mock.calls[1][0]).toContain(storedConfiguration.configurationId)
    expect(fetchMock.mock.calls[1][1].method).toBe('PUT')
  })

  it('读取分享配置并保留服务端错误', async () => {
    vi.stubGlobal('fetch', vi.fn()
      .mockResolvedValueOnce(jsonResponse(storedConfiguration))
      .mockResolvedValueOnce(jsonResponse({ code: 'CONFIGURATION_NOT_FOUND', message: '配置不存在' }, 404)))

    await expect(fetchConfiguration(storedConfiguration.configurationId)).resolves.toEqual(storedConfiguration)
    await expect(fetchConfiguration('missing')).rejects.toMatchObject({
      status: 404,
      code: 'CONFIGURATION_NOT_FOUND',
      message: '配置不存在',
    })
  })
})

describe('fixture', () => {
  it('保留可用的 v1 代理目录测试数据', () => {
    expect(legacyCatalogFixture.parts).toHaveLength(4)
  })
})
