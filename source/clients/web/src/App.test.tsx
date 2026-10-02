import { fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import App from './App'
import {
  catalogFixture,
  initialSelections,
  legacyCatalogFixture,
} from './test/catalogFixture'

function jsonResponse(body: object, status = 200) {
  return {
    ok: status >= 200 && status < 300,
    status,
    json: () => Promise.resolve(body),
  }
}

function configuration(selections = initialSelections, revision = 1, customizations = {}) {
  return {
    schemaVersion: '2.0.0',
    catalogVersion: catalogFixture.catalogVersion,
    vehicleId: 'sc01',
    configurationId: `cfg-${selections['exterior-body-cover']}`,
    renderKey: `render-${selections['exterior-body-cover']}`,
    selections,
    customizations,
    revision,
    priceResult: {
      totalPriceMinor: null,
      quoteAllowed: false,
      blockingReasons: ['PRICE_UNCONFIRMED'],
    },
    createdAt: '2026-10-03T00:00:00.000Z',
    updatedAt: '2026-10-03T00:00:00.000Z',
  }
}

function mockApi() {
  const fetchMock = vi.fn((url: string, init?: RequestInit) => {
    if (url === '/api/v2/catalog') return Promise.resolve(jsonResponse(catalogFixture))
    if (url === '/api/v1/catalog') return Promise.resolve(jsonResponse(legacyCatalogFixture))
    if (url === '/health') {
      return Promise.resolve(jsonResponse({ publicationVersion: 'publication-v1' }))
    }
    if (url === '/api/v1/renders/resolve') {
      return Promise.resolve(jsonResponse({
        configurationKey: 'v1-proxy',
        renderViewId: 'front-left',
        imageUrl: '/assets/renders/v1-proxy.png',
      }))
    }
    if (url === '/api/v2/renders/resolve') {
      const request = JSON.parse(String(init?.body))
      return Promise.resolve(jsonResponse({
        schemaVersion: '2.0.0',
        catalogVersion: catalogFixture.catalogVersion,
        vehicleId: 'sc01',
        configurationId: `cfg-${request.selections['exterior-body-cover']}`,
        renderKey: `render-${request.selections['exterior-body-cover']}`,
        renderViewId: request.renderViewId,
      }))
    }
    if (url === '/api/v2/configurations') {
      const request = JSON.parse(String(init?.body))
      return Promise.resolve(jsonResponse(
        configuration(request.selections, 1, request.customizations),
        201,
      ))
    }
    if (url.startsWith('/api/v2/configurations/')) {
      const request = init?.body ? JSON.parse(String(init.body)) : null
      return Promise.resolve(jsonResponse(configuration(
        request?.selections ?? initialSelections,
        2,
        request?.customizations ?? {},
      )))
    }
    throw new Error(`unexpected URL: ${url}`)
  })
  vi.stubGlobal('fetch', fetchMock)
  return fetchMock
}

async function loadProxy() {
  const pending = await waitFor(() => {
    const image = document.querySelector<HTMLImageElement>('.vehicle-image-preload')
    expect(image).not.toBeNull()
    return image!
  })
  fireEvent.load(pending)
}

describe('App v2', () => {
  afterEach(() => {
    vi.unstubAllGlobals()
    localStorage.clear()
    window.history.replaceState(null, '', '/')
  })

  it('加载动态层级目录并明确展示 draft、价格待确认和 v1 代理图提示', async () => {
    mockApi()
    render(<App />)

    expect(screen.getByText('正在加载 SC01 草案目录…')).toBeInTheDocument()
    expect(await screen.findByRole('heading', { name: 'SC01' })).toBeInTheDocument()
    expect(screen.getByText('DRAFT · 不可报价')).toBeInTheDocument()
    expect(screen.getByText('价格待确认')).toBeInTheDocument()
    expect(screen.getByRole('region', { name: '类别筛选' })).toHaveTextContent('外观')
    expect(screen.getByRole('region', { name: '组件筛选' })).toHaveTextContent('车身轮毂')
    expect(catalogFixture.selectionOrder).toHaveLength(38)
    expect(catalogFixture.surfaces.every((surface) => surface.required)).toBe(true)
    expect(Object.keys(initialSelections)).toEqual(catalogFixture.selectionOrder)

    await loadProxy()
    expect(screen.getByAltText('SC01 代理车辆')).toHaveAttribute('src', '/assets/renders/v1-proxy.png')
    expect(screen.getByText('v2 暂无渲染图，当前保留 v1 代理图')).toBeInTheDocument()
  })

  it('category/component/surface/materialFamily 联动筛选并选择选项', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01' })
    await loadProxy()

    await user.click(within(screen.getByRole('region', { name: '组件筛选' }))
      .getByRole('button', { name: '轮毂' }))
    expect(screen.getByRole('region', { name: '表面筛选' })).toHaveTextContent('轮毂材质')
    expect(screen.getByRole('region', { name: '材质系列筛选' })).toHaveTextContent('铝合金镁合金')

    await user.click(screen.getByRole('button', { name: /镁合金 · 价格待确认/ }))
    expect(screen.getByText(/wheel-material=wheel-magnesium-alloy/)).toBeInTheDocument()

    await user.click(within(screen.getByRole('region', { name: '类别筛选' }))
      .getByRole('button', { name: '方向盘' }))
    expect(await screen.findByText('Ultrasuede（黑）')).toBeInTheDocument()
    expect(screen.getByRole('region', { name: 'PDF 512 色卡' })).toHaveTextContent('SF4')
  })

  it('自定义车漆显示调色盘和受限参数，并随配置保存', async () => {
    const user = userEvent.setup()
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01' })

    expect(screen.getByRole('button', { name: /红色.*免费/ })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /银色.*免费/ })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: /自定义车漆.*¥9,600/ }))
    const editor = screen.getByRole('region', { name: '自定义车漆参数' })
    expect(within(editor).getByLabelText('车漆颜色')).toHaveAttribute('type', 'color')
    expect(within(editor).getByLabelText('金属度')).toHaveAttribute('min', '0')
    expect(within(editor).getByLabelText('金属度')).toHaveAttribute('max', '1')

    fireEvent.change(within(editor).getByLabelText('车漆颜色'), {
      target: { value: '#123456' },
    })
    await user.click(screen.getByRole('button', { name: '保存配置' }))
    await waitFor(() => {
      const saveCall = fetchMock.mock.calls.find(([url]) => url === '/api/v2/configurations')
      expect(saveCall).toBeDefined()
      const payload = JSON.parse(String(saveCall?.[1]?.body))
      expect(payload.customizations['exterior-body-cover'].colorHex).toBe('#123456')
    })
  })

  it('视角变化会刷新对应代理图，配置中非渲染变化不会触发无意义替换', async () => {
    const user = userEvent.setup()
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01' })
    await loadProxy()

    await user.click(screen.getByRole('button', { name: '侧面' }))
    await waitFor(() => {
      expect(fetchMock.mock.calls.filter(([url]) => url === '/api/v2/renders/resolve')).toHaveLength(2)
    })
    const viewPreload = document.querySelector('.vehicle-image-preload')
    expect(viewPreload).not.toBeNull()
    fireEvent.load(viewPreload!)
    expect(screen.getByAltText('SC01 代理车辆')).toHaveAttribute('src', '/assets/renders/v1-proxy.png')
    expect(screen.getByText('v2 暂无渲染图，当前保留 v1 代理图')).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: /银色/ }))
    await waitFor(() => expect(document.querySelector('.vehicle-image-preload')).not.toBeNull())
    expect(screen.getByAltText('SC01 代理车辆')).toBeInTheDocument()
  })

  it('保存配置并生成可分享链接', async () => {
    const user = userEvent.setup()
    const writeText = vi.fn().mockResolvedValue(undefined)
    Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText } })
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01' })

    await user.click(screen.getByRole('button', { name: /银色/ }))
    await user.click(screen.getByRole('button', { name: '保存配置' }))
    expect(await screen.findByText('已同步 · revision 1')).toBeInTheDocument()
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v2/configurations')).toBe(true)

    await user.click(screen.getByRole('button', { name: '分享配置' }))
    expect(await screen.findByText('分享链接已复制')).toBeInTheDocument()
    expect(writeText).toHaveBeenCalledWith(expect.stringContaining('configuration=cfg-body-cover-silver'))
  })

  it('离线时显示本地草稿状态并阻止远端保存', async () => {
    const user = userEvent.setup()
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01' })
    fireEvent(window, new Event('offline'))

    expect(await screen.findByText('离线 · 本地草稿')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: /银色/ }))
    await user.click(screen.getByRole('button', { name: '保存配置' }))
    expect(screen.getByText('当前离线，草稿已保存在本机，联网后可同步')).toBeInTheDocument()
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v2/configurations')).toBe(false)
  })
})
