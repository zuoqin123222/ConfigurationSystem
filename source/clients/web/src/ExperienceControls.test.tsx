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

  it('通过受限 bridge 控制镜头、动画、场景、渲染和全屏', async () => {
    const user = userEvent.setup()
    let currentState = {
      cameraId: 'wheel',
      animationEnabled: false,
      animationId: null as string | null,
      lightPreset: 'studio',
      renderMode: 'realtime',
      renderAvailability: 'ready' as const,
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
      setfullscreen: vi.fn(async (enabled: boolean) => {
        currentState = { ...currentState, fullscreen: enabled }
        return true
      }),
      reportuiready: vi.fn().mockResolvedValue(true),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const toolbar = screen.getByRole('navigation', { name: '体验控制' })
    await screen.findByRole('button', { name: '动画' })
    expect(within(toolbar).getAllByRole('button')).toHaveLength(5)
    await waitFor(() => expect(bridge.getpresentationstatejson).toHaveBeenCalled())

    await user.hover(within(toolbar).getByRole('button', { name: '镜头' }))
    const cameraMenu = await screen.findByRole('menu', { name: '镜头预设' })
    expect(within(cameraMenu).getAllByRole('menuitemradio')).toHaveLength(5)
    expect(within(cameraMenu).getByRole('menuitemradio', { name: '轮毂' }))
      .toHaveAttribute('aria-checked', 'true')
    expect(within(cameraMenu).getByRole('menuitemradio', { name: '驾驶位' }).querySelector('img'))
      .toBeNull()
    fireEvent.click(within(cameraMenu).getByRole('menuitemradio', { name: '驾驶位' }))
    await user.unhover(within(toolbar).getByRole('button', { name: '镜头' }))
    await user.hover(within(toolbar).getByRole('button', { name: '动画' }))
    const animationMenu = await screen.findByRole('menu', { name: '动画列表' })
    expect(within(animationMenu).getAllByRole('menuitemradio').map((item) => item.textContent))
      .toEqual(['开启机舱盖', '后盖往复', '车轮旋转'])
    fireEvent.click(within(animationMenu).getByRole('menuitemradio', { name: '开启机舱盖' }))
    await user.unhover(within(toolbar).getByRole('button', { name: '动画' }))
    await user.hover(within(toolbar).getByRole('button', { name: '动画' }))
    fireEvent.click(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))
    await user.unhover(within(toolbar).getByRole('button', { name: '动画' }))
    await user.hover(within(toolbar).getByRole('button', { name: '场景' }))
    const sceneMenu = await screen.findByRole('menu', { name: '场景预设' })
    expect(sceneMenu).toHaveClass('compact-popover')
    expect(within(sceneMenu).getAllByRole('menuitemradio').map((item) => item.textContent))
      .toEqual(['影棚', '外景'])
    await user.unhover(within(toolbar).getByRole('button', { name: '场景' }))
    await user.click(within(toolbar).getByRole('button', { name: '场景' }))
    await user.click(within(toolbar).getByRole('button', { name: '渲染' }))
    await user.click(within(toolbar).getByRole('button', { name: '全屏' }))

    expect(bridge.setcameraid).toHaveBeenCalledWith('driver')
    expect(bridge.focusanimation).toHaveBeenNthCalledWith(1, 'hood')
    expect(bridge.focusanimation).toHaveBeenNthCalledWith(2, '')
    expect(bridge.setlightpreset).toHaveBeenCalledWith('outdoor')
    expect(within(toolbar).getByRole('button', { name: '场景' }))
      .not.toHaveAttribute('aria-pressed')
    expect(bridge.reportuiready).toHaveBeenCalledWith('controls')
    expect(bridge.setrendermode).toHaveBeenCalledWith('path-tracing')
    expect(bridge.setfullscreen).toHaveBeenCalledWith(true)
    expect(within(toolbar).queryByRole('button', { name: '画质' })).not.toBeInTheDocument()
    expect(within(toolbar).queryByRole('button', { name: '复位' })).not.toBeInTheDocument()
  })

  it('从 UE 状态初始化控件，Promise 拒绝时不更新激活状态', async () => {
    const user = userEvent.setup()
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraIndex: 4,
        animationEnabled: true,
        lightPreset: 'outdoor',
        renderMode: 'path-tracing',
        renderProgress: 0.42,
        quality: 'epic',
        fullscreen: true,
      })),
      focusanimation: vi.fn().mockRejectedValue(new Error('rejected')),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const animation = await screen.findByRole('button', { name: '动画' })
    const renderButton = screen.getByRole('button', { name: '渲染' })
    await waitFor(() => expect(animation).toHaveAttribute('aria-pressed', 'true'))
    await waitFor(() => expect(renderButton.querySelector('.render-progress-value'))
      .toHaveStyle({ strokeDashoffset: 58 }))
    await user.hover(animation)
    fireEvent.click(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))

    expect(await screen.findByRole('alert'))
      .toHaveTextContent('UE 命令被拒绝或执行失败')
    expect(animation).toHaveAttribute('aria-pressed', 'true')
  })

  it('能力探测部分不支持时只过滤对应动画项', async () => {
    const user = userEvent.setup()
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraId: 'wheel',
        animationEnabled: false,
        animationId: null,
        lightPreset: 'studio',
        renderMode: 'realtime',
        quality: 'epic',
        fullscreen: false,
      })),
      canplayanimation: vi.fn(async (animationId: string) => animationId !== 'trunk'),
      focusanimation: vi.fn().mockResolvedValue(true),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const animation = await screen.findByRole('button', { name: '动画' })
    await user.hover(animation)
    const menu = await screen.findByRole('menu', { name: '动画列表' })
    await waitFor(() => expect(bridge.canplayanimation).toHaveBeenCalledWith('wheel-spin'))
    await waitFor(() => expect(
      within(menu).queryByRole('menuitemradio', { name: '后盖往复' }),
    ).not.toBeInTheDocument())
    expect(within(menu).getByRole('menuitemradio', { name: '开启机舱盖' }))
      .toBeInTheDocument()
    expect(bridge.canplayanimation).toHaveBeenCalledWith('hood')
    expect(bridge.canplayanimation).toHaveBeenCalledWith('trunk')
    expect(bridge.canplayanimation).toHaveBeenCalledWith('wheel-spin')
  })

  it('动画能力首次瞬态失败后自动重试并保留菜单项', async () => {
    const user = userEvent.setup()
    let hoodAttempts = 0
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraId: 'wheel',
        animationEnabled: false,
        animationId: null,
        lightPreset: 'studio',
        renderMode: 'realtime',
        quality: 'epic',
        fullscreen: false,
      })),
      canplayanimation: vi.fn(async (animationId: string) => {
        if (animationId !== 'hood') return true
        hoodAttempts += 1
        return hoodAttempts > 1
      }),
      focusanimation: vi.fn().mockResolvedValue(true),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    await waitFor(() => expect(hoodAttempts).toBeGreaterThanOrEqual(2))
    const animation = screen.getByRole('button', { name: '动画' })
    await user.hover(animation)
    expect(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))
      .toBeInTheDocument()
  })

  it('镜头一级按钮循环，动画一级按钮按目录逐项开关循环', async () => {
    const user = userEvent.setup()
    let currentState = {
      cameraId: 'wheel',
      animationEnabled: false,
      animationId: null as string | null,
      lightPreset: 'studio',
      renderMode: 'realtime',
      quality: 'epic',
      fullscreen: false,
    }
    const bridge = {
      getpresentationstatejson: vi.fn(async () => JSON.stringify(currentState)),
      setcameraid: vi.fn(async (cameraId: string) => {
        currentState = { ...currentState, cameraId }
        return true
      }),
      focusanimation: vi.fn(async (animationId: string) => {
        currentState = {
          ...currentState,
          animationEnabled: animationId !== '',
          animationId: animationId || null,
        }
        return true
      }),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const cameraButton = screen.getByRole('button', { name: '镜头' })
    const animationButton = await screen.findByRole('button', { name: '动画' })
    await user.click(cameraButton)
    await waitFor(() => expect(bridge.setcameraid).toHaveBeenCalledWith('driver'))
    await user.click(animationButton)
    await waitFor(() => expect(bridge.focusanimation).toHaveBeenNthCalledWith(1, 'hood'))
    await user.click(animationButton)
    await waitFor(() => expect(bridge.focusanimation).toHaveBeenNthCalledWith(2, ''))
    await user.click(animationButton)
    await waitFor(() => expect(bridge.focusanimation).toHaveBeenNthCalledWith(3, 'trunk'))

    await user.hover(cameraButton)
    expect(await screen.findByRole('menu', { name: '镜头预设' })).toBeInTheDocument()
    await user.hover(screen.getByRole('button', { name: '场景' }))
    expect(screen.queryByRole('menu', { name: '镜头预设' })).not.toBeInTheDocument()
    expect(await screen.findByRole('menu', { name: '场景预设' })).toBeInTheDocument()
  })

  it('已有非当前目录动画时一级动画先关闭，再从目录首项继续', async () => {
    const user = userEvent.setup()
    let currentState = {
      cameraId: 'wheel',
      animationEnabled: true,
      animationId: 'door-right' as string | null,
      lightPreset: 'studio',
      renderMode: 'realtime',
      quality: 'epic',
      fullscreen: false,
    }
    const bridge = {
      getpresentationstatejson: vi.fn(async () => JSON.stringify(currentState)),
      focusanimation: vi.fn(async (animationId: string) => {
        currentState = {
          ...currentState,
          animationEnabled: animationId !== '',
          animationId: animationId || null,
        }
        return true
      }),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const animationButton = await screen.findByRole('button', { name: '动画' })
    await user.click(animationButton)
    await waitFor(() => expect(bridge.focusanimation).toHaveBeenNthCalledWith(1, ''))
    await user.click(animationButton)
    await waitFor(() => expect(bridge.focusanimation).toHaveBeenNthCalledWith(2, 'hood'))
    await user.unhover(animationButton)
    await user.hover(animationButton)
    expect(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))
      .toHaveAttribute('aria-checked', 'true')
  })

  it('Path Tracing 被拒绝时显示 UE 返回的具体原因', async () => {
    const user = userEvent.setup()
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraId: 'wheel',
        animationEnabled: false,
        animationId: null,
        lightPreset: 'studio',
        renderMode: 'realtime',
        renderAvailability: 'ready',
        quality: 'high',
        fullscreen: false,
      })),
      setrendermode: vi.fn().mockResolvedValue(false),
      getrendermodeerror: vi.fn()
        .mockResolvedValue('当前 GPU、RHI 或 Shader Platform 不支持 Path Tracing。'),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    await waitFor(() => expect(bridge.getpresentationstatejson).toHaveBeenCalled())
    await user.click(screen.getByRole('button', { name: '渲染' }))

    expect(await screen.findByRole('alert'))
      .toHaveTextContent('当前 GPU、RHI 或 Shader Platform 不支持 Path Tracing。')
    expect(bridge.getrendermodeerror).toHaveBeenCalledOnce()
  })

  it('预热期间禁用渲染按钮并显示非环形旋转标识', async () => {
    const user = userEvent.setup()
    let renderAvailability: 'preparing' | 'ready' = 'preparing'
    const setrendermode = vi.fn().mockResolvedValue(true)
    const bridge = {
      getpresentationstatejson: vi.fn(async () => JSON.stringify({
        cameraId: 'wheel',
        animationEnabled: false,
        animationId: null,
        lightPreset: 'studio',
        renderMode: 'realtime',
        renderAvailability,
        quality: 'high',
        fullscreen: false,
      })),
      setrendermode,
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const renderButton = screen.getByRole('button', { name: '渲染' })
    await waitFor(() => expect(renderButton).toBeDisabled())
    expect(renderButton).toHaveAttribute('aria-busy', 'true')
    expect(renderButton.querySelector('.render-warmup-loader')).toBeInTheDocument()
    expect(renderButton.querySelector('.render-progress')).not.toBeInTheDocument()
    expect(renderButton.querySelectorAll('polygon')).toHaveLength(4)
    await user.click(renderButton)
    expect(setrendermode).not.toHaveBeenCalled()

    renderAvailability = 'ready'
    await waitFor(() => expect(renderButton).toBeEnabled())
    expect(renderButton).toHaveAttribute('aria-busy', 'false')
    expect(renderButton.querySelector('.render-warmup-loader')).not.toBeInTheDocument()
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

    await userEvent.hover(screen.getByRole('button', { name: '镜头' }))
    expect(await screen.findByRole('menuitemradio', { name: '驾驶位' }))
      .toHaveAttribute('aria-checked', 'true')
    expect(screen.getByRole('menuitemradio', { name: '前舱' }))
      .toHaveAttribute('aria-checked', 'false')
  })

  it('车内镜头点击后不等待 UE 命令返回就更新选中标记', async () => {
    const user = userEvent.setup()
    let currentState = {
      cameraId: 'wheel',
      animationEnabled: false,
      lightPreset: 'studio',
      renderMode: 'realtime',
      quality: 'high',
      fullscreen: false,
    }
    let resolveCameraCommand: ((accepted: boolean) => void) | undefined
    const bridge = {
      getpresentationstatejson: vi.fn(async () => JSON.stringify(currentState)),
      setcameraid: vi.fn(() => new Promise<boolean>((resolve) => {
        resolveCameraCommand = resolve
      })),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    await waitFor(() => expect(bridge.getpresentationstatejson).toHaveBeenCalled())
    const cameraButton = screen.getByRole('button', { name: '镜头' })
    await user.hover(cameraButton)
    const menu = await screen.findByRole('menu', { name: '镜头预设' })
    fireEvent.click(within(menu).getByRole('menuitemradio', { name: '驾驶位' }))

    await waitFor(() => expect(within(menu).getByRole('menuitemradio', { name: '驾驶位' }))
      .toHaveAttribute('aria-checked', 'true'))
    expect(within(menu).getByRole('menuitemradio', { name: '轮毂' }))
      .toHaveAttribute('aria-checked', 'false')

    currentState = { ...currentState, cameraId: 'driver' }
    await act(async () => resolveCameraCommand?.(true))
    await user.unhover(cameraButton)
    await waitFor(() => {
      expect(screen.queryByRole('menu', { name: '镜头预设' })).not.toBeInTheDocument()
    })
  })

  it('UE 未确认镜头切换时回滚到已确认镜头', async () => {
    const user = userEvent.setup()
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
        cameraId: 'wheel',
        animationEnabled: false,
        lightPreset: 'studio',
        renderMode: 'realtime',
        quality: 'high',
        fullscreen: false,
      })),
      setcameraid: vi.fn().mockResolvedValue(true),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    await waitFor(() => expect(bridge.getpresentationstatejson).toHaveBeenCalled())
    const cameraButton = screen.getByRole('button', { name: '镜头' })
    await user.hover(cameraButton)
    fireEvent.click(await screen.findByRole('menuitemradio', { name: '驾驶位' }))
    await waitFor(() => expect(bridge.setcameraid).toHaveBeenCalledWith('driver'))
    await user.unhover(cameraButton)
    await user.hover(cameraButton)
    expect(await screen.findByRole('menuitemradio', { name: '驾驶位' }))
      .toHaveAttribute('aria-checked', 'true')
    expect(screen.getByRole('menuitemradio', { name: '轮毂' }))
      .toHaveAttribute('aria-checked', 'false')

    await waitFor(() => expect(screen.getByRole('alert'))
      .toHaveTextContent('UE 镜头切换未确认'), { timeout: 1500 })
    expect(screen.getByRole('menuitemradio', { name: '驾驶位' }))
      .toHaveAttribute('aria-checked', 'false')
    expect(screen.getByRole('menuitemradio', { name: '轮毂' }))
      .toHaveAttribute('aria-checked', 'true')
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

    await user.hover(screen.getByRole('button', { name: '镜头' }))
    const menu = await screen.findByRole('menu', { name: '镜头预设' })
    expect(within(menu).getByRole('menuitemradio', { name: '轮毂' }))
      .toHaveAttribute('aria-checked', 'true')

    fireEvent.click(within(menu).getByRole('menuitemradio', { name: '驾驶位' }))
    await waitFor(() => expect(bridge.setcamera).toHaveBeenCalledWith(4))

    await user.unhover(screen.getByRole('button', { name: '镜头' }))
    await user.hover(screen.getByRole('button', { name: '镜头' }))
    fireEvent.click(await screen.findByRole('menuitemradio', { name: '座椅' }))
    expect(bridge.setcamera).toHaveBeenCalledOnce()
    expect(await screen.findByRole('alert'))
      .toHaveTextContent('UE 控制桥缺少所需方法')
  })

  it('bridge 缺失时禁用动画并在 ready 后重新探测', async () => {
    const user = userEvent.setup()
    render(<ExperienceControls ueEnabled />)

    const animation = await screen.findByRole('button', { name: '动画' })
    expect(animation).toBeDisabled()
    expect(screen.getByRole('alert')).toHaveTextContent('UE 控制桥不可用')
    expect(animation).toHaveAttribute('aria-pressed', 'false')

    const canplayanimation = vi.fn().mockResolvedValue(true)
    window.ue = {
      uebridge: {
        getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify({
          cameraId: 'wheel',
          animationEnabled: false,
          animationId: null,
          lightPreset: 'studio',
          renderMode: 'realtime',
          quality: 'epic',
          fullscreen: false,
        })),
        focusanimation: vi.fn().mockResolvedValue(true),
        canplayanimation,
      },
    }
    await waitFor(() => expect(animation).toBeEnabled(), { timeout: 1500 })
    await waitFor(() => expect(canplayanimation).toHaveBeenCalledWith('hood'))
    await user.hover(animation)
    expect(await screen.findByRole('menuitemradio', { name: '开启机舱盖' }))
      .toBeInTheDocument()
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
