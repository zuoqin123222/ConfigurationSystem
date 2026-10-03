import type { Customizations, Selections } from './types'

interface ReflectedUeBridge {
  applyconfigurationjson?: (configurationJson: string) => void
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
