import { describe, expect, it } from 'vitest'
import {
  componentsForCategory,
  createCanonicalKey,
  createInitialSelections,
  createRenderCanonicalKey,
  materialGroupsForSurface,
  normalizeCustomizations,
  normalizeSelections,
  renderRelevantSelections,
  surfacesForComponent,
} from './configurator'
import { catalogFixture, initialSelections } from './test/catalogFixture'

describe('v2 动态选配逻辑', () => {
  it('按 selectionOrder 生成完整初始选择和稳定标识', () => {
    const selections = createInitialSelections(catalogFixture)

    expect(selections).toEqual(initialSelections)
    expect(Object.keys(selections)).toEqual(
      catalogFixture.selectionOrder.filter((surfaceId) =>
        catalogFixture.options.some(
          (option) => option.surfaceId === surfaceId && option.pricing.isStandard,
        ),
      ),
    )
    expect(selections).not.toHaveProperty('lower-skirt')
    expect(createCanonicalKey(catalogFixture, selections)).not.toContain('lower-skirt=')
  })

  it('初始选择严格使用 defaultSelections，不受多标配 option 数组顺序影响', () => {
    const reorderedCatalog = {
      ...catalogFixture,
      options: [...catalogFixture.options].reverse(),
    }

    expect(createInitialSelections(reorderedCatalog)).toEqual(catalogFixture.defaultSelections)
    expect(createInitialSelections(reorderedCatalog)['exterior-body-cover']).toBe('body-cover-red')
  })

  it('分享配置存在无效选项时仅回退到显式标配，无标配表面保持不选装', () => {
    expect(normalizeSelections(catalogFixture, {
      'exterior-body-cover': 'unknown',
      'wheel-material': 'wheel-magnesium-alloy',
      'lower-skirt': 'unknown',
    })).toEqual({
      ...initialSelections,
      'wheel-material': 'wheel-magnesium-alloy',
    })
  })

  it('按四阶段 category、component、surface 联动并输出同级材质组', () => {
    expect(componentsForCategory(catalogFixture, 'exterior').map((item) => item.componentId))
      .toEqual(['car-paint', 'chassis', 'wheel', 'caliper'])
    expect(componentsForCategory(catalogFixture, 'interior').map((item) => item.displayName))
      .toEqual(['方向盘', 'IP', 'A柱', '座椅', '门板', '储物盒盖', '副仪表台', '车顶'])
    expect(surfacesForComponent(catalogFixture, 'wheel').map((item) => item.surfaceId))
      .toEqual([
        'wheel-material',
        'wheel-style',
        'wheel-color',
      ])
    expect(materialGroupsForSurface(catalogFixture, 'wheel-material').map((group) => ({
      materialFamilyId: group.materialFamily.materialFamilyId,
      optionIds: group.options.map((item) => item.optionId),
    }))).toEqual([
      { materialFamilyId: 'aluminum-alloy', optionIds: ['wheel-aluminum-alloy'] },
      { materialFamilyId: 'magnesium-alloy', optionIds: ['wheel-magnesium-alloy'] },
    ])
  })

  it('只把 renderRelevant 选项纳入渲染选择', () => {
    const catalog = {
      ...catalogFixture,
      options: catalogFixture.options.map((option) =>
        option.optionId === 'wheel-aluminum-alloy'
          ? { ...option, renderRelevant: false }
          : option,
      ),
    }
    const relevant = renderRelevantSelections(catalog, createInitialSelections(catalog))

    expect(relevant).not.toHaveProperty('wheel-material')
    expect(relevant).toHaveProperty('exterior-body-cover', 'body-cover-red')
  })

  it('按 surface 顺序稳定编码材料色卡与车漆定制', () => {
    const customSelections = {
      ...initialSelections,
      'exterior-body-cover': 'body-cover-custom',
    }
    const customizations = {
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-sf4' },
      'exterior-body-cover': {
        colorHex: '#123456',
        metallic: 0.2,
        roughness: 0.3,
        clearCoat: 0.8,
        orangePeel: 0.1,
        flakeIntensity: 0.4,
      },
    }
    const reversed = Object.fromEntries(Object.entries(customizations).reverse())

    expect(createCanonicalKey(catalogFixture, customSelections, customizations))
      .toBe(createCanonicalKey(catalogFixture, customSelections, reversed))
    expect(createRenderCanonicalKey(catalogFixture, customSelections, customizations))
      .toContain('exterior-body-cover.colorHex=#123456')
  })

  it('恢复时丢弃与所选 option 材料族不匹配的 variant', () => {
    expect(normalizeCustomizations(catalogFixture, initialSelections, {
      'steering-wheel-skin': { materialVariantId: 'alcantara-p2-2911' },
    })).toEqual({})
    expect(normalizeCustomizations(catalogFixture, {
      ...initialSelections,
      'steering-wheel-skin': 'steering-skin-ultrasuede-custom',
    }, {
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-sf4' },
    })).toEqual({
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-sf4' },
    })
  })
})
