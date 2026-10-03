import type { Customizations, Selections } from './types'

export interface ReflectedUeBridge {
  applyconfigurationjson?: (configurationJson: string) => void
  setconfiguratorcategory?: (categoryId: string) => Promise<boolean>
  getpresentationstatejson?: () => Promise<string>
  setcamera?: (cameraIndex: number) => Promise<boolean>
  setanimationenabled?: (enabled: boolean) => Promise<boolean>
  setlightpreset?: (preset: string) => Promise<boolean>
  setrendermode?: (mode: string) => Promise<boolean>
  setqualitylevel?: (quality: string) => Promise<boolean>
  resetpresentation?: () => Promise<boolean>
  setfullscreen?: (enabled: boolean) => Promise<boolean>
}

export type UeCameraIndex = 0 | 1 | 2 | 3 | 4 | 5
export type UeConfiguratorCategory = 'exterior' | 'interior' | 'performance' | 'personalization'

export const CONFIGURATOR_CATEGORY_EVENT = 'ue-configurator-category'
export const CONFIGURATOR_CATEGORIES: Array<{
  id: UeConfiguratorCategory
  label: string
}> = [
  { id: 'exterior', label: '外饰' },
  { id: 'interior', label: '内饰' },
  { id: 'performance', label: '性能配置' },
  { id: 'personalization', label: '其他个性化' },
]

export type UeControlCommand =
  | { type: 'camera'; cameraIndex: UeCameraIndex }
  | { type: 'animation'; enabled: boolean }
  | { type: 'light'; preset: 'studio' | 'outdoor' }
  | { type: 'render'; mode: 'realtime' | 'path-tracing' }
  | { type: 'quality'; quality: UeQualityLevel }
  | { type: 'reset' }
  | { type: 'fullscreen'; enabled: boolean }

export type UeQualityLevel = 'low' | 'medium' | 'high' | 'epic'

export interface UePresentationState {
  cameraIndex: UeCameraIndex
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
  if (!CONFIGURATOR_CATEGORIES.some((category) => category.id === categoryId)
    || typeof bridge?.setconfiguratorcategory !== 'function') return false
  try {
    return await bridge.setconfiguratorcategory(categoryId)
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
    if (!Number.isInteger(state.cameraIndex)
      || Number(state.cameraIndex) < 0
      || Number(state.cameraIndex) > 5
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
        if (!Number.isInteger(command.cameraIndex)
          || command.cameraIndex < 0
          || command.cameraIndex > 5
          || typeof bridge.setcamera !== 'function') return false
        return await bridge.setcamera(command.cameraIndex)
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
