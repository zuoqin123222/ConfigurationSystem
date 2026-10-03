import { render, screen, waitFor, within } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it, vi } from 'vitest'
import ExperienceControls from './ExperienceControls'

describe('ExperienceControls', () => {
  afterEach(() => {
    delete window.ue
    document.documentElement.classList.remove('controls-document')
    document.body.classList.remove('controls-document')
  })

  it('通过受限 bridge 控制镜头、动画、灯光、渲染、复位和全屏', async () => {
    const user = userEvent.setup()
    const initialState = {
      cameraIndex: 1,
      animationEnabled: false,
      lightPreset: 'studio',
      renderMode: 'realtime',
      quality: 'high',
      fullscreen: false,
    }
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify(initialState)),
      setcamera: vi.fn().mockResolvedValue(true),
      setanimationenabled: vi.fn().mockResolvedValue(true),
      setlightpreset: vi.fn().mockResolvedValue(true),
      setrendermode: vi.fn().mockResolvedValue(true),
      setqualitylevel: vi.fn().mockResolvedValue(true),
      resetpresentation: vi.fn().mockResolvedValue(true),
      setfullscreen: vi.fn().mockResolvedValue(true),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const toolbar = screen.getByRole('navigation', { name: '体验控制' })
    expect(within(toolbar).getAllByRole('button')).toHaveLength(7)
    await waitFor(() => expect(bridge.getpresentationstatejson).toHaveBeenCalled())

    await user.click(within(toolbar).getByRole('button', { name: '镜头' }))
    await user.click(screen.getByRole('menuitemradio', { name: '驾驶位' }))
    await user.click(within(toolbar).getByRole('button', { name: '动画' }))
    await user.click(within(toolbar).getByRole('button', { name: '灯光' }))
    await user.click(within(toolbar).getByRole('button', { name: '渲染' }))
    await user.click(within(toolbar).getByRole('button', { name: '画质' }))
    await user.click(screen.getByRole('menuitemradio', { name: '极高' }))
    await user.click(within(toolbar).getByRole('button', { name: '复位' }))
    await user.click(within(toolbar).getByRole('button', { name: '全屏' }))

    expect(bridge.setcamera).toHaveBeenCalledWith(4)
    expect(bridge.setanimationenabled).toHaveBeenCalledWith(true)
    expect(bridge.setlightpreset).toHaveBeenCalledWith('outdoor')
    expect(bridge.setrendermode).toHaveBeenCalledWith('path-tracing')
    expect(bridge.setqualitylevel).toHaveBeenCalledWith('epic')
    expect(bridge.resetpresentation).toHaveBeenCalledOnce()
    expect(bridge.setfullscreen).toHaveBeenCalledWith(true)
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
      setanimationenabled: vi.fn().mockRejectedValue(new Error('rejected')),
    }
    window.ue = { uebridge: bridge }
    render(<ExperienceControls ueEnabled />)

    const animation = screen.getByRole('button', { name: '动画' })
    await waitFor(() => expect(animation).toHaveAttribute('aria-pressed', 'true'))
    await user.click(animation)

    expect(await screen.findByRole('alert'))
      .toHaveTextContent('UE 控制桥不可用或命令被拒绝')
    expect(animation).toHaveAttribute('aria-pressed', 'true')
  })

  it('bridge 缺失时显示错误且不伪造激活状态', async () => {
    const user = userEvent.setup()
    render(<ExperienceControls ueEnabled />)

    const animation = screen.getByRole('button', { name: '动画' })
    await user.click(animation)

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
})
