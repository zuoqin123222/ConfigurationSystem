import { describe, expect, it } from 'vitest'
import { catalogFixture, initialSelections } from './test/catalogFixture'
import {
  createPortableConfiguration,
  createPortableConfigurationQr,
  parsePortableConfiguration,
  PORTABLE_CONFIGURATION_PREFIX,
} from './portableConfiguration'
import {
  bundledCatalog,
  resolveStaticAssetUrl,
  usesBundledCatalog,
} from './bundledCatalog'
import { createInitialSelections, normalizeCustomizations } from './configurator'

describe('portable configuration', () => {
  it('仅 file 协议使用随包目录', () => {
    expect(usesBundledCatalog('file:')).toBe(true)
    expect(usesBundledCatalog('https:')).toBe(false)
    expect(bundledCatalog.catalogVersion).toBe(catalogFixture.catalogVersion)
    expect(resolveStaticAssetUrl(
      '/sc01/option-icons/default.svg',
      '?assetRevision=42',
      'file:',
      'file:///C:/SC01/WebUI/index.html',
    )).toBe('file:///C:/SC01/WebUI/sc01/option-icons/default.svg?v=42')
  })

  it('保存与分享可复用确定性的自包含字符串', () => {
    const first = createPortableConfiguration(catalogFixture, initialSelections, {})
    const second = createPortableConfiguration(
      catalogFixture,
      Object.fromEntries(Object.entries(initialSelections).reverse()),
      {},
    )

    expect(first).toBe(second)
    expect(first.startsWith(PORTABLE_CONFIGURATION_PREFIX)).toBe(true)
    expect(parsePortableConfiguration(first, catalogFixture)).toEqual({
      selections: initialSelections,
      customizations: {},
    })
  })

  it('二维码编码完整配置字符串', () => {
    const value = createPortableConfiguration(catalogFixture, initialSelections, {})
    expect(createPortableConfigurationQr(value)).toMatch(/^data:image\/gif;base64,/)
  })

  it('完整 SC01 目录的默认配置可压缩并生成中等纠错二维码', () => {
    const selections = createInitialSelections(bundledCatalog)
    const customizations = normalizeCustomizations(bundledCatalog, selections, {})
    const value = createPortableConfiguration(bundledCatalog, selections, customizations)

    expect(value.length).toBeLessThan(2953)
    expect(createPortableConfigurationQr(value)).toMatch(/^data:image\/gif;base64,/)
    expect(parsePortableConfiguration(value, bundledCatalog)).toEqual({
      selections,
      customizations,
    })
  })

  it('拒绝损坏和跨目录版本字符串', () => {
    expect(() => parsePortableConfiguration('invalid', catalogFixture))
      .toThrow('不是受支持的 SC01 配置字符串')
    const value = createPortableConfiguration(catalogFixture, initialSelections, {})
    expect(() => parsePortableConfiguration(value, {
      ...catalogFixture,
      catalogVersion: 'other',
    })).toThrow('配置字符串与当前目录版本不兼容')
    const corrupted = `${value.slice(0, -1)}${value.endsWith('0') ? '1' : '0'}`
    expect(() => parsePortableConfiguration(corrupted, catalogFixture))
      .toThrow('配置字符串已损坏')
  })
})
