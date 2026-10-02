import { describe, expect, it } from 'vitest'
import {
  componentsForCategory,
  createCanonicalKey,
  createInitialSelections,
  normalizeSelections,
  optionsForFilters,
  renderRelevantSelections,
  surfacesForComponent,
} from './configurator'
import { catalogFixture } from './test/catalogFixture'

describe('v2 动态选配逻辑', () => {
  it('按 selectionOrder 生成完整初始选择和稳定标识', () => {
    const selections = createInitialSelections(catalogFixture)

    expect(selections).toEqual({
      'exterior-body-cover': 'body-cover-red',
      'wheel-material': 'wheel-aluminum-alloy',
      'steering-wheel-skin': 'steering-skin-ultrasuede-black',
    })
    expect(createCanonicalKey(catalogFixture, selections)).toBe(
      'exterior-body-cover=body-cover-red__wheel-material=wheel-aluminum-alloy__steering-wheel-skin=steering-skin-ultrasuede-black',
    )
  })

  it('分享配置存在无效选项时按对应表面的首项修复', () => {
    expect(normalizeSelections(catalogFixture, {
      'exterior-body-cover': 'unknown',
      'wheel-material': 'wheel-magnesium-alloy',
    })).toEqual({
      'exterior-body-cover': 'body-cover-red',
      'wheel-material': 'wheel-magnesium-alloy',
      'steering-wheel-skin': 'steering-skin-ultrasuede-black',
    })
  })

  it('按 category、component、surface 与 materialFamily 联动筛选', () => {
    expect(componentsForCategory(catalogFixture, 'exterior').map((item) => item.componentId))
      .toEqual(['body', 'wheel'])
    expect(surfacesForComponent(catalogFixture, 'wheel').map((item) => item.surfaceId))
      .toEqual(['wheel-material'])
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
})
