import { describe, expect, it, vi } from 'vitest'
import { runCefBridgeProbe } from './cefBridgeProbe'
import { catalogFixture } from './test/catalogFixture'

describe('Shipping CEF bridge probe', () => {
  it('验证五动画 canplay、focus、清除和执行器状态', async () => {
    let focusedId: string | null = null
    const completecefbridgeprobe = vi.fn().mockResolvedValue(true)
    const bridge = {
      canplayanimation: vi.fn().mockResolvedValue(true),
      focusanimation: vi.fn(async (animationId: string) => {
        focusedId = animationId || null
        return true
      }),
      getpresentationstatejson: vi.fn(async () => JSON.stringify({
        cameraId: 'wheel',
        animationEnabled: focusedId !== null,
        animationId: focusedId,
        lightPreset: 'studio',
        renderMode: 'realtime',
        quality: 'epic',
        fullscreen: false,
      })),
      getanimationexecutorstatejson: vi.fn(async (animationId: string) => {
        return JSON.stringify({
          animationId,
          canPlay: true,
          active: focusedId === animationId,
          focused: focusedId === animationId,
          executor: 'sequence',
          currentFrame: focusedId === animationId ? 1 : 0,
          direction: 1,
          moving: focusedId === animationId,
        })
      }),
      completecefbridgeprobe,
    }

    const animations = [
      catalogFixture.animations[0],
      {
        ...catalogFixture.animations[0],
        animationId: 'door-left',
        displayName: '开启左车门',
      },
      {
        ...catalogFixture.animations[0],
        animationId: 'door-right',
        displayName: '开启右车门',
      },
      catalogFixture.animations[1],
      catalogFixture.animations[2],
    ]
    await runCefBridgeProbe(bridge, animations)

    expect(bridge.canplayanimation.mock.calls.map(([id]) => id)).toEqual([
      'hood',
      'door-left',
      'door-right',
      'trunk',
      'wheel-spin',
    ])
    expect(bridge.focusanimation).toHaveBeenCalledTimes(10)
    expect(completecefbridgeprobe).toHaveBeenCalledOnce()
    const report = JSON.parse(completecefbridgeprobe.mock.calls[0][0])
    expect(report.ok).toBe(true)
    expect(report.steps).toHaveLength(5)
    expect(report.steps.every(
      (step: {
        focusPassed: boolean
        clearPassed: boolean
        executor: { executor: string }
      }) =>
        step.focusPassed
        && step.clearPassed
        && step.executor.executor === 'sequence',
    )).toBe(true)
  })
})
