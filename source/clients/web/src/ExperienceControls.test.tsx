import { act, fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import ExperienceControls from './ExperienceControls'
import { catalogFixture } from './test/catalogFixture'

describe('ExperienceControls', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({
      ok: true,
      status: 200,
      json: () => Promise.resolve(catalogFixture),
    }))
  })

  afterEach(() => {
    vi.useRealTimers()
    vi.unstubAllGlobals()
    delete window.ue
    document.documentElement.classList.remove('controls-document')
    document.body.classList.remove('controls-document')
  })

  it('通过受限 bridge 控制镜头、动画、灯光、渲染、复位和全屏', async () => {
    const user = userEvent.setup()
    let currentState = {
      cameraId: 'wheel',
      animationEnabled: false,
      animationId: null as string | null,
      lightPreset: 'studio',
      renderMode: 'realtime',
      quality: 'high',
      fullscreen: false,
    }
    const bridge = {
      getpresentationstatejson: vi.fn(
        async () => JSON.stringify(currentState),
      ),
      setcameraid: vi.fn().mockResolvedValue(true),
      focusanimation: vi.fn(async (animationId: string) => {
        currentState = {
          ...currentState,
          animationEnabled: animationId !== '',
          animationId: animationId || null,
        }
        return true
      }),
      setlightpreset: vi.fn(async (preset: string) => {
        currentState = {
          ...currentState,
          lightPreset: preset as 'studio' | 'outdoor',
        }
        return true
      }),
      setrendermode: vi.fn(async (mode: string) => {
        currentState = {
          ...currentState,
          renderMode: mode as 'realtime' | 'path-tracing',
        }
        return true
      }),
      setqualitylevel: vi.fn().mockResolvedValue(true),
      resetpresentation: vi.fn().mockResolvedValue(true),
      setfullscreen: vi.fn(async (enabled: boolean) => {
        currentState = { ...currentState, fullscreen: enabled }
        return true
      }),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const toolbar = screen.getByRole('navigation', { name: '体验控制' })
    expect(within(toolbar).getAllByRole('button')).toHaveLength(7)
    await waitFor(() => expect(bridge.getpresentationstatejson).toHaveBeenCalled())

    await user.click(within(toolbar).getByRole('button', { name: '镜头' }))
    const cameraMenu = await screen.findByRole('menu', { name: '镜头预设' })
    expect(within(cameraMenu).getAllByRole('menuitemradio')).toHaveLength(5)
    expect(within(cameraMenu).getByRole('menuitemradio', { name: '轮毂' }))
      .toHaveAttribute('aria-checked', 'true')
    expect(within(cameraMenu).getByRole('menuitemradio', { name: '驾驶位' }).querySelector('img'))
      .toHaveAttribute('src', '/camera-driver.svg')
    await user.click(within(cameraMenu).getByRole('menuitemradio', { name: '驾驶位' }))
    await user.click(within(toolbar).getByRole('button', { name: '动画' }))
    const animationMenu = await screen.findByRole('menu', { name: '动画列表' })
    expect(within(animationMenu).getAllByRole('menuitemradio').map((item) => item.textContent))
      .toEqual(['开启机舱盖', '后盖往复', '车轮旋转'])
    await user.click(within(animationMenu).getByRole('menuitemradio', { name: '开启机舱盖' }))
    await user.click(within(toolbar).getByRole('button', { name: '动画' }))
    await user.click(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))
    await user.click(within(toolbar).getByRole('button', { name: '灯光' }))
    await user.click(within(toolbar).getByRole('button', { name: '渲染' }))
    await user.click(within(toolbar).getByRole('button', { name: '画质' }))
    await user.click(screen.getByRole('menuitemradio', { name: '极高' }))
    await user.click(within(toolbar).getByRole('button', { name: '复位' }))
    await user.click(within(toolbar).getByRole('button', { name: '全屏' }))

    expect(bridge.setcameraid).toHaveBeenCalledWith('driver')
    expect(bridge.focusanimation).toHaveBeenNthCalledWith(1, 'hood')
    expect(bridge.focusanimation).toHaveBeenNthCalledWith(2, '')
    expect(bridge.setlightpreset).toHaveBeenCalledWith('outdoor')
    expect(bridge.setrendermode).toHaveBeenCalledWith('path-tracing')
    expect(bridge.setqualitylevel).toHaveBeenCalledWith('epic')
    expect(bridge.resetpresentation).toHaveBeenCalledOnce()
    expect(bridge.setfullscreen).toHaveBeenCalledWith(true)
    expect(bridge.getpresentationstatejson.mock.calls.length).toBeGreaterThanOrEqual(15)
  })

  it('从 UE 状态初始化控件，Promise 拒绝时不更新激活状态', async () => {
    const user = userEvent.setup()
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraIndex: 4,
        animationEnabled: true,
        lightPreset: 'outdoor',
        renderMode: 'path-tracing',
        quality: 'epic',
        fullscreen: true,
      })),
      focusanimation: vi.fn().mockRejectedValue(new Error('rejected')),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const animation = screen.getByRole('button', { name: '动画' })
    await waitFor(() => expect(animation).toHaveAttribute('aria-pressed', 'true'))
    await user.click(animation)
    await user.click(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))

    expect(await screen.findByRole('alert'))
      .toHaveTextContent('UE 控制桥不可用或命令被拒绝')
    expect(animation).toHaveAttribute('aria-pressed', 'true')
  })

  it('兼容只有旧 cameraIndex 的 UE 展示状态', async () => {
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraIndex: 4,
        animationEnabled: false,
        lightPreset: 'studio',
        renderMode: 'realtime',
        quality: 'high',
        fullscreen: false,
      })),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    await userEvent.click(screen.getByRole('button', { name: '镜头' }))
    expect(await screen.findByRole('menuitemradio', { name: '驾驶位' }))
      .toHaveAttribute('aria-checked', 'true')
    expect(screen.getByRole('menuitemradio', { name: '前舱' }))
      .toHaveAttribute('aria-checked', 'false')
  })

  it('setcameraid 缺失时仅按目录 legacyIndex 调用旧 setcamera', async () => {
    const user = userEvent.setup()
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraIndex: 2,
        animationEnabled: false,
        lightPreset: 'studio',
        renderMode: 'realtime',
        quality: 'high',
        fullscreen: false,
      })),
      setcamera: vi.fn().mockResolvedValue(true),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    await user.click(screen.getByRole('button', { name: '镜头' }))
    const menu = await screen.findByRole('menu', { name: '镜头预设' })
    expect(within(menu).getByRole('menuitemradio', { name: '轮毂' }))
      .toHaveAttribute('aria-checked', 'true')

    await user.click(within(menu).getByRole('menuitemradio', { name: '驾驶位' }))
    expect(bridge.setcamera).toHaveBeenCalledWith(4)

    await user.click(screen.getByRole('button', { name: '镜头' }))
    await user.click(screen.getByRole('menuitemradio', { name: '座椅' }))
    expect(bridge.setcamera).toHaveBeenCalledOnce()
    expect(await screen.findByRole('alert'))
      .toHaveTextContent('UE 控制桥不可用或命令被拒绝')
  })

  it('bridge 缺失时显示错误且不伪造激活状态', async () => {
    const user = userEvent.setup()
    render(<ExperienceControls ueEnabled />)

    const animation = screen.getByRole('button', { name: '动画' })
    await user.click(animation)
    await user.click(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))

    expect(screen.getByRole('alert')).toHaveTextContent('UE 控制桥不可用或命令被拒绝')
    expect(animation).toHaveAttribute('aria-pressed', 'false')
  })

  it('挂载时为 CEF 控制页启用透明文档，卸载时清理', () => {
    const view = render(<ExperienceControls ueEnabled />)
    expect(document.documentElement).toHaveClass('controls-document')
    expect(document.body).toHaveClass('controls-document')

    view.unmount()
    expect(document.documentElement).not.toHaveClass('controls-document')
    expect(document.body).not.toHaveClass('controls-document')
  })

  it('Web 全屏时底栏空闲渐隐，指针 hover 后立即恢复', async () => {
    vi.useFakeTimers()
    window.ue = {
      uebridge: {
        getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
          cameraIndex: 1,
          animationEnabled: false,
          lightPreset: 'studio',
          renderMode: 'realtime',
          quality: 'high',
          fullscreen: true,
        })),
      },
    }
    const { container } = render(<ExperienceControls ueEnabled />)
    const view = container.querySelector('.controls-view')

    await act(async () => {
      await Promise.resolve()
    })
    expect(view).toHaveClass('fullscreen')

    act(() => vi.advanceTimersByTime(1800))
    expect(view).toHaveClass('toolbar-idle')

    fireEvent.pointerMove(view!)
    expect(view).not.toHaveClass('toolbar-idle')
  })
})
