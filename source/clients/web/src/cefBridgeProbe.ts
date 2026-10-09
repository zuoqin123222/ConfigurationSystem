import type { CatalogAnimation } from './types'
import type { ReflectedUeBridge } from './ueBridge'

const delay = (milliseconds: number) => new Promise<void>((resolve) => {
  window.setTimeout(resolve, milliseconds)
})

interface ExecutorState {
  animationId: string
  canPlay: boolean
  active: boolean
  focused: boolean
  executor: 'sequence' | 'part-actuator' | 'wheel' | 'none'
  moving: boolean
  currentFrame?: number
  direction?: number
  openRequested?: boolean
  enabled?: boolean
}

async function readExecutor(
  bridge: ReflectedUeBridge,
  animationId: string,
): Promise<ExecutorState> {
  if (typeof bridge.getanimationexecutorstatejson !== 'function') {
    throw new Error('METHOD_UNAVAILABLE:getanimationexecutorstatejson')
  }
  return JSON.parse(
    await bridge.getanimationexecutorstatejson(animationId),
  ) as ExecutorState
}

export async function runCefBridgeProbe(
  bridge: ReflectedUeBridge,
  animations: CatalogAnimation[],
): Promise<void> {
  const expectedIds = ['hood', 'door-left', 'door-right', 'trunk', 'wheel-spin']
  const catalogIds = animations.map((animation) => animation.animationId)
  const steps: Record<string, unknown>[] = []
  let ok = JSON.stringify(catalogIds) === JSON.stringify(expectedIds)
  let error = ok ? '' : `CATALOG_ANIMATIONS:${catalogIds.join(',')}`
  try {
    if (typeof bridge.canplayanimation !== 'function'
      || typeof bridge.focusanimation !== 'function'
      || typeof bridge.getpresentationstatejson !== 'function'
      || typeof bridge.completecefbridgeprobe !== 'function') {
      throw new Error('METHOD_UNAVAILABLE:cef-probe')
    }
    for (const animationId of expectedIds) {
      const canPlay = await bridge.canplayanimation(animationId)
      const focused = canPlay && await bridge.focusanimation(animationId)
      const presentation = JSON.parse(await bridge.getpresentationstatejson())
      const executor = await readExecutor(bridge, animationId)
      const focusPassed = focused
        && presentation.animationId === animationId
        && executor.canPlay
        && executor.focused
        && executor.executor === 'sequence'
      const cleared = await bridge.focusanimation('')
      let clearedExecutor = await readExecutor(bridge, animationId)
      for (let attempt = 0;
        attempt < 30 && (clearedExecutor.active || clearedExecutor.moving);
        attempt += 1) {
        await delay(100)
        clearedExecutor = await readExecutor(bridge, animationId)
      }
      const clearedPresentation = JSON.parse(
        await bridge.getpresentationstatejson(),
      )
      const clearPassed = cleared
        && clearedPresentation.animationId === null
        && !clearedExecutor.active
        && !clearedExecutor.focused
        && !clearedExecutor.moving
        && (clearedExecutor.executor !== 'part-actuator'
          || clearedExecutor.openRequested === false)
        && (clearedExecutor.executor !== 'wheel'
          || clearedExecutor.enabled === false)
      steps.push({
        animationId,
        canPlay,
        focused,
        focusPassed,
        cleared,
        clearPassed,
        executor,
        clearedExecutor,
      })
      ok = ok && focusPassed && clearPassed
    }
  } catch (cause) {
    ok = false
    error = cause instanceof Error ? cause.message : String(cause)
  }
  await bridge.completecefbridgeprobe?.(JSON.stringify({
    schemaVersion: 1,
    probe: 'ShippingCefBridgeProbe',
    ok,
    error,
    expectedAnimationIds: expectedIds,
    steps,
  }))
}
