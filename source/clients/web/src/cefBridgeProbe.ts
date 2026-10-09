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

interface ConfiguratorProbeResult {
  ok: boolean
  error: string
  stages: Array<{
    categoryId: string
    regionName: string
    regionPresent: boolean
    partsPresent: boolean
    scrollTop: number | null
  }>
  personalization: ConfiguratorDomCheckpoint[]
}

interface ConfiguratorDomCheckpoint {
  id: string
  name: string
  regionName: string
  readyState: DocumentReadyState
  href: string
  rootPresent: boolean
  rootChildCount: number
  shellPresent: boolean
  panelPresent: boolean
  panelScrollPresent: boolean
  regionPresent: boolean
  partsPresent: boolean
  panelTextLength: number
  display: string
  visibility: string
  opacity: string
  width: number
  height: number
  scrollTop: number | null
  scrollHeight: number | null
  clientHeight: number | null
  colorCorrectionApplied: boolean
}

async function waitForElement(
  selector: string,
  timeoutMilliseconds = 5000,
): Promise<Element | null> {
  const deadline = Date.now() + timeoutMilliseconds
  while (Date.now() < deadline) {
    const element = document.querySelector(selector)
    if (element) return element
    await delay(50)
  }
  return null
}

function buttonWithText(name: string): HTMLButtonElement | undefined {
  return Array.from(document.querySelectorAll<HTMLButtonElement>('button'))
    .find((button) => button.textContent?.trim() === name)
}

function partButtonWithText(name: string): HTMLButtonElement | undefined {
  return Array.from(
    document.querySelectorAll<HTMLButtonElement>(
      '[aria-label="部件筛选"] button',
    ),
  ).find((button) => button.textContent?.trim() === name)
}

async function waitForPartButton(
  name: string,
  timeoutMilliseconds = 5000,
): Promise<HTMLButtonElement | null> {
  const deadline = Date.now() + timeoutMilliseconds
  while (Date.now() < deadline) {
    const button = partButtonWithText(name)
    if (button) return button
    await delay(50)
  }
  return null
}

function readConfiguratorCheckpoint(
  id: string,
  name: string,
  regionName: string,
): ConfiguratorDomCheckpoint {
  const root = document.querySelector<HTMLElement>('#root')
  const shell = document.querySelector<HTMLElement>('.app-shell.embedded')
  const panel = document.querySelector<HTMLElement>('.config-panel')
  const panelScroll = document.querySelector<HTMLElement>('.panel-scroll')
  const region = document.querySelector<HTMLElement>(
    `[aria-label="${regionName}"]`,
  )
  const parts = document.querySelector<HTMLElement>('[aria-label="部件筛选"]')
  const style = panel ? window.getComputedStyle(panel) : null
  const bounds = panel?.getBoundingClientRect()
  return {
    id,
    name,
    regionName,
    readyState: document.readyState,
    href: window.location.href,
    rootPresent: root !== null,
    rootChildCount: root?.childElementCount ?? 0,
    shellPresent: shell !== null,
    panelPresent: panel !== null,
    panelScrollPresent: panelScroll !== null,
    regionPresent: region !== null,
    partsPresent: parts !== null,
    panelTextLength: panel?.innerText.length ?? 0,
    display: style?.display ?? '',
    visibility: style?.visibility ?? '',
    opacity: style?.opacity ?? '',
    width: Math.round(bounds?.width ?? 0),
    height: Math.round(bounds?.height ?? 0),
    scrollTop: panelScroll?.scrollTop ?? null,
    scrollHeight: panelScroll?.scrollHeight ?? null,
    clientHeight: panelScroll?.clientHeight ?? null,
    colorCorrectionApplied:
      document.querySelector('.ue-color-corrected') !== null,
  }
}

function checkpointPassed(checkpoint: ConfiguratorDomCheckpoint): boolean {
  return checkpoint.readyState !== 'loading'
    && checkpoint.rootPresent
    && checkpoint.rootChildCount > 0
    && checkpoint.shellPresent
    && checkpoint.panelPresent
    && checkpoint.panelScrollPresent
    && checkpoint.regionPresent
    && checkpoint.partsPresent
    && checkpoint.panelTextLength > 0
    && checkpoint.display !== 'none'
    && checkpoint.visibility !== 'hidden'
    && checkpoint.opacity !== '0'
    && checkpoint.width > 0
    && checkpoint.height > 0
    && checkpoint.scrollTop === 0
    && !checkpoint.colorCorrectionApplied
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
  configurator?: ConfiguratorProbeResult,
): Promise<void> {
  const expectedIds = ['hood', 'door-left', 'door-right', 'trunk', 'wheel-spin']
  const catalogIds = animations.map((animation) => animation.animationId)
  const steps: Record<string, unknown>[] = []
  let ok = JSON.stringify(catalogIds) === JSON.stringify(expectedIds)
    && (configurator?.ok ?? true)
  let error = configurator && !configurator.ok
    ? configurator.error
    : (ok ? '' : `CATALOG_ANIMATIONS:${catalogIds.join(',')}`)
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
    ...(configurator ? { configurator } : {}),
  }))
}

export async function runConfiguratorCefProbe(
  bridge: ReflectedUeBridge,
  animations: CatalogAnimation[],
): Promise<void> {
  const result: ConfiguratorProbeResult = {
    ok: true,
    error: '',
    stages: [],
    personalization: [],
  }
  try {
    window.dispatchEvent(new CustomEvent('ue-configurator-category', {
      detail: 'performance',
    }))
    const lowerSkirtRegion = await waitForElement(
      '[aria-label="车辆下护板配置"]',
    )
    const lowerSkirtButton = buttonWithText('下护板')
    if (!lowerSkirtRegion || !lowerSkirtButton) {
      throw new Error('CONFIGURATOR_PERFORMANCE_CONTROLS_MISSING')
    }
    lowerSkirtButton.click()
    await delay(0)
    const aluminumButton = Array.from(
      document.querySelectorAll<HTMLButtonElement>('button'),
    ).find((button) => button.getAttribute('aria-label')?.startsWith('铝合金，'))
    if (!aluminumButton) {
      throw new Error('CONFIGURATOR_ALUMINUM_OPTION_MISSING')
    }
    aluminumButton.click()

    const panelScroll = document.querySelector<HTMLElement>('.panel-scroll')
    if (!panelScroll) throw new Error('CONFIGURATOR_PANEL_SCROLL_MISSING')
    panelScroll.scrollTop = 240

    const expectedStages = [
      ['interior', '表皮配置'],
      ['exterior', '车漆配置'],
      ['personalization', '内饰组件配置'],
      ['performance', '车辆下护板配置'],
    ] as const
    for (const [categoryId, regionName] of expectedStages) {
      window.dispatchEvent(new CustomEvent('ue-configurator-category', {
        detail: categoryId,
      }))
      const deadline = Date.now() + 5000
      let region: Element | null = null
      let parts: Element | null = null
      do {
        region = document.querySelector(`[aria-label="${regionName}"]`)
        parts = document.querySelector('[aria-label="部件筛选"]')
        if (region && parts && panelScroll.scrollTop === 0) break
        await delay(50)
      } while (Date.now() < deadline)
      const stage = {
        categoryId,
        regionName,
        regionPresent: region !== null,
        partsPresent: parts !== null,
        scrollTop: panelScroll.scrollTop,
      }
      result.stages.push(stage)
      if (!stage.regionPresent || !stage.partsPresent || stage.scrollTop !== 0) {
        throw new Error(`CONFIGURATOR_STAGE_FAILED:${categoryId}`)
      }
    }

    window.dispatchEvent(new CustomEvent('ue-configurator-category', {
      detail: 'personalization',
    }))
    const personalizationPages = [
      ['interior-parts', '内饰组件', '内饰组件配置'],
      ['door-pocket', '门板口袋', '门板口袋配置'],
      ['stitching', '缝线', '缝线配置'],
      ['headrest-embroidery', '头枕刺绣', '头枕刺绣配置'],
      ['door-panel-embroidery', '中板刺绣', '中板刺绣配置'],
    ] as const
    for (const [pageId, pageName, regionName] of personalizationPages) {
      const partButton = await waitForPartButton(pageName)
      if (!partButton) {
        throw new Error(`CONFIGURATOR_PERSONALIZATION_BUTTON_MISSING:${pageName}`)
      }
      partButton.click()
      const region = await waitForElement(`[aria-label="${regionName}"]`)
      if (!region) {
        throw new Error(`CONFIGURATOR_PERSONALIZATION_REGION_MISSING:${pageName}`)
      }
      await delay(0)
      const checkpoint = readConfiguratorCheckpoint(
        pageId,
        pageName,
        regionName,
      )
      result.personalization.push(checkpoint)
      if (!checkpointPassed(checkpoint)) {
        throw new Error(`CONFIGURATOR_PERSONALIZATION_FAILED:${pageName}`)
      }
    }

    const embroideryRegion = document.querySelector(
      '[aria-label="中板刺绣配置"]',
    )
    const embroideryOption = Array.from(
      embroideryRegion?.querySelectorAll<HTMLButtonElement>('button') ?? [],
    ).find((button) => button.getAttribute('aria-label')?.startsWith('中板刺绣，'))
    if (!embroideryOption) {
      throw new Error('CONFIGURATOR_DOOR_EMBROIDERY_OPTION_MISSING')
    }
    embroideryOption.click()
    await delay(0)
    const selectedCheckpoint = readConfiguratorCheckpoint(
      'door-panel-embroidery-selected',
      '中板刺绣-已选择',
      '中板刺绣配置',
    )
    result.personalization.push(selectedCheckpoint)
    if (!checkpointPassed(selectedCheckpoint)) {
      throw new Error('CONFIGURATOR_DOOR_EMBROIDERY_SELECTION_FAILED')
    }
  } catch (cause) {
    result.ok = false
    result.error = cause instanceof Error ? cause.message : String(cause)
  }
  await runCefBridgeProbe(bridge, animations, result)
}
