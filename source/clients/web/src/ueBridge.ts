import type { CatalogCameraId, Customizations, Selections } from './types'

export interface ReflectedUeBridge {
  applyconfigurationjson?: (configurationJson: string) => void
  setconfiguratorcategory?: (categoryId: string) => Promise<boolean>
  setconfiguratorheaderstatejson?: (stateJson: string) => Promise<boolean>
  triggerconfiguratorheaderaction?: (action: string) => Promise<boolean>
  getconfiguratorheaderstatejson?: () => Promise<string>
  getpresentationstatejson?: () => Promise<string>
  setcamera?: (cameraIndex: number) => Promise<boolean>
  setcameraid?: (cameraId: string) => Promise<boolean>
  setanimationenabled?: (enabled: boolean) => Promise<boolean>
  setlightpreset?: (preset: string) => Promise<boolean>
  setrendermode?: (mode: string) => Promise<boolean>
  setqualitylevel?: (quality: string) => Promise<boolean>
  resetpresentation?: () => Promise<boolean>
  setfullscreen?: (enabled: boolean) => Promise<boolean>
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
  | { type: 'light'; preset: 'studio' | 'outdoor' }
  | { type: 'render'; mode: 'realtime' | 'path-tracing' }
  | { type: 'quality'; quality: UeQualityLevel }
  | { type: 'reset' }
  | { type: 'fullscreen'; enabled: boolean }

export type UeQualityLevel = 'low' | 'medium' | 'high' | 'epic'
export type UeConfiguratorHeaderAction = 'save' | 'share'
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
  lightPreset: 'studio' | 'outdoor'
  renderMode: 'realtime' | 'path-tracing'
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

export function applyUeConfiguration(
  bridge: ReflectedUeBridge,
  selections: Selections,
  customizations: Customizations,
): boolean {
  if (typeof bridge.applyconfigurationjson !== 'function') return false
  bridge.applyconfigurationjson(createUeConfigurationJson(selections, customizations))
  return true
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
  if (!['save', 'share'].includes(action)
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
      || !['studio', 'outdoor'].includes(String(state.lightPreset))
      || !['realtime', 'path-tracing'].includes(String(state.renderMode))
      || !['low', 'medium', 'high', 'epic'].includes(String(state.quality))
      || typeof state.fullscreen !== 'boolean') return null
    return state as unknown as UePresentationState
  } catch {
    return null
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
