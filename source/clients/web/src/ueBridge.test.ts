import { afterEach, describe, expect, it, vi } from 'vitest'
import {
  applyUeConfiguration,
  createUeConfigurationJson,
  getUeBridge,
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
})
