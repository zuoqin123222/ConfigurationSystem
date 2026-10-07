import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  applyUeConfiguration,
  canPlayUeAnimation,
  closeUeAnimation,
  createUeConfigurationJson,
  createUeConfiguratorHeaderStateJson,
  executeUeControl,
  getUeControlFailureKind,
  getUeControlFailureMessage,
  getUeConfiguratorHeaderState,
  getUeBridge,
  getUePresentationState,
  getUeRenderModeError,
  focusUeAnimation,
  isUeConfiguratorHeaderState,
  playUeAnimation,
  reportUeUiReady,
  setUeCameraId,
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

  it('只发送 v2 白名单 JSON 并校验 UE 材质事务回执', async () => {
    const receipt = {
      ok: true,
      code: 'APPLIED',
      message: '配置与可映射材质槽已原子应用。',
      configurationId: 'cfg-123',
      appliedSurfaceIds: ['exterior-body-cover', 'door-middle'],
      unsupportedSurfaceIds: [],
      appliedSlotIds: ['CS_Validation_Paint', 'CS_Validation_Interior'],
    }
    const applyconfigurationtransactionjson = vi.fn()
      .mockResolvedValue(JSON.stringify(receipt))
    const bridge = { applyconfigurationtransactionjson }
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

    await expect(applyUeConfiguration(bridge, selections, customizations))
      .resolves.toEqual(receipt)
    expect(applyconfigurationtransactionjson).toHaveBeenCalledWith(createUeConfigurationJson(
      selections,
      customizations,
    ))
    expect(JSON.parse(applyconfigurationtransactionjson.mock.calls[0][0])).toEqual({
      schemaVersion: '2.0.0',
      selections,
      customizations,
    })
  })

  it('缺少白名单入口时返回明确失败回执', async () => {
    await expect(applyUeConfiguration({}, {}, {})).resolves.toMatchObject({
      ok: false,
      code: 'BRIDGE_METHOD_UNAVAILABLE',
    })
  })

  it('明确返回 proxy capability 缺口并拒绝非法回执', async () => {
    const unsupported = {
      ok: true,
      code: 'APPLIED_WITH_UNSUPPORTED_SURFACES',
      message: '配置已应用；1 个 surfaceId 为当前 proxy capability 明确缺口。',
      configurationId: 'cfg-456',
      appliedSurfaceIds: [],
      unsupportedSurfaceIds: ['wheel-style'],
      appliedSlotIds: [],
    }
    const bridge = {
      applyconfigurationtransactionjson: vi.fn()
        .mockResolvedValueOnce(JSON.stringify(unsupported))
        .mockResolvedValueOnce('{"ok":true}'),
    }
    await expect(applyUeConfiguration(bridge, {}, {})).resolves.toEqual(unsupported)
    await expect(applyUeConfiguration(bridge, {}, {})).resolves.toMatchObject({
      ok: false,
      code: 'INVALID_UE_RECEIPT',
    })
  })

  it('允许目录阶段 ID 通过 bridge 联动并拒绝非法 ID', async () => {
    const setconfiguratorcategory = vi.fn().mockResolvedValue(true)
    const bridge = { setconfiguratorcategory }

    await expect(syncUeConfiguratorCategory(bridge, 'interior')).resolves.toBe(true)
    await expect(syncUeConfiguratorCategory(bridge, 'Invalid Category')).resolves.toBe(false)
    expect(setconfiguratorcategory).toHaveBeenCalledOnce()
    expect(setconfiguratorcategory).toHaveBeenCalledWith('interior')
  })

  it('语义相机优先调用 setcameraid，入口缺失时按 legacyIndex 调用 setcamera', async () => {
    const bridge = {
      setcameraid: vi.fn().mockResolvedValue(true),
      setcamera: vi.fn().mockResolvedValue(true),
    }
    const legacyBridge = {
      setcamera: vi.fn().mockResolvedValue(true),
    }

    await expect(setUeCameraId(bridge, 'front-cabin', 5)).resolves.toBe(true)
    await expect(setUeCameraId(bridge, 4)).resolves.toBe(true)
    await expect(setUeCameraId(legacyBridge, 'wheel', 2)).resolves.toBe(true)
    await expect(setUeCameraId(legacyBridge, 'seat', null)).resolves.toBe(false)
    await expect(setUeCameraId(bridge, 'Invalid Camera')).resolves.toBe(false)

    expect(bridge.setcameraid).toHaveBeenCalledWith('front-cabin')
    expect(bridge.setcamera).toHaveBeenCalledWith(4)
    expect(legacyBridge.setcamera).toHaveBeenCalledOnce()
    expect(legacyBridge.setcamera).toHaveBeenCalledWith(2)
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

  it('Header 动作只允许 save、share 和 reset 三个固定值', async () => {
    const triggerconfiguratorheaderaction = vi.fn().mockResolvedValue(true)
    const bridge = { triggerconfiguratorheaderaction }

    await expect(triggerUeConfiguratorHeaderAction(bridge, 'save')).resolves.toBe(true)
    await expect(triggerUeConfiguratorHeaderAction(bridge, 'share')).resolves.toBe(true)
    await expect(triggerUeConfiguratorHeaderAction(bridge, 'reset')).resolves.toBe(true)
    await expect(triggerUeConfiguratorHeaderAction(
      bridge,
      'debug' as 'save',
    )).resolves.toBe(false)
    expect(triggerconfiguratorheaderaction.mock.calls).toEqual([
      ['save'],
      ['share'],
      ['reset'],
    ])
  })

  it('await CEF Promise 并把语义镜头及其余显式命令路由到对应 bridge 方法', async () => {
    const bridge = {
      setcamera: vi.fn().mockResolvedValue(true),
      setcameraid: vi.fn().mockResolvedValue(true),
      setanimationenabled: vi.fn().mockResolvedValue(true),
      playanimation: vi.fn().mockResolvedValue(true),
      closeanimation: vi.fn().mockResolvedValue(true),
      setlightpreset: vi.fn().mockResolvedValue(true),
      setrendermode: vi.fn().mockResolvedValue(true),
      setqualitylevel: vi.fn().mockResolvedValue(true),
      resetpresentation: vi.fn().mockResolvedValue(true),
      setfullscreen: vi.fn().mockResolvedValue(true),
    }

    await expect(executeUeControl(bridge, {
      type: 'camera',
      cameraId: 'front-cabin',
      legacyIndex: 5,
    })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'camera', cameraIndex: 5 })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'animation', enabled: true })).resolves.toBe(true)
    await expect(executeUeControl(
      bridge,
      { type: 'play-animation', animationId: 'hood' },
    )).resolves.toBe(true)
    await expect(executeUeControl(
      bridge,
      { type: 'close-animation', animationId: 'hood' },
    )).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'light', preset: 'outdoor' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'render', mode: 'path-tracing' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'quality', quality: 'epic' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'reset' })).resolves.toBe(true)
    await expect(executeUeControl(bridge, { type: 'fullscreen', enabled: true })).resolves.toBe(true)

    expect(bridge.setcameraid).toHaveBeenCalledWith('front-cabin')
    expect(bridge.setcamera).toHaveBeenCalledWith(5)
    expect(bridge.setanimationenabled).toHaveBeenCalledWith(true)
    expect(bridge.playanimation).toHaveBeenCalledWith('hood')
    expect(bridge.closeanimation).toHaveBeenCalledWith('hood')
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
      cameraId: 'front-cabin',
      animationEnabled: true,
      animationId: 'hood',
      lightPreset: 'outdoor',
      renderMode: 'path-tracing',
      renderProgress: 0.42,
      renderAvailability: 'ready',
      quality: 'epic',
      fullscreen: true,
    } as const
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify(state)),
    }

    await expect(getUePresentationState(bridge)).resolves.toEqual(state)
    bridge.getpresentationstatejson.mockResolvedValue(JSON.stringify({
      ...state,
      animationId: '../hood',
    }))
    await expect(getUePresentationState(bridge)).resolves.toBeNull()
    bridge.getpresentationstatejson.mockResolvedValue(JSON.stringify({
      ...state,
      renderProgress: 1.2,
    }))
    await expect(getUePresentationState(bridge)).resolves.toBeNull()
    bridge.getpresentationstatejson.mockResolvedValue(JSON.stringify({
      ...state,
      renderAvailability: 'waiting',
    }))
    await expect(getUePresentationState(bridge)).resolves.toBeNull()
    bridge.getpresentationstatejson.mockResolvedValue('{"cameraIndex":99}')
    await expect(getUePresentationState(bridge)).resolves.toBeNull()
  })

  it('读取并限制 UE 渲染模式失败原因', async () => {
    const bridge = {
      getrendermodeerror: vi.fn()
        .mockResolvedValueOnce('  当前 GPU 不支持 Path Tracing。  ')
        .mockResolvedValueOnce('x'.repeat(513)),
    }

    await expect(getUeRenderModeError(bridge))
      .resolves.toBe('当前 GPU 不支持 Path Tracing。')
    await expect(getUeRenderModeError(bridge)).resolves.toBe('')
    await expect(getUeRenderModeError({})).resolves.toBe('')
  })

  it('动画能力检查要求可执行 bridge，并兼容具备播放/关闭入口的旧 bridge', async () => {
    const canplayanimation = vi.fn().mockResolvedValue(false)
    await expect(canPlayUeAnimation({
      canplayanimation,
      focusanimation: vi.fn(),
    }, 'door-left'))
      .resolves.toBe(false)
    expect(canplayanimation).toHaveBeenCalledWith('door-left')
    await expect(canPlayUeAnimation(null, 'hood')).resolves.toBe(false)
    await expect(canPlayUeAnimation({}, 'hood')).resolves.toBe(false)
    await expect(canPlayUeAnimation({
      playanimation: vi.fn(),
      closeanimation: vi.fn(),
    }, 'hood')).resolves.toBe(true)
    await expect(canPlayUeAnimation({
      canplayanimation,
      focusanimation: vi.fn(),
    }, '../hood')).resolves.toBe(false)
  })

  it('区分 bridge、方法缺失与命令拒绝错误', () => {
    const command = { type: 'play-animation', animationId: 'hood' } as const
    expect(getUeControlFailureKind(null, command)).toBe('bridge-unavailable')
    expect(getUeControlFailureKind({}, command)).toBe('method-unavailable')
    expect(getUeControlFailureKind({ playanimation: vi.fn() }, command))
      .toBe('command-rejected')
    expect(getUeControlFailureMessage('bridge-unavailable')).toBe('UE 控制桥不可用')
    expect(getUeControlFailureMessage('method-unavailable'))
      .toBe('UE 控制桥缺少所需方法')
    expect(getUeControlFailureMessage('command-rejected'))
      .toBe('UE 命令被拒绝或执行失败')
  })

  it('向 UE 报告三块 CEF 的稳定视图标识', async () => {
    const reportuiready = vi.fn().mockResolvedValue(true)
    window.ue = { uebridge: { reportuiready } }

    await expect(reportUeUiReady('embedded')).resolves.toBe(true)
    await expect(reportUeUiReady('controls')).resolves.toBe(true)
    await expect(reportUeUiReady('header')).resolves.toBe(true)
    expect(reportuiready.mock.calls.map(([viewId]) => viewId))
      .toEqual(['embedded', 'controls', 'header'])
  })

  it('继续读取只有 cameraIndex 的旧 UE 展示状态', async () => {
    const state = {
      cameraIndex: 4,
      animationEnabled: false,
      lightPreset: 'studio',
      renderMode: 'realtime',
      quality: 'high',
      fullscreen: false,
    } as const
    const bridge = {
      getpresentationstatejson: vi.fn().mockResolvedValue(JSON.stringify(state)),
    }

    await expect(getUePresentationState(bridge)).resolves.toEqual(state)
  })

  it('焦点动画优先调用 UE 原子接口，并拒绝非法 ID', async () => {
    const focusanimation = vi.fn().mockResolvedValue(true)
    const bridge = {
      focusanimation,
      playanimation: vi.fn().mockResolvedValue(true),
      closeanimation: vi.fn().mockResolvedValue(true),
    }

    await expect(focusUeAnimation(bridge, 'hood', 'trunk')).resolves.toBe(true)
    expect(focusanimation).toHaveBeenCalledWith('trunk')
    expect(bridge.closeanimation).not.toHaveBeenCalled()
    expect(bridge.playanimation).not.toHaveBeenCalled()
    await expect(focusUeAnimation(bridge, 'trunk', null)).resolves.toBe(true)
    expect(focusanimation).toHaveBeenLastCalledWith('')
    await expect(focusUeAnimation(bridge, 'hood', '-invalid')).resolves.toBe(false)
  })

  it('旧 bridge 切换失败时恢复原动画', async () => {
    const bridge = {
      playanimation: vi.fn()
        .mockResolvedValueOnce(false)
        .mockResolvedValueOnce(true),
      closeanimation: vi.fn().mockResolvedValue(true),
    }

    await expect(focusUeAnimation(bridge, 'hood', 'trunk')).resolves.toBe(false)
    expect(bridge.closeanimation).toHaveBeenCalledWith('hood')
    expect(bridge.playanimation).toHaveBeenNthCalledWith(1, 'trunk')
    expect(bridge.playanimation).toHaveBeenNthCalledWith(2, 'hood')
    await expect(playUeAnimation(bridge, 'Invalid Animation')).resolves.toBe(false)
    await expect(closeUeAnimation(bridge, '../hood')).resolves.toBe(false)
  })
})
