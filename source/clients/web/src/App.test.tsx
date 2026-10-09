import { readFileSync } from 'node:fs'
import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import App, { STANDALONE_LAYOUT, versionStaticAssetUrl } from './App'
import {
  createPortableConfiguration,
} from './portableConfiguration'
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

async function enterOptions() {
  const nextButton = screen.queryByRole('button', { name: '下一步' })
  if (nextButton) await userEvent.click(nextButton)
}

async function selectStage(name: string) {
  await enterOptions()
  await userEvent.click(within(screen.getByRole('navigation', { name: '选配阶段' }))
    .getByRole('button', { name }))
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

  it('默认预设仅选中时使用双图高预览，未选中时收敛为单图', () => {
    const styles = readFileSync('src/styles.css', 'utf8')
    expect(styles).toMatch(/\.preset-card-visual\s*\{[^}]*grid-template-rows:\s*minmax\(0,\s*1fr\)[^}]*height:\s*170px/s)
    expect(styles).toMatch(/\.preset-card\.selected \.preset-card-visual\s*\{[^}]*grid-template-rows:\s*repeat\(2,\s*minmax\(0,\s*1fr\)\)[^}]*height:\s*344px/s)
    expect(styles).toMatch(/\.preset-card-visual img\s*\{[^}]*height:\s*100%[^}]*object-fit:\s*cover/s)
    expect(styles).toMatch(/@media \(max-width:\s*520px\)\s*\{[\s\S]*?\.preset-card\.selected \.preset-card-visual\s*\{[^}]*height:\s*clamp\(284px,\s*84vw,\s*344px\)/)
  })

  it('默认预设选中显示内外双图，修改配置后仅保留外观单图', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)

    await screen.findByRole('heading', { name: 'SC01 定制' })
    const defaultPreset = screen.getByRole('button', { name: '默认配置' })
    expect(defaultPreset.querySelectorAll('.preset-card-visual img')).toHaveLength(2)

    await user.click(screen.getByRole('button', { name: '下一步' }))
    await user.click(screen.getByRole('button', { name: /银色.*免费/ }))
    await user.click(within(screen.getByRole('navigation', { name: '选配阶段' }))
      .getByRole('button', { name: '预设' }))

    const unselectedDefaultPreset = screen.getByRole('button', { name: '默认配置' })
    expect(unselectedDefaultPreset).toHaveAttribute('aria-pressed', 'false')
    expect(unselectedDefaultPreset.querySelectorAll('.preset-card-visual img')).toHaveLength(1)
    expect(unselectedDefaultPreset.querySelector('img'))
      .toHaveAttribute('src', '/sc01/presets/default-exterior.webp')
  })

  it('默认独立页使用现代顶栏、紧凑侧栏和烘焙车辆视角', async () => {
    mockApi()
    render(<App />)

    expect(screen.getByText('正在加载 SC01 草案目录…')).toBeInTheDocument()
    expect(await screen.findByRole('heading', { name: 'SC01 定制' })).toBeInTheDocument()
    expect(screen.queryByRole('region', { name: '分类筛选' })).not.toBeInTheDocument()
    expect(screen.getByText('参考总价')).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '复位' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '存草稿' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '分享' })).not.toBeInTheDocument()
    expect(screen.queryByText(/草稿|未保存更改|已保存/)).not.toBeInTheDocument()
    const stageNavigation = screen.getByRole('navigation', { name: '选配阶段' })
    expect(within(stageNavigation).getAllByRole('button').map((button) => button.textContent))
      .toEqual(['01预设', '02外饰', '03内饰', '04性能', '05个性化', '06总览'])
    expect(screen.getByRole('region', { name: '预设配置' })).toBeInTheDocument()
    const defaultPreset = screen.getByRole('button', { name: '默认配置' })
    expect(defaultPreset).toHaveAttribute('aria-pressed', 'true')
    const presetVisual = defaultPreset.querySelector('.preset-card-visual')
    expect(presetVisual).toBeInstanceOf(HTMLSpanElement)
    expect(presetVisual?.children).toHaveLength(2)
    expect(Array.from(presetVisual?.querySelectorAll('img') ?? []).map((image) => image.getAttribute('src')))
      .toEqual([
        '/sc01/presets/default-exterior.webp',
        '/sc01/presets/default-interior.webp',
      ])
    expect(within(defaultPreset).getByText('¥229,800')).toBeInTheDocument()
    expect(within(document.querySelector('.panel-reference-total') as HTMLElement)
      .getByText('¥229,800')).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '导入配置' })).toHaveTextContent('＋')
    await userEvent.click(screen.getByRole('button', { name: '下一步' }))
    expect(within(stageNavigation).getByRole('button', { name: '外饰' }).querySelector('img'))
      .toBeNull()
    expect(screen.queryByText('DRAFT · 不可报价')).not.toBeInTheDocument()
    expect(document.querySelector('.brand')).toBeNull()
    expect(document.querySelector('.vehicle-title')).toBeNull()
    expect(screen.getByRole('region', { name: '部件筛选' }))
      .toHaveTextContent('车漆车架轮毂卡钳')
    expect(screen.getByRole('region', { name: '部件筛选' }).querySelector('img')).toBeNull()
    const redSwatch = screen.getByRole('button', { name: /红色，免费/ })
      .querySelector<HTMLElement>('.color-choice-swatch')
    const silverSwatch = screen.getByRole('button', { name: /银色，免费/ })
      .querySelector<HTMLElement>('.color-choice-swatch')
    expect(redSwatch).toHaveStyle({ background: '#FF3B3B' })
    expect(silverSwatch?.style.background).toContain('linear-gradient')
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
    expect(catalogFixture.selectionOrder).toHaveLength(40)
    expect(catalogFixture.surfaces.filter((surface) => !surface.required)).toHaveLength(4)
    expect(catalogFixture.surfaces.find((surface) => surface.surfaceId === 'nameplate'))
      .toMatchObject({ required: true })
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

  it('彻底移除草稿入口与本地草稿，分享只复制当前配置', async () => {
    const user = userEvent.setup()
    const writeText = vi.fn().mockResolvedValue(undefined)
    Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText } })
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('外饰')
    await user.click(screen.getByRole('button', { name: /银色.*免费/ }))
    expect(screen.queryByRole('button', { name: '存草稿' })).not.toBeInTheDocument()
    await selectStage('总览')
    await user.click(screen.getByRole('button', { name: '分享' }))
    await waitFor(() => {
      expect(writeText.mock.calls[0]?.[0]).toMatch(/^SC01CFG2\./)
    })
    expect(screen.getByAltText('当前配置二维码')).toBeInTheDocument()
    expect(screen.getByText('自包含配置字符串已复制')).toBeInTheDocument()
    expect(localStorage.length).toBe(0)
  })

  it('可导入自包含配置字符串并恢复选择', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    const importedSelections = {
      ...initialSelections,
      'exterior-body-cover': 'body-cover-silver',
    }
    const portableValue = createPortableConfiguration(
      catalogFixture,
      importedSelections,
      {},
    )

    await user.click(screen.getByRole('button', { name: '导入配置' }))
    expect(screen.getByRole('dialog', { name: '配置导入' })).toBeInTheDocument()
    expect(screen.queryByRole('heading', { name: '配置传输' })).not.toBeInTheDocument()
    expect(screen.queryByAltText('当前配置二维码')).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '复制当前配置' })).not.toBeInTheDocument()
    fireEvent.change(screen.getByRole('textbox', { name: '配置字符串' }), {
      target: { value: portableValue },
    })
    await user.click(screen.getByRole('button', { name: '导入' }))

    const importedCard = screen.getByRole('button', { name: '导入配置1' })
    expect(importedCard).toHaveAttribute('aria-pressed', 'true')
    expect(importedCard.querySelector('img')).toBeNull()
    expect(screen.getByText('配置已导入')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '删除导入配置1' }))
    expect(screen.getByRole('alertdialog', { name: '确认删除导入配置' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: '导入配置1' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '确认删除' }))
    expect(screen.queryByRole('button', { name: '导入配置1' })).not.toBeInTheDocument()
  })

  it('页内复位只恢复当前 surface，保留分享参数与当前视角', async () => {
    const user = userEvent.setup()
    const fetchMock = mockApi({
      v2ImageUrl: '/assets/v2/renders/review/front-left.png',
    })
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('外饰')
    await user.click(screen.getByRole('button', { name: /银色.*免费/ }))
    await user.click(within(screen.getByRole('group', { name: '车辆视角' }))
      .getByRole('button', { name: '侧面' }))
    window.history.replaceState(null, '', '/?configuration=cfg-old')

    await user.click(screen.getByRole('button', { name: '复位' }))

    expect(screen.getByRole('button', { name: /红色.*免费/ }))
      .toHaveAttribute('aria-pressed', 'true')
    expect(window.location.search).toBe('?configuration=cfg-old')
    expect(screen.getByText('已复位车漆')).toBeInTheDocument()
    await waitFor(() => {
      const resolveCalls = fetchMock.mock.calls.filter(
        ([url]) => url === '/api/v2/renders/resolve',
      )
      const request = JSON.parse(String(resolveCalls.at(-1)?.[1]?.body))
      expect(request.selections).toEqual(initialSelections)
      expect(request.customizations).toEqual({})
      expect(request.renderViewId).toBe('side')
    })
  })

  it('按界面顺序前后导航，轮毂三项作为同一页', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '车漆配置' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '车架配置' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '轮毂材质配置' })).toBeInTheDocument()
    expect(screen.getByRole('region', { name: '轮毂造型配置' })).toBeInTheDocument()
    expect(screen.getByRole('region', { name: '轮毂颜色配置' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '车架配置' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '车漆配置' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '预设配置' })).toBeInTheDocument()
  })

  it('性能尾翼与个性化八页连续前后导航，脚垫后才进入总览', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('性能')
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '尾翼' }))
    expect(screen.getByRole('region', { name: '尾翼配置' })).toBeInTheDocument()

    const personalizationPages = [
      '内饰组件',
      '门板口袋',
      '缝线',
      '头枕刺绣',
      '中板刺绣',
      '中板缝线',
      '铭牌',
      '脚垫',
    ]
    for (const displayName of personalizationPages) {
      await user.click(screen.getByRole('button', { name: '下一步' }))
      expect(screen.getByRole('region', { name: `${displayName}配置` })).toBeInTheDocument()
    }
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '配置总览' })).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '脚垫配置' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '铭牌配置' })).toBeInTheDocument()

    await selectStage('个性化')
    expect(screen.getByRole('region', { name: '内饰组件配置' })).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '尾翼配置' })).toBeInTheDocument()
  })

  it('总览逐项显示价格，底部只保留上一步与分享', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('外饰')
    await user.click(screen.getByRole('button', { name: /自定义车漆.*¥9,600/ }))
    await selectStage('总览')

    const summary = screen.getByRole('region', { name: '配置总览' })
    expect(summary).toHaveTextContent('车漆')
    expect(summary).toHaveTextContent('自定义车漆')
    expect(summary).toHaveTextContent('¥9,600')
    expect(summary).not.toHaveTextContent('免费')
    expect(summary).not.toHaveTextContent('铭牌')
    const actions = document.querySelector('.page-actions')
    expect(actions).not.toBeNull()
    expect(within(actions as HTMLElement).getAllByRole('button').map((button) => button.textContent))
      .toEqual(['上一步', '分享'])
    expect(screen.getByText('参考总价')).toBeInTheDocument()
    expect(screen.getByText('¥239,400')).toBeInTheDocument()
    await user.click(within(actions as HTMLElement).getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '脚垫配置' })).toBeInTheDocument()
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
    expect(screen.queryByRole('region', { name: '分类筛选' })).not.toBeInTheDocument()
    await enterOptions()
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
    expect(screen.getByRole('button', { name: '场景' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '画质' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '复位' })).not.toBeInTheDocument()
    expect(screen.queryByRole('complementary', { name: '车辆选配' })).not.toBeInTheDocument()
    expect(document.body).toHaveClass('controls-document')
    expect(document.querySelector('.ue-color-corrected')).toBeNull()
    await waitFor(() => expect(fetchMock).toHaveBeenCalledWith(
      '/api/v2/catalog',
      expect.objectContaining({ headers: { Accept: 'application/json' } }),
    ))
  })

  it('header 视图从目录渲染阶段导航并可切换', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=header')
    const setconfiguratorcategory = vi.fn().mockResolvedValue(true)
    window.ue = {
      uebridge: { setconfiguratorcategory },
    }
    const user = userEvent.setup()
    mockApi()
    render(<App />)

    expect(screen.getByRole('heading', { name: 'SC01 定制' })).toBeInTheDocument()
    const navigation = await screen.findByRole('navigation', { name: '选配阶段' })
    expect(within(navigation).getAllByRole('button').map((button) => button.textContent))
      .toEqual(['01预设', '02外饰', '03内饰', '04性能', '05个性化', '06总览'])
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
    expect(screen.queryByText('¥239,400')).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '复位' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '分享' })).not.toBeInTheDocument()
    expect(screen.queryByRole('button', { name: '存草稿' })).not.toBeInTheDocument()

    expect(document.body).toHaveClass('header-document')
  })

  it('embedded 右栏响应顶部阶段事件并触发目录阶段镜头', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const setcameraid = vi.fn().mockResolvedValue(true)
    const setconfiguratorcategory = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setcameraid, setconfiguratorcategory } }
    mockApi()
    render(<App />)
    const panel = await screen.findByRole('complementary', { name: '车辆选配' })

    fireEvent(window, new CustomEvent('ue-configurator-category', { detail: 'interior' }))

    expect(await within(screen.getByRole('region', { name: '部件筛选' }))
      .findByRole('button', { name: '方向盘' })).toBeInTheDocument()
    await waitFor(() => expect(setcameraid).toHaveBeenCalledWith('driver'))
    expect(setconfiguratorcategory).not.toHaveBeenCalled()
    const scrollRegion = panel.querySelector('.panel-scroll')
    const summary = panel.querySelector('.summary')
    expect(scrollRegion).not.toBeNull()
    expect(summary).toBeNull()
    expect(panel.querySelector('.save-bar')).toBeNull()
    expect(panel.querySelector('.canonical')).toBeNull()
  })

  it('embedded bridge 仅通过带回执的原子事务同步 UE v2 状态', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const applyconfigurationtransactionjson = vi.fn().mockResolvedValue(JSON.stringify({
      ok: true,
      code: 'APPLIED',
      message: '配置与代理材质目标已原子应用。',
      configurationId: 'cfg-test',
      appliedSurfaceIds: [],
      unsupportedSurfaceIds: [],
      appliedSlotIds: [],
    }))
    window.ue = {
      uebridge: { applyconfigurationtransactionjson },
    }
    const user = userEvent.setup()
    mockApi()
    render(<App />)

    await screen.findByRole('heading', { name: 'SC01 定制' })
    await waitFor(() => expect(applyconfigurationtransactionjson).toHaveBeenCalled())
    expect(screen.queryByRole('navigation', { name: '体验控制' })).not.toBeInTheDocument()

    await enterOptions()
    await user.click(screen.getByRole('button', { name: /银色.*免费/ }))
    await waitFor(() => {
      const payload = JSON.parse(String(applyconfigurationtransactionjson.mock.lastCall?.[0]))
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
      const payload = JSON.parse(String(applyconfigurationtransactionjson.mock.lastCall?.[0]))
      expect(payload).toEqual({
        schemaVersion: '2.0.0',
        selections: initialSelections,
        customizations: {},
      })
    })
  })

  it('embedded bridge 将 unsupported 稳定 ID 转成可读反馈并说明配置已保留', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const applyconfigurationtransactionjson = vi.fn().mockResolvedValue(JSON.stringify({
      ok: true,
      code: 'APPLIED_WITH_UNSUPPORTED_SURFACES',
      message: '配置已应用；1 个 surfaceId 为当前 proxy capability 明确缺口。',
      configurationId: 'cfg-unsupported',
      appliedSurfaceIds: [],
      unsupportedSurfaceIds: ['wheel-style'],
      appliedSlotIds: [],
    }))
    window.ue = { uebridge: { applyconfigurationtransactionjson } }
    mockApi()
    render(<App />)

    await screen.findByRole('heading', { name: 'SC01 定制' })
    expect(await screen.findByText(
      '当前代理车暂不支持实时预览（配置已保留）：轮毂造型',
    )).toHaveAttribute('role', 'status')
  })

  it('embedded 从车漆逐页选择非默认项时合并旧事务并只提交最终配置', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    let resolveFirst: ((value: string) => void) | undefined
    const receipt = (configurationId: string) => JSON.stringify({
      ok: true,
      code: 'APPLIED',
      message: '配置与 40 个代理材质目标已原子应用。',
      configurationId,
      appliedSurfaceIds: [],
      unsupportedSurfaceIds: [],
      appliedSlotIds: [],
    })
    const applyconfigurationtransactionjson = vi.fn()
      .mockImplementationOnce(() => new Promise<string>((resolve) => {
        resolveFirst = resolve
      }))
      .mockResolvedValue(receipt('cfg-silver'))
    window.ue = { uebridge: { applyconfigurationtransactionjson } }
    const user = userEvent.setup()
    mockApi()
    render(<App />)

    await screen.findByRole('heading', { name: 'SC01 定制' })
    await waitFor(() => expect(applyconfigurationtransactionjson).toHaveBeenCalledTimes(1))
    await enterOptions()

    let pageCount = 0
    while (!screen.queryByRole('region', { name: '配置总览' }) && pageCount < 45) {
      const surfaceSections = Array.from(
        document.querySelectorAll<HTMLElement>('.surface-options'),
      )
      expect(surfaceSections.length).toBeGreaterThan(0)
      for (const section of surfaceSections) {
        const sliders = Array.from(
          section.querySelectorAll<HTMLInputElement>('input[type="range"]'),
        )
        const slider = sliders.at(-1)
        if (slider && slider.max !== slider.value) {
          fireEvent.input(slider, { target: { value: slider.max } })
          continue
        }
        const choices = Array.from(
          section.querySelectorAll<HTMLButtonElement>('button.color-choice'),
        )
        const choice = [...choices].reverse().find(
          (button) => button.getAttribute('aria-pressed') !== 'true',
        )
        if (choice) await user.click(choice)
      }
      await user.click(screen.getByRole('button', { name: '下一步' }))
      pageCount += 1
    }

    expect(screen.getByRole('region', { name: '配置总览' })).toBeInTheDocument()
    expect(pageCount).toBeGreaterThan(30)
    expect(applyconfigurationtransactionjson).toHaveBeenCalledTimes(1)

    await act(async () => {
      resolveFirst?.(receipt('cfg-default'))
    })
    await waitFor(() => expect(applyconfigurationtransactionjson).toHaveBeenCalledTimes(2))
    const latestPayload = JSON.parse(
      String(applyconfigurationtransactionjson.mock.calls[1][0]),
    )
    const changedSelections = Object.entries(initialSelections).filter(
      ([surfaceId, optionId]) => latestPayload.selections[surfaceId] !== optionId,
    )
    const customizedSurfaceCount = Object.keys(latestPayload.customizations).length
    expect(changedSelections.length).toBeGreaterThan(10)
    expect(customizedSurfaceCount).toBeGreaterThan(3)
    expect(changedSelections.length + customizedSurfaceCount).toBeGreaterThan(14)
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

    await screen.findByRole('heading', { name: 'SC01 定制' })
    await enterOptions()
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

    await enterOptions()
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '轮毂' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('side'))

    await user.click(screen.getByRole('button', { name: '下一步' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('wheel'))
    await user.click(screen.getByRole('button', { name: '下一步' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('rear-wheel'))

    fireEvent(window, new CustomEvent('ue-configurator-category', { detail: 'interior' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('driver'))
    const cameraCallCount = setcameraid.mock.calls.length
    await user.click(within(screen.getByRole('region', { name: '子项筛选' }))
      .getByRole('button', { name: '加粗(EVA海绵)' }))
    expect(setcameraid).toHaveBeenCalledTimes(cameraCallCount)

    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('seat'))
  })

  it('内饰上下步按部件与子项界面顺序推进并联动组件镜头', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const setcameraid = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setcameraid } }
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    fireEvent(window, new CustomEvent('ue-configurator-category', { detail: 'interior' }))
    expect(await screen.findByRole('region', { name: '表皮配置' })).toBeInTheDocument()
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('driver'))
    const driverCameraCallCount = setcameraid.mock.calls.length
    await user.click(screen.getByRole('button', { name: '下一步' }))
    const evaOptions = screen.getByRole('region', { name: '加粗(EVA海绵)配置' })
    expect(evaOptions).toBeInTheDocument()
    expect(within(evaOptions).queryByRole('img', { name: /定制项目参考/ })).not.toBeInTheDocument()
    expect(within(evaOptions).getByRole('button', { name: /加粗\(EVA海绵\)/ })
      .querySelector('.color-choice-swatch')).toHaveStyle({ background: '#000000' })
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '回中标配置' })).toBeInTheDocument()
    expect(setcameraid).toHaveBeenCalledTimes(driverCameraCallCount)
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '接触面配置' })).toBeInTheDocument()
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('seat'))
    await user.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByRole('region', { name: '回中标配置' })).toBeInTheDocument()
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('driver'))
  })

  it('embedded 上下步不回写 category，按子项顺序且每次镜头变化只联动一次', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const setcameraid = vi.fn().mockResolvedValue(true)
    const setconfiguratorcategory = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setcameraid, setconfiguratorcategory } }
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    fireEvent(window, new CustomEvent('ue-configurator-category', { detail: 'exterior' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('exterior'))
    setcameraid.mockClear()
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '车架配置' })).toBeInTheDocument()
    await waitFor(() => expect(setcameraid).toHaveBeenCalledTimes(1))
    expect(setcameraid).toHaveBeenLastCalledWith('engine-bay')
    expect(setconfiguratorcategory).not.toHaveBeenCalled()

    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '轮毂材质配置' })).toBeInTheDocument()
    await waitFor(() => expect(setcameraid).toHaveBeenCalledTimes(2))
    expect(setcameraid).toHaveBeenLastCalledWith('side')
    expect(setconfiguratorcategory).not.toHaveBeenCalled()
  })

  it('个性化除头枕外使用 front-cabin，头枕使用 seat', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const setcameraid = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setcameraid } }
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    fireEvent(window, new CustomEvent('ue-configurator-category', { detail: 'personalization' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('front-cabin'))
    const parts = screen.getByRole('region', { name: '部件筛选' })
    await user.click(within(parts).getByRole('button', { name: '头枕刺绣' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('seat'))
    await user.click(within(parts).getByRole('button', { name: '中板刺绣' }))
    await waitFor(() => expect(setcameraid).toHaveBeenLastCalledWith('front-cabin'))
  })

  it('分类、部件、子项由目录元数据联动且 wheel 三框同页展示', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await loadProxy()

    await selectStage('外饰')
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
    const wheelStyles = screen.getByRole('region', { name: '轮毂造型配置' })
    expect(wheelStyles).toBeInTheDocument()
    expect(within(wheelStyles).queryByRole('button', { name: /多条幅轮毂/ }))
      .not.toBeInTheDocument()
    expect(within(wheelStyles).getAllByRole('button')).toHaveLength(8)
    expect(within(wheelStyles).getByRole('button', { name: '款式1，¥500' })).toBeEnabled()
    const wheelColors = screen.getByRole('region', { name: '轮毂颜色配置' })
    expect(within(wheelColors).getAllByRole('button')).toHaveLength(10)
    expect(within(wheelColors).getByRole('button', { name: '亮银色，免费' })).toBeEnabled()
    for (const name of ['哑光银', '黑色', '哑光黑', '枪灰', '哑光枪灰', '香槟金', '哑光香槟金', '古铜', '哑光古铜']) {
      expect(within(wheelColors).getByRole('button', { name: `${name}，¥1,200` })).toBeEnabled()
    }
    await user.click(screen.getByRole('button', { name: '下一步' }))
    expect(screen.getByRole('region', { name: '前卡钳配置' })).toBeInTheDocument()

    await selectStage('内饰')
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '方向盘' }))
    expect(within(screen.getByRole('region', { name: '奥司维材质' }))
      .getAllByRole('slider')[0])
      .toHaveAttribute('aria-valuetext', '奥司维（黑）')
    expect(document.querySelector('.selected-material-preview')).toHaveTextContent('奥司维')

    await selectStage('个性化')
    const personalizationParts = await screen.findByRole('region', { name: '部件筛选' })
    expect(personalizationParts).toHaveTextContent(
      '内饰组件门板口袋缝线头枕刺绣中板刺绣中板缝线铭牌脚垫',
    )
    expect(screen.queryByRole('region', { name: '子项筛选' })).not.toBeInTheDocument()
  })

  it('尾翼灰色项以暂不可选状态禁用展示', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('性能')
    const performanceParts = within(screen.getByRole('region', { name: '部件筛选' }))
    expect(performanceParts.getAllByRole('button').map((button) => button.textContent))
      .toEqual(['下护板', '尾翼'])
    await user.click(performanceParts.getByRole('button', { name: '尾翼' }))

    expect(screen.getByRole('button', { name: '无尾翼，免费' }))
      .toHaveAttribute('aria-pressed', 'true')
    const unavailable = screen.getByRole('button', { name: '灰色，暂不可选' })
    expect(unavailable).toBeDisabled()
    expect(unavailable).toHaveTextContent('暂不可选')
  })

  it('个性化缝线、刺绣、中板和脚垫按新目录展示', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await selectStage('个性化')
    const parts = screen.getByRole('region', { name: '部件筛选' })

    await user.click(within(parts).getByRole('button', { name: '内饰组件' }))
    expect(screen.getByRole('button', { name: '黑色，¥1,680' })).toBeEnabled()

    await user.click(within(parts).getByRole('button', { name: '缝线' }))
    const stitching = screen.getByRole('region', { name: '缝线配置' })
    for (const color of ['黑色', '红色', '黄色', '蓝色', '绿色']) {
      expect(within(stitching).getByRole('button', { name: `${color}，免费` }))
        .toBeEnabled()
    }

    await user.click(within(parts).getByRole('button', { name: '头枕刺绣' }))
    expect(screen.getByRole('button', { name: '无刺绣，免费' }))
      .toHaveAttribute('aria-pressed', 'true')
    const headrestEmbroidery = screen.getByRole('button', { name: '头枕刺绣，¥1,288' })
    expect(headrestEmbroidery).toBeEnabled()
    expect(headrestEmbroidery.querySelector('img'))
      .toHaveAttribute('src', '/sc01/interior-parts/headrest-embroidery.webp')

    await user.click(within(parts).getByRole('button', { name: '中板刺绣' }))
    const doorEmbroidery = screen.getByRole('button', { name: '中板刺绣，¥1,688' })
    expect(doorEmbroidery).toBeEnabled()
    expect(doorEmbroidery.querySelector('img'))
      .toHaveAttribute('src', '/sc01/interior-parts/door-panel-embroidery.webp')

    await user.click(within(parts).getByRole('button', { name: '中板缝线' }))
    expect(screen.getByRole('button', { name: '黑色，免费' }))
      .toHaveAttribute('aria-pressed', 'true')
    expect(screen.getByRole('button', { name: '自定义颜色，¥600' })).toBeEnabled()

    await user.click(within(parts).getByRole('button', { name: '铭牌' }))
    expect(screen.getByRole('button', { name: '无，免费' }))
      .toHaveAttribute('aria-pressed', 'true')
    for (const [name, price, preview] of [
      ['铜', '¥880', '/sc01/interior-parts/nameplate-copper-preview.webp'],
      ['不锈钢', '¥860', '/sc01/interior-parts/nameplate-stainless-preview.webp'],
      ['碳纤维', '¥980', '/sc01/interior-parts/nameplate-carbon-preview.webp'],
    ]) {
      const choice = screen.getByRole('button', { name: `${name}，${price}` })
      expect(choice.querySelector('img')).toHaveAttribute('src', preview)
    }

    await user.click(within(parts).getByRole('button', { name: '脚垫' }))
    expect(screen.getByRole('button', { name: '豪车毯，¥560' })).toBeEnabled()
    expect(screen.getByRole('button', { name: '豪车毯+金属板，¥1,680' })).toBeEnabled()
  })

  it('不为未声明 variant 色彩能力的付费材质选项展示色卡', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('内饰')
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))

    expect(await screen.findByRole('heading', { name: '接触面' })).toBeInTheDocument()
    expect(screen.queryByRole('button', { name: /Black UF7，奥司维定制/ }))
      .not.toBeInTheDocument()
  })

  it('材料标题可直接选择材质，部件图与选中材质摘要独立显示', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('内饰')
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '方向盘' }))
    const leatherFamily = within(screen.getByRole('region', { name: '牛皮材质' }))
    await user.click(screen.getByRole('region', { name: '牛皮材质' }))
    expect(screen.getByRole('region', { name: '牛皮材质' })).toHaveClass('selected')
    expect(leatherFamily.getAllByRole('slider')[0]).toHaveAttribute(
      'aria-valuetext',
      '9743 Nero',
    )
    await waitFor(() => {
      const preview = document.querySelector('.selected-material-preview') as HTMLElement
      expect(preview).toHaveTextContent('9743 Nero')
      expect(preview).toHaveTextContent('¥1,280')
      expect(preview).not.toHaveTextContent('牛皮')
      expect(preview.querySelector('.selected-material-copy small')).toHaveTextContent('¥1,280')
      expect(preview.querySelector('.selected-material-thumbnail img'))
        .toHaveAttribute('alt', '9743 Nero材质预览')
      expect(preview.querySelector('.surface-reference')).toBeNull()
    })
  })

  it('织布和织物羊毛保留方块卡', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('内饰')
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '车顶' }))

    expect(within(screen.getByRole('region', { name: '棚面配置' }))
      .getByRole('button', { name: '棚面，免费' })).toHaveClass('color-choice')
    const source = readFileSync('src/App.tsx', 'utf8')
    expect(source).toMatch(/TEXTURE_CARD_MATERIAL_FAMILIES[\s\S]*'woven-wool'[\s\S]*'woven-fabric'/)
    expect(source).toMatch(/useTextureCards[\s\S]*material-texture-options/)
  })

  it('自定义车漆显示调色盘和受限参数，并在颜色变化时实时提交', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const user = userEvent.setup()
    const applyconfigurationtransactionjson = vi.fn().mockResolvedValue(JSON.stringify({
      ok: true,
      code: 'APPLIED',
      message: '配置已应用。',
      configurationId: 'cfg-custom-paint',
      appliedSurfaceIds: [],
      unsupportedSurfaceIds: [],
      appliedSlotIds: [],
    }))
    window.ue = { uebridge: { applyconfigurationtransactionjson } }
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await enterOptions()
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
    await waitFor(() => {
      const payload = JSON.parse(String(applyconfigurationtransactionjson.mock.lastCall?.[0]))
      expect(payload.customizations['exterior-body-cover']).toMatchObject({
        colorHex: '#123456',
      })
    })
  })

  it('车架默认银色免费，PPG 按 custom 能力初始化同款调色盘', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('外饰')
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

  it('背板自定义颜色支持亮面与哑光切换', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('内饰')
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))
    await user.click(within(screen.getByRole('region', { name: '子项筛选' }))
      .getByRole('button', { name: '背板' }))

    expect(screen.getByRole('button', { name: /高光原色碳纤维，免费/ }))
      .toHaveAttribute('aria-pressed', 'true')
    await user.click(screen.getByRole('button', { name: /自定义颜色，¥3,360/ }))
    const editor = screen.getByRole('region', { name: '自定义颜色' })
    const finishGroup = within(editor).getByRole('group', { name: '背板表面效果' })
    expect(within(editor).getByText('饰面')).toBeInTheDocument()
    expect(editor.querySelector('.custom-finish-layout')).not.toBeNull()
    const glossButton = within(finishGroup).getByRole('button', { name: '亮面' })
    const matteButton = within(finishGroup).getByRole('button', { name: '哑光' })
    expect(glossButton).toHaveAttribute('aria-pressed', 'true')
    await user.click(matteButton)
    expect(matteButton).toHaveAttribute('aria-pressed', 'true')
    expect(screen.getByRole('button', { name: /自定义颜色，¥3,360/ }).querySelector('img'))
      .toHaveAttribute('src', '/sc01/option-icons/rainbow.svg')
  })

  it('回中标按四种材料展示免费完整色卡', async () => {
    const user = userEvent.setup()
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await selectStage('内饰')
    await user.click(within(screen.getByRole('region', { name: '部件筛选' }))
      .getByRole('button', { name: '座椅' }))
    await user.click(within(screen.getByRole('region', { name: '子项筛选' }))
      .getByRole('button', { name: '回中标' }))

    for (const familyName of ['奥司维', 'Alcantara', '超纤皮', '牛皮']) {
      const family = within(screen.getByRole('region', { name: `${familyName}材质` }))
      expect(family.getAllByRole('slider').length).toBeGreaterThan(0)
      expect(family.queryByText('免费')).not.toBeInTheDocument()
    }
    expect(document.querySelector('.selected-material-preview')).toHaveTextContent('免费')
    expect(document.querySelectorAll('.material-family .check')).toHaveLength(1)
    expect(screen.getByRole('region', { name: '奥司维材质' })).toHaveClass('selected')
    expect(screen.getByRole('region', { name: 'Alcantara材质' })).not.toHaveClass('selected')
  })

  it('渲染相关配置变化会刷新代理图并保留上一张直到新图就绪', async () => {
    const user = userEvent.setup()
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })
    await loadProxy()

    await selectStage('外饰')
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

  it('embedded 分享生成可分享字符串与二维码且不落草稿', async () => {
    window.history.replaceState(null, '', '/?source=ue&view=embedded')
    const user = userEvent.setup()
    const writeText = vi.fn().mockResolvedValue(undefined)
    const setconfiguratorheaderstatejson = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { setconfiguratorheaderstatejson } }
    Object.defineProperty(navigator, 'clipboard', { configurable: true, value: { writeText } })
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    await enterOptions()
    await user.click(screen.getByRole('button', { name: /银色/ }))
    fireEvent(window, new CustomEvent('ue-configurator-header-action', { detail: 'share' }))
    await waitFor(() => {
      expect(writeText.mock.calls[0]?.[0]).toMatch(/^SC01CFG2\./)
    })
    expect(screen.getByAltText('当前配置二维码')).toBeInTheDocument()
    expect(localStorage.length).toBe(0)
  })

  it('进入页面忽略旧草稿并始终使用默认配置与默认价格', async () => {
    localStorage.setItem('automotive-v2-configurator', JSON.stringify({
      catalog: catalogFixture,
      selections: {
        ...initialSelections,
        'exterior-body-cover': 'body-cover-custom',
      },
      customizations: {},
    }))
    mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: 'SC01 定制' })

    expect(screen.getByRole('button', { name: '默认配置' }))
      .toHaveAttribute('aria-pressed', 'true')
    expect(within(screen.getByRole('button', { name: '默认配置' })).getByText('¥229,800'))
      .toBeInTheDocument()
    expect(within(document.querySelector('.panel-reference-total') as HTMLElement)
      .getByText('¥229,800')).toBeInTheDocument()
  })
})
