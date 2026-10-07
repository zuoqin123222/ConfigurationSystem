import type { CatalogCameraId, Customizations, Selections } from './types'

export interface ReflectedUeBridge {
  applyconfigurationjson?: (configurationJson: string) => void
  applyconfigurationtransactionjson?: (configurationJson: string) => Promise<string>
  setconfiguratorcategory?: (categoryId: string) => Promise<boolean>
  setconfiguratorheaderstatejson?: (stateJson: string) => Promise<boolean>
  triggerconfiguratorheaderaction?: (action: string) => Promise<boolean>
  getconfiguratorheaderstatejson?: () => Promise<string>
  getpresentationstatejson?: () => Promise<string>
  setcamera?: (cameraIndex: number) => Promise<boolean>
  setcameraid?: (cameraId: string) => Promise<boolean>
  setanimationenabled?: (enabled: boolean) => Promise<boolean>
  playanimation?: (animationId: string) => Promise<boolean>
  closeanimation?: (animationId: string) => Promise<boolean>
  focusanimation?: (nextAnimationId: string) => Promise<boolean>
  canplayanimation?: (animationId: string) => Promise<boolean>
  getanimationexecutorstatejson?: (animationId: string) => Promise<string>
  completecefbridgeprobe?: (resultJson: string) => Promise<boolean>
  reportuiready?: (viewId: string) => Promise<boolean>
  setlightpreset?: (preset: string) => Promise<boolean>
  setrendermode?: (mode: string) => Promise<boolean>
  getrendermodeerror?: () => Promise<string>
  setqualitylevel?: (quality: string) => Promise<boolean>
  resetpresentation?: () => Promise<boolean>
  setfullscreen?: (enabled: boolean) => Promise<boolean>
}

export interface UeConfigurationReceipt {
  ok: boolean
  code: string
  message: string
  configurationId: string
  appliedSurfaceIds: string[]
  unsupportedSurfaceIds: string[]
  appliedSlotIds: string[]
}

export type UeCameraIndex = 0 | 1 | 2 | 3 | 4 | 5
export type UeConfiguratorCategory = string

export const CONFIGURATOR_CATEGORY_EVENT = 'ue-configurator-category'
export const CONFIGURATOR_HEADER_STATE_EVENT = 'ue-configurator-header-state'
export const CONFIGURATOR_HEADER_ACTION_EVENT = 'ue-configurator-header-action'
export type UeControlCommand =
  | {
    type: 'camera'
    cameraId: CatalogCameraId
    legacyIndex?: UeCameraIndex | null
  }
  | { type: 'camera'; cameraIndex: UeCameraIndex }
  | { type: 'animation'; enabled: boolean }
  | { type: 'play-animation'; animationId: string }
  | { type: 'close-animation'; animationId: string }
  | { type: 'light'; preset: 'studio' | 'outdoor' }
  | { type: 'render'; mode: 'realtime' | 'path-tracing' }
  | { type: 'quality'; quality: UeQualityLevel }
  | { type: 'reset' }
  | { type: 'fullscreen'; enabled: boolean }

export type UeQualityLevel = 'low' | 'medium' | 'high' | 'epic'
export type UeRenderAvailability = 'preparing' | 'ready' | 'unavailable'
export type UeControlFailureKind =
  | 'bridge-unavailable'
  | 'method-unavailable'
  | 'command-rejected'
export type UeConfiguratorHeaderAction = 'save' | 'share' | 'reset'
export type UeConfiguratorSyncState = 'idle' | 'saving' | 'saved' | 'error'

export interface UeConfiguratorHeaderState {
  categoryId: UeConfiguratorCategory
  referenceTotalMinor: number
  syncState: UeConfiguratorSyncState
  syncMessage: string
  dirty: boolean
  online: boolean
}

export interface UePresentationState {
  cameraId?: CatalogCameraId
  cameraIndex?: UeCameraIndex
  animationEnabled: boolean
  animationId?: string | null
  lightPreset: 'studio' | 'outdoor'
  renderMode: 'realtime' | 'path-tracing'
  renderProgress?: number
  renderAvailability?: UeRenderAvailability
  quality: UeQualityLevel
  fullscreen: boolean
}

declare global {
  interface Window {
    ue?: {
      uebridge?: ReflectedUeBridge
    }
  }
}

export function getUeBridge(embedded: boolean): ReflectedUeBridge | null {
  if (!embedded) return null
  return window.ue?.uebridge ?? null
}

export function getUeControlFailureKind(
  bridge: ReflectedUeBridge | null,
  command: UeControlCommand,
): UeControlFailureKind {
  if (!bridge) return 'bridge-unavailable'
  const hasMethod = (() => {
    switch (command.type) {
      case 'camera':
        return 'cameraId' in command
          ? typeof bridge.setcameraid === 'function'
            || (isUeCameraIndex(command.legacyIndex)
              && typeof bridge.setcamera === 'function')
          : typeof bridge.setcamera === 'function'
      case 'animation':
        return typeof bridge.setanimationenabled === 'function'
      case 'play-animation':
        return typeof bridge.playanimation === 'function'
      case 'close-animation':
        return typeof bridge.closeanimation === 'function'
      case 'light':
        return typeof bridge.setlightpreset === 'function'
      case 'render':
        return typeof bridge.setrendermode === 'function'
      case 'quality':
        return typeof bridge.setqualitylevel === 'function'
      case 'reset':
        return typeof bridge.resetpresentation === 'function'
      case 'fullscreen':
        return typeof bridge.setfullscreen === 'function'
      default:
        return false
    }
  })()
  return hasMethod ? 'command-rejected' : 'method-unavailable'
}

export type UeUiViewId = 'embedded' | 'controls' | 'header'

export async function reportUeUiReady(viewId: UeUiViewId): Promise<boolean> {
  const bridge = getUeBridge(true)
  if (typeof bridge?.reportuiready !== 'function') return false
  try {
    return await bridge.reportuiready(viewId)
  } catch {
    return false
  }
}

export function getUeControlFailureMessage(kind: UeControlFailureKind): string {
  switch (kind) {
    case 'bridge-unavailable':
      return 'UE 控制桥不可用'
    case 'method-unavailable':
      return 'UE 控制桥缺少所需方法'
    case 'command-rejected':
      return 'UE 命令被拒绝或执行失败'
  }
}

export function hasUeAnimationBridgeMethod(
  bridge: ReflectedUeBridge | null,
): boolean {
  return !!bridge && (
    typeof bridge.focusanimation === 'function'
    || (typeof bridge.playanimation === 'function'
      && typeof bridge.closeanimation === 'function')
  )
}

export function createUeConfigurationJson(
  selections: Selections,
  customizations: Customizations,
): string {
  return JSON.stringify({
    schemaVersion: '2.0.0',
    selections,
    customizations,
  })
}

export async function applyUeConfiguration(
  bridge: ReflectedUeBridge,
  selections: Selections,
  customizations: Customizations,
): Promise<UeConfigurationReceipt> {
  const configurationJson = createUeConfigurationJson(selections, customizations)
  if (typeof bridge.applyconfigurationtransactionjson === 'function') {
    try {
      const value: unknown = JSON.parse(
        await bridge.applyconfigurationtransactionjson(configurationJson),
      )
      if (isUeConfigurationReceipt(value)) return value
      return failedConfigurationReceipt(
        'INVALID_UE_RECEIPT',
        'UE 返回了非法材质事务回执。',
      )
    } catch {
      return failedConfigurationReceipt(
        'UE_TRANSACTION_REJECTED',
        'UE 材质事务调用失败。',
      )
    }
  }
  return failedConfigurationReceipt('BRIDGE_METHOD_UNAVAILABLE', 'UE 材质事务入口不可用。')
}

function failedConfigurationReceipt(
  code: string,
  message: string,
): UeConfigurationReceipt {
  return {
    ok: false,
    code,
    message,
    configurationId: '',
    appliedSurfaceIds: [],
    unsupportedSurfaceIds: [],
    appliedSlotIds: [],
  }
}

export function isUeConfigurationReceipt(value: unknown): value is UeConfigurationReceipt {
  if (!value || typeof value !== 'object') return false
  const receipt = value as Record<string, unknown>
  const fields = [
    'ok',
    'code',
    'message',
    'configurationId',
    'appliedSurfaceIds',
    'unsupportedSurfaceIds',
    'appliedSlotIds',
  ]
  return Object.keys(receipt).length === fields.length
    && Object.keys(receipt).every((field) => fields.includes(field))
    && typeof receipt.ok === 'boolean'
    && typeof receipt.code === 'string'
    && /^[A-Z0-9_]+$/.test(receipt.code)
    && typeof receipt.message === 'string'
    && receipt.message.length <= 512
    && typeof receipt.configurationId === 'string'
    && receipt.configurationId.length <= 128
    && isCatalogNodeIdArray(receipt.appliedSurfaceIds)
    && isCatalogNodeIdArray(receipt.unsupportedSurfaceIds)
    && isMaterialSlotIdArray(receipt.appliedSlotIds)
}

function isCatalogNodeIdArray(value: unknown): value is string[] {
  return Array.isArray(value)
    && value.length <= 64
    && value.every(isCatalogNodeId)
}

function isMaterialSlotIdArray(value: unknown): value is string[] {
  return Array.isArray(value)
    && value.length <= 128
    && value.every((item) => typeof item === 'string'
      && item.length > 0
      && item.length <= 128
      && /^[A-Za-z0-9_]+$/.test(item))
}

export async function syncUeConfiguratorCategory(
  bridge: ReflectedUeBridge | null,
  categoryId: string,
): Promise<boolean> {
  if (!isCatalogNodeId(categoryId)
    || typeof bridge?.setconfiguratorcategory !== 'function') return false
  try {
    return await bridge.setconfiguratorcategory(categoryId)
  } catch {
    return false
  }
}

export async function setUeCameraId(
  bridge: ReflectedUeBridge | null,
  cameraId: CatalogCameraId | null | undefined,
  legacyIndex?: UeCameraIndex | null,
): Promise<boolean> {
  try {
    if (typeof cameraId === 'string') {
      if (!/^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(cameraId)) return false
      if (typeof bridge?.setcameraid === 'function') {
        return await bridge.setcameraid(cameraId)
      }
      if (isUeCameraIndex(legacyIndex) && typeof bridge?.setcamera === 'function') {
        return await bridge.setcamera(legacyIndex)
      }
      return false
    }
    if (isUeCameraIndex(cameraId)) {
      if (typeof bridge?.setcamera !== 'function') return false
      return await bridge.setcamera(cameraId)
    }
    return false
  } catch {
    return false
  }
}

export async function playUeAnimation(
  bridge: ReflectedUeBridge | null,
  animationId: string,
): Promise<boolean> {
  if (!isCatalogNodeId(animationId)
    || typeof bridge?.playanimation !== 'function') return false
  try {
    return await bridge.playanimation(animationId)
  } catch {
    return false
  }
}

export async function closeUeAnimation(
  bridge: ReflectedUeBridge | null,
  animationId: string,
): Promise<boolean> {
  if (!isCatalogNodeId(animationId)
    || typeof bridge?.closeanimation !== 'function') return false
  try {
    return await bridge.closeanimation(animationId)
  } catch {
    return false
  }
}

export async function focusUeAnimation(
  bridge: ReflectedUeBridge | null,
  currentAnimationId: string | null,
  nextAnimationId: string | null,
): Promise<boolean> {
  if (nextAnimationId !== null && !isCatalogNodeId(nextAnimationId)) return false
  if (typeof bridge?.focusanimation === 'function') {
    try {
      return await bridge.focusanimation(nextAnimationId ?? '')
    } catch {
      return false
    }
  }
  if (currentAnimationId === nextAnimationId) return true
  if (currentAnimationId
    && !await closeUeAnimation(bridge, currentAnimationId)) return false
  if (!nextAnimationId) return true
  if (await playUeAnimation(bridge, nextAnimationId)) return true
  if (currentAnimationId) await playUeAnimation(bridge, currentAnimationId)
  return false
}

export async function canPlayUeAnimation(
  bridge: ReflectedUeBridge | null,
  animationId: string,
): Promise<boolean> {
  if (!isCatalogNodeId(animationId)) return false
  if (!hasUeAnimationBridgeMethod(bridge)) return false
  if (typeof bridge?.canplayanimation !== 'function') return true
  try {
    return await bridge.canplayanimation(animationId)
  } catch {
    return false
  }
}

function isUeCameraIndex(value: unknown): value is UeCameraIndex {
  return Number.isInteger(value) && Number(value) >= 0 && Number(value) <= 5
}

function isCatalogNodeId(value: unknown): value is string {
  return typeof value === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(value)
}

export function createUeConfiguratorHeaderStateJson(
  state: UeConfiguratorHeaderState,
): string {
  return JSON.stringify(state)
}

export function isUeConfiguratorHeaderState(
  value: unknown,
): value is UeConfiguratorHeaderState {
  if (!value || typeof value !== 'object') return false
  const state = value as Record<string, unknown>
  const allowedFields = [
    'categoryId',
    'referenceTotalMinor',
    'syncState',
    'syncMessage',
    'dirty',
    'online',
  ]
  return Object.keys(state).length === allowedFields.length
    && Object.keys(state).every((field) => allowedFields.includes(field))
    && isCatalogNodeId(state.categoryId)
    && Number.isSafeInteger(state.referenceTotalMinor)
    && Number(state.referenceTotalMinor) >= 0
    && ['idle', 'saving', 'saved', 'error'].includes(String(state.syncState))
    && typeof state.syncMessage === 'string'
    && state.syncMessage.length <= 256
    && typeof state.dirty === 'boolean'
    && typeof state.online === 'boolean'
}

export async function syncUeConfiguratorHeaderState(
  bridge: ReflectedUeBridge | null,
  state: UeConfiguratorHeaderState,
): Promise<boolean> {
  if (!isUeConfiguratorHeaderState(state)
    || typeof bridge?.setconfiguratorheaderstatejson !== 'function') return false
  try {
    return await bridge.setconfiguratorheaderstatejson(
      createUeConfiguratorHeaderStateJson(state),
    )
  } catch {
    return false
  }
}

export async function getUeConfiguratorHeaderState(
  bridge: ReflectedUeBridge | null,
): Promise<UeConfiguratorHeaderState | null> {
  if (typeof bridge?.getconfiguratorheaderstatejson !== 'function') return null
  try {
    const state: unknown = JSON.parse(await bridge.getconfiguratorheaderstatejson())
    return isUeConfiguratorHeaderState(state) ? state : null
  } catch {
    return null
  }
}

export async function triggerUeConfiguratorHeaderAction(
  bridge: ReflectedUeBridge | null,
  action: UeConfiguratorHeaderAction,
): Promise<boolean> {
  if (!['save', 'share', 'reset'].includes(action)
    || typeof bridge?.triggerconfiguratorheaderaction !== 'function') return false
  try {
    return await bridge.triggerconfiguratorheaderaction(action)
  } catch {
    return false
  }
}

export async function getUePresentationState(
  bridge: ReflectedUeBridge | null,
): Promise<UePresentationState | null> {
  if (!bridge || typeof bridge.getpresentationstatejson !== 'function') return null
  try {
    const value: unknown = JSON.parse(await bridge.getpresentationstatejson())
    if (!value || typeof value !== 'object') return null
    const state = value as Record<string, unknown>
    const validCameraId = isCatalogNodeId(state.cameraId)
      || (Number.isInteger(state.cameraId)
        && Number(state.cameraId) >= 0
        && Number(state.cameraId) <= 5)
    const validCameraIndex = Number.isInteger(state.cameraIndex)
      && Number(state.cameraIndex) >= 0
      && Number(state.cameraIndex) <= 5
    if ((!validCameraId && !validCameraIndex)
      || typeof state.animationEnabled !== 'boolean'
      || (state.animationId !== undefined
        && state.animationId !== null
        && !isCatalogNodeId(state.animationId))
      || !['studio', 'outdoor'].includes(String(state.lightPreset))
      || !['realtime', 'path-tracing'].includes(String(state.renderMode))
      || (state.renderProgress !== undefined
        && (!Number.isFinite(state.renderProgress)
          || Number(state.renderProgress) < 0
          || Number(state.renderProgress) > 1))
      || (state.renderAvailability !== undefined
        && !['preparing', 'ready', 'unavailable'].includes(
          String(state.renderAvailability),
        ))
      || !['low', 'medium', 'high', 'epic'].includes(String(state.quality))
      || typeof state.fullscreen !== 'boolean') return null
    return state as unknown as UePresentationState
  } catch {
    return null
  }
}

export async function getUeRenderModeError(
  bridge: ReflectedUeBridge | null,
): Promise<string> {
  if (typeof bridge?.getrendermodeerror !== 'function') return ''
  try {
    const error = await bridge.getrendermodeerror()
    return typeof error === 'string' && error.length <= 512 ? error.trim() : ''
  } catch {
    return ''
  }
}

/**
 * 唯一允许 controls 视图调用的 UE 命令分发器。
 * 不接受动态方法名、控制台命令、URL 或任意 JSON 转发。
 */
export async function executeUeControl(
  bridge: ReflectedUeBridge | null,
  command: UeControlCommand,
): Promise<boolean> {
  if (!bridge) return false
  try {
    switch (command.type) {
      case 'camera':
        return 'cameraId' in command
          ? await setUeCameraId(bridge, command.cameraId, command.legacyIndex)
          : await setUeCameraId(bridge, command.cameraIndex)
      case 'animation':
        if (typeof command.enabled !== 'boolean'
          || typeof bridge.setanimationenabled !== 'function') return false
        return await bridge.setanimationenabled(command.enabled)
      case 'play-animation':
        return playUeAnimation(bridge, command.animationId)
      case 'close-animation':
        return closeUeAnimation(bridge, command.animationId)
      case 'light':
        if (!['studio', 'outdoor'].includes(command.preset)
          || typeof bridge.setlightpreset !== 'function') return false
        return await bridge.setlightpreset(command.preset)
      case 'render':
        if (!['realtime', 'path-tracing'].includes(command.mode)
          || typeof bridge.setrendermode !== 'function') return false
        return await bridge.setrendermode(command.mode)
      case 'quality':
        if (!['low', 'medium', 'high', 'epic'].includes(command.quality)
          || typeof bridge.setqualitylevel !== 'function') return false
        return await bridge.setqualitylevel(command.quality)
      case 'reset':
        if (typeof bridge.resetpresentation !== 'function') return false
        return await bridge.resetpresentation()
      case 'fullscreen':
        if (typeof command.enabled !== 'boolean'
          || typeof bridge.setfullscreen !== 'function') return false
        return await bridge.setfullscreen(command.enabled)
      default:
        return false
    }
  } catch {
    return false
  }
}
