import { fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import App, { STANDALONE_LAYOUT, versionStaticAssetUrl } from './App'
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
      basePriceMinor: 22980000,
      totalPriceMinor: 22980000,
      quoteAllowed: false,
      blockingReasons: ['PRICE_UNCONFIRMED'],
    },
    createdAt: '2026-10-03T00:00:00.000Z',
    updatedAt: '2026-10-03T00:00:00.000Z',
  }
}

function mockApi(options: { v2ImageUrl?: string } = {}) {
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
        ...(options.v2ImageUrl ? { imageUrl: options.v2ImageUrl } : {}),
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
    delete window.ue
    localStorage.clear()
    window.history.replaceState(null, '', '/')
  })

  it('UE 资源版本参数会传递到静态色卡 URL', () => {
    expect(versionStaticAssetUrl(
      '/sc01/thumbnails/leather-p10-1242.webp',
      '?source=ue&assetRevision=4321',
    )).toBe('/sc01/thumbnails/leather-p10-1242.webp?v=4321')
    expect(versionStaticAssetUrl(
      '/sc01/thumbnails/leather-p10-1242.webp?size=512',
      '?assetRevision=build%20two',
    )).toBe('/sc01/thumbnails/leather-p10-1242.webp?size=512&v=build%20two')
    expect(versionStaticAssetUrl(
      '/sc01/thumbnails/leather-p10-1242.webp',
      '?source=web',
    )).toBe('/sc01/thumbnails/leather-p10-1242.webp')
  })

  it('默认独立页使用现代顶栏、紧凑侧栏和烘焙车辆视角', async () => {
    mockApi()
    render(<App />)

    expect(screen.getByText('正在加载 SC01 草案目录…')).toBeInTheDocument()
    expect(await screen.findByRole('heading', { name: 'SC01 定制' })).toBeInTheDocument()
    const categories = screen.getByRole('region', { name: '分类筛选' })
    expect(within(categories).getAllByRole('button')).toHaveLength(4)
    expect(within(categories).getByRole('button', { name: '外饰' })).toHaveAttribute('aria-pressed', 'true')
    expect(within(categories).getByRole('button', { name: '个性化' })).toBeInTheDocument()
    expect(screen.getByText('参考总价')).toBeInTheDocument()
    expect(screen.getByText('¥229,800')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '复位' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '保存' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '分享' })).toBeInTheDocument()
    expect(screen.getByText('未同步更改')).toBeInTheDocument()
    const stageNavigation = screen.getByRole('navigation', { name: '选配阶段' })
    expect(within(stageNavigation).getAllByRole('button').map((button) => button.textContent))
      .toEqual(['01外饰', '02内饰', '03性能', '04个性化'])
    expect(within(stageNavigation).getByRole('button', { name: '外饰' }).querySelector('img'))
      .toBeNull()
    expect(screen.queryByText('DRAFT · 不可报价')).not.toBeInTheDocument()
    expect(document.querySelector('.brand')).toBeNull()
    expect(document.querySelector('.vehicle-title')).toBeNull()
    expect(screen.getByRole('region', { name: '分类筛选' })).toBeInTheDocument()
    expect(screen.getByRole('region', { name: '部件筛选' }))
      .toHaveTextContent('车漆车架轮毂卡钳')
    expect(screen.queryByText(/当前材质|可选材质|款可选/)).not.toBeInTheDocument()
    expect(document.querySelector('.option-card')).toBeNull()
    expect(document.querySelector('.material-family-title')).toBeNull()
    expect(document.querySelectorAll('.color-choice')).toHaveLength(3)
    const shell = document.querySelector<HTMLElement>('.app-shell.standalone')
    expect(STANDALONE_LAYOUT).toEqual({
      headerHeight: 76,
      panelWidth: 480,
      stageMargin: 18,
      stageRadius: 24,
    })
    expect(shell?.style.getPropertyValue('--standalone-header-height')).toBe('76px')
    expect(shell?.style.getPropertyValue('--standalone-panel-width')).toBe('480px')
    expect(shell?.style.getPropertyValue('--standalone-stage-margin')).toBe('18px')
    expect(shell?.style.getPropertyValue('--standalone-stage-radius')).toBe('24px')
    expect(within(screen.getByRole('group', { name: '车辆视角' }))
      .getAllByRole('button')).toHaveLength(4)
    expect(catalogFixture.selectionOrder).toHaveLength(38)
    expect(catalogFixture.surfaces.filter((surface) => !surface.required)).toHaveLength(8)
    expect(Object.keys(initialSelections)).toEqual(
      catalogFixture.selectionOrder.filter((surfaceId) =>
        catalogFixture.options.some(
          (option) => option.surfaceId === surfaceId && option.pricing.isStandard,
        ),
      ),
    )

    await loadProxy()
    expect(screen.getByAltText('SC01 车辆预览')).toHaveAttribute('src', '/assets/renders/v1-proxy.png')
    expect(screen.getByText('当前展示烘焙车辆预览')).toBeInTheDocument()
  })

  it('默认独立页顶栏直接保存、分享并反馈同步状态', async () => {
    const user = userEvent.setup()
    const writeText = vi.fn().mockResolvedValue(undefined)
    Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText } })
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(screen.getByRole('button', { name: /银色.*免费/ }))
    await user.click(screen.getByRole('button', { name: '保存' }))
    expect(await screen.findByText('已同步 · revision 1')).toBeInTheDocument()
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v2/configurations')).toBe(true)

    await user.click(screen.getByRole('button', { name: '分享' }))
    await waitFor(() => {
      expect(writeText).toHaveBeenCalledWith(expect.stringContaining('configuration=cfg-body-cover-silver'))
    })
    expect(screen.getByText('分享链接已复制')).toBeInTheDocument()
  })

  it('独立页复位会清除本地组合与分享参数并重新请求默认左前视角', async () => {
    const user = userEvent.setup()
    const cachedSelections = {
      ...initialSelections,
      'exterior-body-cover': 'body-cover-silver',
    }
    localStorage.setItem('automotive-v2-configurator', JSON.stringify({
      catalog: catalogFixture,
      selections: cachedSelections,
      customizations: {},
    }))
    const fetchMock = mockApi({
      v2ImageUrl: '/assets/v2/renders/review/front-left.png',
    })
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    expect(screen.getByRole('button', { name: /银色.*免费/ }))
      .toHaveAttribute('aria-pressed', 'true')
    await user.click(within(screen.getByRole('group', { name: '车辆视角' }))
      .getByRole('button', { name: '侧面' }))
    window.history.replaceState(null, '', '/?configuration=cfg-old')

    await user.click(screen.getByRole('button', { name: '复位' }))

    expect(screen.getByRole('button', { name: /红色.*免费/ }))
      .toHaveAttribute('aria-pressed', 'true')
    expect(window.location.search).toBe('')
    expect(screen.getByText('已恢复默认配置')).toBeInTheDocument()
    await waitFor(() => {
      const cached = JSON.parse(String(localStorage.getItem('automotive-v2-configurator')))
      expect(cached.selections).toEqual(initialSelections)
      const resolveCalls = fetchMock.mock.calls.filter(
        ([url]) => url === '/api/v2/renders/resolve',
      )
      const request = JSON.parse(String(resolveCalls.at(-1)?.[1]?.body))
      expect(request.selections).toEqual(initialSelections)
      expect(request.customizations).toEqual({})
      expect(request.renderViewId).toBe('front-left')
    })
  })

  it('默认配置下可连续复位并在每次复位后重新显示车辆图片', async () => {
    const user = userEvent.setup()
    const imageUrl = '/assets/v2/renders/review/default/front-left.png'
    const fetchMock = mockApi({ v2ImageUrl: imageUrl })
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await loadProxy()

    const resolveCount = () => fetchMock.mock.calls.filter(
      ([url]) => url === '/api/v2/renders/resolve',
    ).length
    const initialResolveCount = resolveCount()

    await user.click(screen.getByRole('button', { name: '复位' }))
    await waitFor(() => expect(resolveCount()).toBe(initialResolveCount + 1))
    await loadProxy()
    expect(screen.getByAltText('SC01 车辆预览')).toHaveAttribute('src', imageUrl)

    await user.click(screen.getByRole('button', { name: '复位' }))
    await waitFor(() => expect(resolveCount()).toBe(initialResolveCount + 2))
    await loadProxy()
    expect(screen.getByAltText('SC01 车辆预览')).toHaveAttribute('src', imageUrl)
    expect(screen.queryByText('正在加载车辆预览…')).not.toBeInTheDocument()
  })

  it('embedded 模式只展示完整选配区且不请求车辆预览、视角或图片解析', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const fetchMock = mockApi()
    render(<App />)

    expect(await screen.findByRole('heading', { name: 'SC01 定制' })).toBeInTheDocument()
    expect(screen.getByRole('complementary', { name: '车辆选配' })).toBeInTheDocument()
    expect(screen.queryByRole('region', { name: '车辆展示区' })).not.toBeInTheDocument()
    expect(screen.queryByRole('group', { name: '车辆视角' })).not.toBeInTheDocument()
    expect(screen.queryByRole('navigation', { name: '体验控制' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '画质设置' })).not.toBeInTheDocument()
    expect(screen.getByRole('region', { name: '分类筛选' }))
      .toHaveTextContent('外饰内饰性能个性化')
    expect(screen.getByRole('region', { name: '部件筛选' }))
      .toHaveTextContent('车漆车架轮毂卡钳')

    await waitFor(() => {
      expect(fetchMock.mock.calls.some(([url]) => url === '/api/v2/catalog')).toBe(true)
    })
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v1/catalog')).toBe(false)
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v2/renders/resolve')).toBe(false)
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v1/renders/resolve')).toBe(false)
    expect(fetchMock.mock.calls.some(([url]) => url === '/health')).toBe(false)
  })

  it('普通 Web 页面不挂载只供 UE 使用的体验控制层', async () => {
    mockApi()
    render(<App />)

    await screen.findByRole('heading', { name: 'SC01 定制' })
    expect(screen.queryByRole('navigation', { name: '体验控制' })).not.toBeInTheDocument()
  })

  it('普通浏览器即使使用 UE 视图参数也不启用颜色补偿', () => {
    window.history.replaceState(null, '', '/?source=ue&view=header')
    render(<App />)

    expect(document.querySelector('.ue-color-corrected')).toBeNull()
    expect(screen.getByRole('banner')).toBeInTheDocument()
  })

  it('仅在真实 ueBridge 存在时启用颜色补偿', () => {
    window.history.replaceState(null, '', '/?source=ue&view=header')
    window.ue = { uebridge: {} }
    render(<App />)

    expect(document.querySelector('.ue-color-corrected')).not.toBeNull()
    expect(document.querySelector('#ue-srgb-to-linear')).not.toBeNull()
  })

  it('controls 视图请求目录并渲染透明体验控制层', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=controls')
    const fetchMock = mockApi()
    window.ue = {
      uebridge: {
        getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
          cameraId: 'wheel',
          animationEnabled: false,
          lightPreset: 'studio',
          renderMode: 'realtime',
          quality: 'high',
          fullscreen: false,
        })),
        setcamera: vi.fn().mockResolvedValue(true),
        setanimationenabled: vi.fn().mockResolvedValue(true),
        setlightpreset: vi.fn().mockResolvedValue(true),
        setrendermode: vi.fn().mockResolvedValue(true),
        setqualitylevel: vi.fn().mockResolvedValue(true),
        resetpresentation: vi.fn().mockResolvedValue(true),
        setfullscreen: vi.fn().mockResolvedValue(true),
      },
    }
    render(<App />)

    expect(screen.getByRole('navigation', { name: '体验控制' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '画质' })).toBeInTheDocument()
    expect(screen.queryByRole('complementary', { name: '车辆选配' })).not.toBeInTheDocument()
    expect(document.body).toHaveClass('controls-document')
    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      '/api/v2/catalog',
      expect.objectContaining({ headers: { Accept: 'application/json' } }),
    ))
  })

  it('header 视图从目录渲染阶段导航并可切换', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=header')
    const triggerconfiguratorheaderaction = vi.fn().mockResolvedValue(true)
    const setconfiguratorcategory = vi.fn().mockResolvedValue(true)
    window.ue = {
      uebridge: { triggerconfiguratorheaderaction, setconfiguratorcategory },
    }
    const user = userEvent.setup()
    mockApi()
    render(<App />)

    expect(screen.getByRole('heading', { name: 'SC01 定制' })).toBeInTheDocument()
    const navigation = await screen.findByRole('navigation', { name: '选配阶段' })
    expect(within(navigation).getAllByRole('button').map((button) => button.textContent))
      .toEqual(['01外饰', '02内饰', '03性能', '04个性化'])
    expect(within(navigation).getByRole('button', { name: '内饰' }).querySelector('img'))
      .toBeNull()
    await user.click(within(navigation).getByRole('button', { name: '内饰' }))
    expect(setconfiguratorcategory).toHaveBeenCalledWith('interior')

    fireEvent(window, new CustomEvent('ue-configurator-header-state', {
      detail: {
        categoryId: 'exterior',
        referenceTotalMinor: 23940000,
        syncState: 'idle',
        syncMessage: '',
        dirty: true,
        online: true,
      },
    }))
    expect(screen.getByText('¥239,400')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '复位' }))
    await user.click(screen.getByRole('button', { name: '保存' }))
    await user.click(screen.getByRole('button', { name: '分享' }))
    expect(triggerconfiguratorheaderaction.mock.calls).toEqual([
      ['reset'],
      ['save'],
      ['share'],
    ])

    expect(document.body).toHaveClass('header-document')
  })

  it('embedded 右栏响应顶部阶段事件并触发目录阶段镜头', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const setcameraid = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setcameraid } }
    mockApi()
    render(<App />)
    const panel = await screen.findByRole('complementary', { name: '车辆选配' })

    fireEvent(window, new CustomEvent('ue-configurator-category', { detail: 'interior' }))

    expect(await within(screen.getByRole('region', { name: '部件筛选' }))
      .findByRole('button', { name: '方向盘' })).toBeInTheDocument()
    await waitFor(() => expect(setcameraid).toHaveBeenCalledWith('front-cabin'))
    const scrollRegion = panel.querySelector('.panel-scroll')
    const summary = panel.querySelector('.summary')
    expect(scrollRegion).not.toBeNull()
    expect(summary).toBeNull()
    expect(panel.querySelector('.save-bar')).toBeNull()
    expect(panel.querySelector('.canonical')).toBeNull()
  })

  it('embedded bridge 仅把 Web 选配 JSON 同步给 UE v2 状态', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const applyconfigurationjson = vi.fn()
    window.ue = {
      uebridge: { applyconfigurationjson },
    }
    const user = userEvent.setup()
    mockApi()
    render(<App />)

    await screen.findByRole('heading', { name: 'SC01 定制' })
    await waitFor(() => expect(applyconfigurationjson).toHaveBeenCalled())
    expect(screen.queryByRole('navigation', { name: '体验控制' })).not.toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: /银色.*免费/ }))
    await waitFor(() => {
      const payload = JSON.parse(String(applyconfigurationjson.mock.lastCall?.[0]))
      expect(payload).toEqual({
        schemaVersion: '2.0.0',
        selections: {
          ...initialSelections,
          'exterior-body-cover': 'body-cover-silver',
        },
        customizations: {},
      })
    })

    fireEvent(window, new CustomEvent('ue-configurator-header-action', { detail: 'reset' }))
    await waitFor(() => {
      const payload = JSON.parse(String(applyconfigurationjson.mock.lastCall?.[0]))
      expect(payload).toEqual({
        schemaVersion: '2.0.0',
        selections: initialSelections,
        customizations: {},
      })
    })
  })

  it('embedded 节点焦点按 catalog ui 联动 chassis 的 hood 动画', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const playanimation = vi.fn().mockResolvedValue(true)
    const closeanimation = vi.fn().mockResolvedValue(true)
    window.ue = {
      uebridge: {
        applyconfigurationjson: vi.fn(),
        setcameraid: vi.fn().mockResolvedValue(true),
        playanimation,
        closeanimation,
      },
    }
    mockApi()
    render(<App />)

    const parts = await screen.findByRole('region', { name: '部件筛选' })
    await userEvent.click(within(parts).getByRole('button', { name: '车架' }))
    await waitFor(() => expect(playanimation).toHaveBeenCalledWith('hood'))

    await userEvent.click(within(parts).getByRole('button', { name: '轮毂' }))
    await waitFor(() => expect(closeanimation).toHaveBeenCalledWith('hood'))
  })

  it('embedded 页签按目录 cameraId 调用语义 setcameraid', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const setcameraid = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setcameraid } }
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '轮毂' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('wheel'))

    await user.click(within(screen.getByRole('region', { name: '分类筛选' }))
      .getByRole('button', { name: '内饰' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('front-cabin'))

    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('seat'))
  })

  it('分类、部件、子项由目录元数据联动且 wheel layout 为 stack', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await loadProxy()

    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '轮毂' }))
    expect(screen.queryByRole('region', { name: '子项筛选' })).not.toBeInTheDocument()
    expect(screen.getByRole('region', { name: '轮毂材质配置' })).toBeInTheDocument()
    expect(screen.getByRole('region', { name: '轮毂造型配置' })).toBeInTheDocument()
    expect(screen.getByRole('region', { name: '轮毂颜色配置' })).toBeInTheDocument()
    expect(screen.queryByRole('region', { name: '材质系列筛选' })).not.toBeInTheDocument()
    expect(screen.queryByRole('region', { name: '铝合金材质' })).not.toBeInTheDocument()
    expect(screen.queryByRole('region', { name: '镁合金材质' })).not.toBeInTheDocument()
    const magnesiumOption = screen.getByRole('button', { name: /镁合金，/ })
    await user.click(magnesiumOption)
    expect(magnesiumOption)
      .toHaveAttribute('aria-pressed', 'true')

    await user.click(within(screen.getByRole('region', { name: '分类筛选' }))
      .getByRole('button', { name: '内饰' }))
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '方向盘' }))
    expect(await screen.findByText('Black UF7')).toBeInTheDocument()
    expect(screen.getByRole('region', { name: '奥司维材质' })).toHaveTextContent('Black UF7')
    await user.click(screen.getByRole('button', { name: /Black UF7，奥司维，¥1,180/ }))
    expect(screen.getByRole('button', { name: /Black UF7，奥司维，¥1,180/ }))
      .toHaveAttribute('aria-pressed', 'true')

    await user.click(within(screen.getByRole('region', { name: '分类筛选' }))
      .getByRole('button', { name: '个性化' }))
    const personalizationParts = await screen.findByRole('region', { name: '部件筛选' })
    expect(personalizationParts).toHaveTextContent(
      '内饰全车黑色喷漆件门板口袋缝线徽标中面板横饰板换挡刹车脚垫',
    )
    expect(screen.queryByRole('region', { name: '子项筛选' })).not.toBeInTheDocument()
  })

  it('不为未声明 variant 色彩能力的付费材质选项展示色卡', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(within(screen.getByRole('region', { name: '分类筛选' }))
      .getByRole('button', { name: '内饰' }))
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))

    expect(await screen.findByRole('heading', { name: '座椅接触面' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: /Black UF7，奥司维定制/ }))
      .not.toBeInTheDocument()
  })

  it('材料族标题直接选择材质，色卡保持其显式关联的付费 option', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(within(screen.getByRole('region', { name: '分类筛选' }))
      .getByRole('button', { name: '内饰' }))
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '方向盘' }))
    const leatherFamily = within(screen.getByRole('region', { name: '牛皮材质' }))
    await user.click(leatherFamily.getByRole('button', { name: /^牛皮$/ }))
    await user.click(leatherFamily.getByRole('button', { name: /9743 Nero，牛皮，¥1,280/ }))

    expect(leatherFamily.getByRole('button', { name: /9743 Nero，牛皮，¥1,280/ }))
      .toHaveAttribute('aria-pressed', 'true')
  })

  it('自定义车漆显示调色盘和受限参数，并随配置保存', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const user = userEvent.setup()
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    expect(screen.getByRole('button', { name: /红色.*免费/ })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: /银色.*免费/ })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: /自定义车漆.*¥9,600/ }))
    const editor = screen.getByRole('region', { name: '自定义车漆颜色' })
    expect(within(editor).getByLabelText('车漆颜色')).toHaveAttribute('type', 'text')
    expect(within(editor).getByRole('slider', { name: '车漆颜色饱和度和亮度' })).toBeInTheDocument()
    expect(within(editor).queryByLabelText('金属度')).not.toBeInTheDocument()

    fireEvent.change(within(editor).getByLabelText('车漆颜色'), {
      target: { value: '#123456' },
    })
    fireEvent(window, new CustomEvent('ue-configurator-header-action', { detail: 'save' }))
    await waitFor(() => {
      const saveCall = fetchMock.mock.calls.find(([url]) => url === '/api/v2/configurations')
      expect(saveCall).toBeDefined()
      const payload = JSON.parse(String(saveCall?.[1]?.body))
      expect(payload.customizations['exterior-body-cover'].colorHex).toBe('#123456')
    })
  })

  it('车架默认银色免费，PPG 按 custom 能力初始化同款调色盘', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '车架' }))
    const silverOption = screen.getByRole('button', { name: '银色，免费' })
    expect(silverOption).toHaveAttribute('aria-pressed', 'true')
    expect(screen.queryByText(/默认 · 免费|默认 · ¥0|标配/)).not.toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: /自定义颜色\(PPG涂层\)，¥9,600/ }))
    const editor = screen.getByRole('region', { name: '自定义颜色(PPG涂层)颜色' })
    expect(within(editor).getByLabelText('车架颜色')).toHaveValue('#A61D24')
    expect(within(editor).getByRole('slider', { name: '车漆颜色饱和度和亮度' }))
      .toBeInTheDocument()
  })

  it('座椅背板自定义取色使用 option ui 默认参数且不包含专用控件', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(within(screen.getByRole('region', { name: '分类筛选' }))
      .getByRole('button', { name: '内饰' }))
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))
    await user.click(within(screen.getByRole('region', { name: '子项筛选' }))
      .getByRole('button', { name: '背板' }))

    expect(screen.getByRole('button', { name: /高光原色碳纤维，免费/ }))
      .toHaveAttribute('aria-pressed', 'true')
    await user.click(screen.getByRole('button', { name: /自定义取色，¥1,680/ }))
    const editor = screen.getByRole('region', { name: '自定义取色颜色' })
    expect(within(editor).queryByRole('group', { name: '背板表面效果' })).not.toBeInTheDocument()
    expect(screen.getByRole('button', { name: /自定义取色，¥1,680/ }).querySelector('img'))
      .toHaveAttribute('src', '/sc01/option-icons/rainbow.svg')
  })

  it('座椅回中标按四种材料展示免费完整色卡', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(within(screen.getByRole('region', { name: '分类筛选' }))
      .getByRole('button', { name: '内饰' }))
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))
    await user.click(within(screen.getByRole('region', { name: '子项筛选' }))
      .getByRole('button', { name: '回中标' }))

    expect(screen.getByRole('region', { name: '奥司维材质' })).toHaveTextContent('免费')
    expect(screen.getByRole('region', { name: 'Alcantara材质' })).toHaveTextContent('免费')
    expect(screen.getByRole('region', { name: '超纤皮材质' })).toHaveTextContent('免费')
    expect(screen.getByRole('region', { name: '牛皮材质' })).toHaveTextContent('免费')
  })

  it('渲染相关配置变化会刷新代理图并保留上一张直到新图就绪', async () => {
    const user = userEvent.setup()
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await loadProxy()

    await user.click(screen.getByRole('button', { name: /银色/ }))
    await waitFor(() => {
      expect(fetchMock.mock.calls.filter(([url]) => url === '/api/v2/renders/resolve')).toHaveLength(2)
      expect(document.querySelector('.vehicle-image-preload')).not.toBeNull()
    })
    expect(screen.getByAltText('SC01 车辆预览')).toBeInTheDocument()
  })

  it('v2 resolve 返回 imageUrl 时直接使用 v2 Bake 图片且不请求 v1 代理', async () => {
    const imageUrl = '/assets/v2/renders/sc01-v2/sc01/render-default/front-left.png'
    const fetchMock = mockApi({ v2ImageUrl: imageUrl })
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await loadProxy()

    expect(screen.getByAltText('SC01 车辆预览')).toHaveAttribute('src', imageUrl)
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v1/renders/resolve')).toBe(false)
    expect(fetchMock.mock.calls.some(([url]) => url === '/health')).toBe(false)
  })

  it('保存配置并生成可分享链接', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const user = userEvent.setup()
    const writeText = vi.fn().mockResolvedValue(undefined)
    const setconfiguratorheaderstatejson = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setconfiguratorheaderstatejson } }
    Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText } })
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await user.click(screen.getByRole('button', { name: /银色/ }))
    fireEvent(window, new CustomEvent('ue-configurator-header-action', { detail: 'save' }))
    await waitFor(() => {
      const state = JSON.parse(String(setconfiguratorheaderstatejson.mock.lastCall?.[0]))
      expect(state.syncMessage).toBe('已同步 · revision 1')
      expect(state.dirty).toBe(false)
    })
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v2/configurations')).toBe(true)

    fireEvent(window, new CustomEvent('ue-configurator-header-action', { detail: 'share' }))
    await waitFor(() => {
      expect(writeText).toHaveBeenCalledWith(
        expect.stringContaining('configuration=cfg-body-cover-silver'),
      )
    })
  })

  it('离线时显示本地草稿状态并阻止远端保存', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const user = userEvent.setup()
    const setconfiguratorheaderstatejson = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setconfiguratorheaderstatejson } }
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    fireEvent(window, new Event('offline'))

    await user.click(screen.getByRole('button', { name: /银色/ }))
    fireEvent(window, new CustomEvent('ue-configurator-header-action', { detail: 'save' }))
    await waitFor(() => {
      const state = JSON.parse(String(setconfiguratorheaderstatejson.mock.lastCall?.[0]))
      expect(state.online).toBe(false)
      expect(state.syncMessage).toBe('当前离线，草稿已保存在本机，联网后可同步')
    })
    expect(fetchMock.mock.calls.some(([url]) => url === '/api/v2/configurations')).toBe(false)
  })
})
