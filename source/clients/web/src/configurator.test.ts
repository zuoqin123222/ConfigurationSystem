import { describe, expect, it } from 'vitest'
import {
  componentsForCategory,
  createCanonicalKey,
  createInitialSelections,
  createRenderCanonicalKey,
  normalizeCustomizations,
  normalizeSelections,
  optionsForFilters,
  renderRelevantSelections,
  surfacesForComponent,
} from './configurator'
import { catalogFixture, initialSelections } from './test/catalogFixture'

describe('v2 动态选配逻辑', () => {
  it('按 selectionOrder 生成完整初始选择和稳定标识', () => {
    const selections = createInitialSelections(catalogFixture)

    expect(selections).toEqual(initialSelections)
    expect(Object.keys(selections)).toEqual(catalogFixture.selectionOrder)
    expect(createCanonicalKey(catalogFixture, selections).split('__')).toHaveLength(38)
  })

  it('分享配置存在无效选项时按对应表面的首项修复', () => {
    expect(normalizeSelections(catalogFixture, {
      'exterior-body-cover': 'unknown',
      'wheel-material': 'wheel-magnesium-alloy',
    })).toEqual({
      ...initialSelections,
      'wheel-material': 'wheel-magnesium-alloy',
    })
  })

  it('按 category、component、surface 与 materialFamily 联动筛选', () => {
    expect(componentsForCategory(catalogFixture, 'exterior').map((item) => item.componentId))
      .toEqual(['body', 'wheel'])
    expect(surfacesForComponent(catalogFixture, 'wheel').map((item) => item.surfaceId))
      .toEqual([
        'wheel-material',
        'wheel-style',
        'wheel-color',
        'lower-skirt',
        'front-caliper-color',
        'rear-caliper-color',
        'engine-bay-cover',
      ])
    expect(optionsForFilters(catalogFixture, 'wheel-material', 'magnesium-alloy')
      .map((item) => item.optionId))
      .toEqual(['wheel-magnesium-alloy'])
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
    expect(normalizeCustomizations(catalogFixture, initialSelections, {
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-sf4' },
    })).toEqual({
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-sf4' },
    })
  })
})
