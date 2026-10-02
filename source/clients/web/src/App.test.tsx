import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import App from './App'
import { catalogFixture } from './test/catalogFixture'

const healthFixture = {
  status: 'ok',
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

function resolvedRender(renderViewId = 'front-left') {
  return {
    configurationKey: 'paint-red__wheel-sport__interior-dark__frame-black',
    renderViewId,
    imageUrl: `/assets/renders/from-server/${renderViewId}.png`,
  }
}

function mockApi() {
  const fetchMock = vi.fn((url: string, init?: RequestInit) => {
    if (url === '/health') return Promise.resolve(jsonResponse(healthFixture))
    if (url === '/api/v1/catalog') return Promise.resolve(jsonResponse(catalogFixture))
    const request = JSON.parse(String(init?.body))
    return Promise.resolve(jsonResponse(resolvedRender(request.renderViewId)))
  })
  vi.stubGlobal('fetch', fetchMock)
  return fetchMock
}

describe('App', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('并行启动后渲染中文四分区、四视角和服务端图片', async () => {
    const fetchMock = mockApi()
    render(<App />)

    expect(screen.getByText('正在加载车型与选配目录…')).toBeInTheDocument()
    expect(await screen.findByRole('heading', { name: '演示车型' })).toBeInTheDocument()
    expect(fetchMock.mock.calls.slice(0, 2).map(([url]) => url)).toEqual([
      '/health',
      '/api/v1/catalog',
    ])
    expect(await screen.findByAltText('演示车型 front-left')).toHaveAttribute(
      'src',
      '/assets/renders/from-server/front-left.png',
    )
    expect(screen.getAllByRole('button', { name: /正前|左前|侧面|右后/ })).toHaveLength(4)
    for (const name of ['车漆', '轮毂', '内饰', '内部车架']) {
      expect(screen.getByRole('button', { name: new RegExp(name) })).toBeInTheDocument()
    }
  })

  it('切换选项、模板和视角时立即更新本地价格与 key，并提交 resolve', async () => {
    const user = userEvent.setup()
    const fetchMock = mockApi()
    render(<App />)
    await screen.findByRole('heading', { name: '演示车型' })

    await user.click(screen.getByRole('button', { name: /星辉银/ }))
    expect(screen.getByText('¥308,800')).toBeInTheDocument()
    expect(screen.getByText('paint-silver__wheel-sport__interior-dark__frame-black')).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: /豪华/ }))
    expect(screen.getByText('¥327,600')).toBeInTheDocument()
    expect(screen.getByText('paint-silver__wheel-forged__interior-ivory__frame-black')).toBeInTheDocument()

    await user.click(screen.getByRole('button', { name: '侧面' }))
    expect(await screen.findByText('/assets/renders/from-server/side.png')).toBeInTheDocument()
    const resolveCalls = fetchMock.mock.calls.filter(([url]) => url === '/api/v1/renders/resolve')
    expect(JSON.parse(String(resolveCalls.at(-1)?.[1]?.body))).toEqual({
      catalogVersion: 'mvp-v1',
      publicationVersion: 'publication-v1',
      vehicleId: 'demo-car',
      selections: {
        paint: 'paint-silver',
        wheel: 'wheel-forged',
        interior: 'interior-ivory',
        frame: 'frame-black',
      },
      renderViewId: 'side',
    })
  })

  it('启动请求失败时展示错误并允许重试', async () => {
    let catalogAttempts = 0
    const fetchMock = vi.fn((url: string, init?: RequestInit) => {
      if (url === '/health') return Promise.resolve(jsonResponse(healthFixture))
      if (url === '/api/v1/catalog') {
        catalogAttempts += 1
        return Promise.resolve(
          catalogAttempts === 1 ? jsonResponse({}, 503) : jsonResponse(catalogFixture),
        )
      }
      const request = JSON.parse(String(init?.body))
      return Promise.resolve(jsonResponse(resolvedRender(request.renderViewId)))
    })
    vi.stubGlobal('fetch', fetchMock)
    const user = userEvent.setup()
    render(<App />)

    expect(await screen.findByText('目录服务返回 503')).toBeInTheDocument()
    await user.click(screen.getByRole('button', { name: '重新加载' }))
    expect(await screen.findByRole('heading', { name: '演示车型' })).toBeInTheDocument()
    expect(catalogAttempts).toBe(2)
  })

  it('图片加载失败时展示中文占位', async () => {
    mockApi()
    render(<App />)
    const image = await screen.findByAltText('演示车型 front-left')

    fireEvent.error(image)

    await waitFor(() => {
      expect(screen.getByRole('img', { name: '演示车型 front-left图片暂缺' })).toBeInTheDocument()
    })
  })

  it('resolve 返回 404 时展示当前配置缺图提示', async () => {
    vi.stubGlobal('fetch', vi.fn((url: string) => {
      if (url === '/health') return Promise.resolve(jsonResponse(healthFixture))
      if (url === '/api/v1/catalog') return Promise.resolve(jsonResponse(catalogFixture))
      return Promise.resolve(jsonResponse({ code: 'NOT_FOUND', message: 'not found' }, 404))
    }))

    render(<App />)

    expect(await screen.findByRole('alert')).toHaveTextContent('该配置在当前视角暂无可用图片')
    expect(screen.getByText('暂无可用图片')).toBeInTheDocument()
  })

  it('resolve 返回 409 时提示重新加载目录', async () => {
    vi.stubGlobal('fetch', vi.fn((url: string) => {
      if (url === '/health') return Promise.resolve(jsonResponse(healthFixture))
      if (url === '/api/v1/catalog') return Promise.resolve(jsonResponse(catalogFixture))
      return Promise.resolve(jsonResponse({ code: 'VERSION_CONFLICT', message: 'conflict' }, 409))
    }))

    render(<App />)

    expect(await screen.findByRole('alert')).toHaveTextContent('发布版本已更新，请重新加载目录')
    expect(screen.getByRole('button', { name: '重新加载目录' })).toBeInTheDocument()
  })

  it('配置变化会中止旧 resolve，迟到响应不能覆盖当前视角', async () => {
    const user = userEvent.setup()
    let releaseFirstResolve!: () => void
    let firstResolveSignal: AbortSignal | undefined
    let resolveCount = 0
    const firstPending = new Promise<void>((resolve) => { releaseFirstResolve = resolve })
    vi.stubGlobal('fetch', vi.fn((url: string, init?: RequestInit) => {
      if (url === '/health') return Promise.resolve(jsonResponse(healthFixture))
      if (url === '/api/v1/catalog') return Promise.resolve(jsonResponse(catalogFixture))
      resolveCount += 1
      const request = JSON.parse(String(init?.body))
      if (resolveCount === 1) {
        firstResolveSignal = init?.signal ?? undefined
        return firstPending.then(() => jsonResponse(resolvedRender('front-left')))
      }
      return Promise.resolve(jsonResponse(resolvedRender(request.renderViewId)))
    }))
    render(<App />)
    await screen.findByRole('heading', { name: '演示车型' })
    expect(screen.getByRole('status')).toHaveTextContent('正在解析车辆图片…')

    await user.click(screen.getByRole('button', { name: '侧面' }))
    expect(await screen.findByText('/assets/renders/from-server/side.png')).toBeInTheDocument()
    expect(firstResolveSignal?.aborted).toBe(true)

    releaseFirstResolve()
    await waitFor(() => {
      expect(screen.queryByText('/assets/renders/from-server/front-left.png')).not.toBeInTheDocument()
    })
  })
})
