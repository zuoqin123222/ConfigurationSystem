import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  applyUeConfiguration,
  createUeConfigurationJson,
  createUeConfiguratorHeaderStateJson,
  executeUeControl,
  getUeConfiguratorHeaderState,
  getUeBridge,
  getUePresentationState,
  isUeConfiguratorHeaderState,
  syncUeConfiguratorCategory,
  syncUeConfiguratorHeaderState,
  triggerUeConfiguratorHeaderAction,
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

  it('只允许完整合法的 Header 状态通过 bridge 转发', async () => {
    const setconfiguratorheaderstatejson = vi.fn().mockResolvedValue(true)
    const bridge = { setconfiguratorheaderstatejson }
    const state = {
      categoryId: 'interior',
      referenceTotalMinor: 23940000,
      syncState: 'saved',
      syncMessage: '已同步 · revision 2',
      dirty: false,
      online: true,
    } as const

    expect(isUeConfiguratorHeaderState(state)).toBe(true)
    await expect(syncUeConfiguratorHeaderState(bridge, state)).resolves.toBe(true)
    expect(setconfiguratorheaderstatejson).toHaveBeenCalledWith(
      createUeConfiguratorHeaderStateJson(state),
    )

    await expect(syncUeConfiguratorHeaderState(bridge, {
      ...state,
      referenceTotalMinor: -1,
    })).resolves.toBe(false)
    expect(isUeConfiguratorHeaderState({ ...state, injected: 'console' })).toBe(false)
    expect(setconfiguratorheaderstatejson).toHaveBeenCalledOnce()
  })

  it('Header 挂载后可回读最后一次合法状态', async () => {
    const state = {
      categoryId: 'performance',
      referenceTotalMinor: 24610000,
      syncState: 'idle',
      syncMessage: '未同步更改',
      dirty: true,
      online: true,
    } as const
    const bridge = {
      getconfiguratorheaderstatejson: vi.fn().mockResolvedValue(JSON.stringify(state)),
    }

    await expect(getUeConfiguratorHeaderState(bridge)).resolves.toEqual(state)
    bridge.getconfiguratorheaderstatejson.mockResolvedValue('{"categoryId":"unknown"}')
    await expect(getUeConfiguratorHeaderState(bridge)).resolves.toBeNull()
  })

  it('Header 动作只允许 save 和 share 两个固定值', async () => {
    const triggerconfiguratorheaderaction = vi.fn().mockResolvedValue(true)
    const bridge = { triggerconfiguratorheaderaction }

    await expect(triggerUeConfiguratorHeaderAction(bridge, 'save')).resolves.toBe(true)
    await expect(triggerUeConfiguratorHeaderAction(bridge, 'share')).resolves.toBe(true)
    await expect(triggerUeConfiguratorHeaderAction(
      bridge,
      'debug' as 'save',
    )).resolves.toBe(false)
    expect(triggerconfiguratorheaderaction.mock.calls).toEqual([['save'], ['share']])
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
