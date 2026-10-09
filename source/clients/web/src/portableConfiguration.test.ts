import { describe, expect, it } from 'vitest'
import { catalogFixture, initialSelections } from './test/catalogFixture'
import {
  createLegacyPortableConfiguration,
  createPortableConfiguration,
  createPortableConfigurationQr,
  LEGACY_PORTABLE_CONFIGURATION_PREFIX,
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
    expect(value).toMatch(/^SC01CFG2\./)
    expect(createPortableConfigurationQr(value)).toMatch(/^data:image\/gif;base64,/)
  })

  it('完整 SC01 目录的默认配置可压缩并生成高纠错二维码', () => {
    const selections = createInitialSelections(bundledCatalog)
    const customizations = normalizeCustomizations(bundledCatalog, selections, {})
    const value = createPortableConfiguration(bundledCatalog, selections, customizations)

    expect(value.length).toBeLessThan(700)
    expect(createPortableConfigurationQr(value)).toMatch(/^data:image\/gif;base64,/)
    expect(parsePortableConfiguration(value, bundledCatalog)).toEqual({
      selections,
      customizations,
    })
  })

  it('完整 SC01 非默认材质配置仍可生成同内容二维码', () => {
    const selections = createInitialSelections(bundledCatalog)
    for (const surfaceId of bundledCatalog.selectionOrder) {
      const choices = bundledCatalog.options.filter((option) =>
        option.surfaceId === surfaceId
        && option.availability?.status !== 'disabled'
        && Object.entries(option.requiresSelections ?? {}).every(
          ([requiredSurfaceId, requiredOptionId]) =>
            selections[requiredSurfaceId] === requiredOptionId,
        ))
      const choice = choices.filter((option) => !option.pricing.isStandard).at(-1)
        ?? choices.at(-1)
      if (choice) selections[surfaceId] = choice.optionId
    }
    const requestedCustomizations = Object.fromEntries(
      bundledCatalog.selectionOrder.flatMap((surfaceId) => {
        const option = bundledCatalog.options.find(
          (item) => item.optionId === selections[surfaceId],
        )
        if (
          !option?.materialFamilyId
          || (
            option.ui?.control !== 'material-variant'
            && option.parameters.color?.mode !== 'variant'
          )
        ) return []
        const variant = bundledCatalog.materialVariants
          .filter((item) => item.materialFamilyId === option.materialFamilyId)
          .at(-1)
        return variant
          ? [[surfaceId, { materialVariantId: variant.variantId }]]
          : []
      }),
    )
    const customizations = normalizeCustomizations(
      bundledCatalog,
      selections,
      requestedCustomizations,
    )
    const value = createPortableConfiguration(
      bundledCatalog,
      selections,
      customizations,
    )
    const legacyValue = createLegacyPortableConfiguration(
      bundledCatalog,
      selections,
      customizations,
    )

    expect(value.length).toBeLessThan(700)
    expect(value.length).toBeLessThan(legacyValue.length / 2)
    expect(createPortableConfigurationQr(value)).toMatch(/^data:image\/gif;base64,/)
    expect(parsePortableConfiguration(value, bundledCatalog)).toEqual({
      selections,
      customizations,
    })
  })

  it('继续读取旧版 SC01CFG1 选配码并归一化为当前配置', () => {
    const legacyValue = createLegacyPortableConfiguration(
      catalogFixture,
      initialSelections,
      {},
    )

    expect(legacyValue.startsWith(LEGACY_PORTABLE_CONFIGURATION_PREFIX)).toBe(true)
    expect(parsePortableConfiguration(legacyValue, catalogFixture)).toEqual({
      selections: initialSelections,
      customizations: {},
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

  it('拒绝 requiresSelections 不满足的可移植配置', () => {
    const incompatible = createPortableConfiguration(
      catalogFixture,
      {
        ...initialSelections,
        'wheel-material': 'wheel-aluminum-alloy',
        'wheel-style': 'wheel-style-magnesium-1',
      },
      {},
    )

    expect(() => parsePortableConfiguration(incompatible, catalogFixture))
      .toThrow('配置字符串与当前目录版本不兼容')
  })
})
