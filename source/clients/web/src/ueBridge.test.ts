import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  applyUeConfiguration,
  createUeConfigurationJson,
  executeUeControl,
  getUeBridge,
  getUePresentationState,
  syncUeConfiguratorCategory,
  type UeControlCommand,
} from './ueBridge'

describe('受限 UE bridge', () => {
  afterEach(() => {
    delete window.ue
  })

  it('只在 embedded 模式发现 bridge', () => {
    window.ue = { uebridge: { applyconfigurationjson: vi.fn() } }

    expect(getUeBridge(false)).toBeNull()
    expect(getUeBridge(true)).toBe(window.ue.uebridge)
  })

  it('只发送 v2 selections 和 customizations 白名单 JSON', () => {
    const applyconfigurationjson = vi.fn()
    const bridge = { applyconfigurationjson }
    const selections = {
      'exterior-body-cover': 'body-cover-custom',
      'door-middle': 'door-middle-leather',
    }
    const customizations = {
      'exterior-body-cover': {
        colorHex: '#123456',
        metallic: 0.4,
        roughness: 0.2,
        clearCoat: 0.8,
        orangePeel: 0.1,
        flakeIntensity: 0.3,
      },
      'door-middle': { materialVariantId: 'leather-p10-1217' },
    }

    expect(applyUeConfiguration(bridge, selections, customizations)).toBe(true)
    expect(applyconfigurationjson).toHaveBeenCalledWith(createUeConfigurationJson(
      selections,
      customizations,
    ))
    expect(JSON.parse(applyconfigurationjson.mock.calls[0][0])).toEqual({
      schemaVersion: '2.0.0',
      selections,
      customizations,
    })
  })

  it('缺少白名单入口时不尝试调用其他 UE 能力', () => {
    expect(applyUeConfiguration({}, {}, {})).toBe(false)
  })

  it('只允许四个固定阶段通过 bridge 联动选配右栏', async () => {
    const setconfiguratorcategory = vi.fn().mockResolvedValue(true)
    const bridge = { setconfiguratorcategory }

    await expect(syncUeConfiguratorCategory(bridge, 'interior')).resolves.toBe(true)
    await expect(syncUeConfiguratorCategory(bridge, 'unknown')).resolves.toBe(false)
    expect(setconfiguratorcategory).toHaveBeenCalledOnce()
    expect(setconfiguratorcategory).toHaveBeenCalledWith('interior')
  })

  it('await CEF Promise 并只把七类显式命令路由到对应的 bridge 方法', async () => {
    const bridge = {
      setcamera: vi.fn().mockResolvedValue(true),
      setanimationenabled: vi.fn().mockResolvedValue(true),
      setlightpreset: vi.fn().mockResolvedValue(true),
      setrendermode: vi.fn().mockResolvedValue(true),
      setqualitylevel: vi.fn().mockResolvedValue(true),
      resetpresentation: vi.fn().mockResolvedValue(true),
      setfullscreen: vi.fn().mockResolvedValue(true),
    }

    await expect(executeUeControl(bridge, { type: 'camera', cameraIndex: 5 })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'animation', enabled: true })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'light', preset: 'outdoor' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'render', mode: 'path-tracing' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'quality', quality: 'epic' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'reset' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'fullscreen', enabled: true })).resolves.toBe(true)

    expect(bridge.setcamera).toHaveBeenCalledWith(5)
    expect(bridge.setanimationenabled).toHaveBeenCalledWith(true)
    expect(bridge.setlightpreset).toHaveBeenCalledWith('outdoor')
    expect(bridge.setrendermode).toHaveBeenCalledWith('path-tracing')
    expect(bridge.setqualitylevel).toHaveBeenCalledWith('epic')
    expect(bridge.resetpresentation).toHaveBeenCalledOnce()
    expect(bridge.setfullscreen).toHaveBeenCalledWith(true)
  })

  it('拒绝越界参数、未知命令和缺少入口的调用', async () => {
    const setcamera = vi.fn().mockResolvedValue(true)
    const bridge = { setcamera }

    await expect(executeUeControl(
      bridge,
      { type: 'camera', cameraIndex: 6 } as unknown as UeControlCommand,
    )).resolves.toBe(false)
    await expect(executeUeControl(
      bridge,
      { type: 'console', command: 'quit' } as unknown as UeControlCommand,
    )).resolves.toBe(false)
    await expect(executeUeControl(bridge, { type: 'reset' })).resolves.toBe(false)
    expect(setcamera).not.toHaveBeenCalled()
  })

  it('将 Promise false 和 rejection 都作为拒绝而不是成功', async () => {
    const bridge = {
      setanimationenabled: vi.fn()
        .mockResolvedValueOnce(false)
        .mockRejectedValueOnce(new Error('CEF rejected')),
    }

    await expect(executeUeControl(
      bridge,
      { type: 'animation', enabled: true },
    )).resolves.toBe(false)
    await expect(executeUeControl(
      bridge,
      { type: 'animation', enabled: true },
    )).resolves.toBe(false)
  })

  it('读取并校验 UE 展示状态 JSON', async () => {
    const state = {
      cameraIndex: 4,
      animationEnabled: true,
      lightPreset: 'outdoor',
      renderMode: 'path-tracing',
      quality: 'epic',
      fullscreen: true,
    } as const
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify(state)),
    }

    await expect(getUePresentationState(bridge)).resolves.toEqual(state)
    bridge.getpresentationstatejson.mockResolvedValue('{"cameraIndex":99}')
    await expect(getUePresentationState(bridge)).resolves.toBeNull()
  })
})
