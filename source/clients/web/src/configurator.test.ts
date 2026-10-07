import { describe, expect, it } from 'vitest'
import {
  animationIdForSelection,
  cameraIdForSelection,
  categoriesInUiOrder,
  componentsForCategory,
  createCanonicalKey,
  createDefaultPaintCustomization,
  createInitialSelections,
  createRenderCanonicalKey,
  materialGroupsForSurface,
  normalizeCustomizations,
  normalizeSelections,
  optionIsAvailable,
  optionsForSurface,
  renderRelevantSelections,
  sortMaterialVariants,
  surfacesForComponent,
  workflowPagesForCategory,
} from './configurator'
import { catalogFixture, initialSelections } from './test/catalogFixture'

describe('v2 动态选配逻辑', () => {
  it('保留 Catalog 顶层骨骼网格和完整动画序列路径', () => {
    expect(catalogFixture.skeletalMeshPath)
      .toBe('/Game/Configurator/AuthorizedAudiA5/SK_A5_Car.SK_A5_Car')
    expect(catalogFixture.sequencePath)
      .toBe('/Game/Configurator/AuthorizedAudiA5/Animations/A_A5_FullVehicle.A_A5_FullVehicle')
    expect(catalogFixture.animations.every(
      (animation) => !Object.hasOwn(animation, 'sequencePath')
        && !Object.hasOwn(animation, 'skeletalMeshPath'),
    )).toBe(true)
  })

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
    expect(createInitialSelections(reorderedCatalog).nameplate).toBe('nameplate-none')
  })

  it('分享配置存在无效选项时仅回退到显式标配，无标配表面保持不选装', () => {
    expect(normalizeSelections(catalogFixture, {
      'exterior-body-cover': 'unknown',
      'wheel-material': 'wheel-magnesium-alloy',
      'lower-skirt': 'unknown',
    })).toEqual({
      ...initialSelections,
      'wheel-material': 'wheel-magnesium-alloy',
      'wheel-style': 'wheel-style-magnesium-1',
    })
  })

  it('旧配置缺少铭牌选择时归一化为稳定的免费无选项', () => {
    const legacySelections = { ...initialSelections }
    delete legacySelections.nameplate

    expect(normalizeSelections(catalogFixture, legacySelections).nameplate)
      .toBe('nameplate-none')
  })

  it('按四阶段 category、component、surface 联动并输出同级材质组', () => {
    expect(componentsForCategory(catalogFixture, 'exterior').map((item) => item.componentId))
      .toEqual(['car-paint', 'chassis', 'wheel', 'caliper'])
    expect(componentsForCategory(catalogFixture, 'interior').map((item) => item.displayName))
      .toEqual(['方向盘', '座椅', '门板', '仪表台', '储物盒盖', '副仪表台', '车顶', 'A柱'])
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

  it('由 ui.order 排序且旧目录缺少 ui 时保持原数组顺序', () => {
    const catalog = structuredClone(catalogFixture)
    catalog.components = catalog.components.map((component) => ({
      ...component,
      ui: component.componentId === 'caliper'
        ? { order: 0 }
        : component.componentId === 'car-paint'
          ? { order: 10 }
          : component.ui,
    }))
    expect(componentsForCategory(catalog, 'exterior').map((item) => item.componentId))
      .toEqual(['caliper', 'chassis', 'wheel', 'car-paint'])

    const legacy = structuredClone(catalogFixture)
    legacy.components.forEach((component) => delete component.ui)
    expect(componentsForCategory(legacy, 'exterior').map((item) => item.componentId))
      .toEqual(['car-paint', 'chassis', 'wheel', 'caliper'])

    catalog.categories[0].ui = { order: 10 }
    expect(categoriesInUiOrder(catalog).map((item) => item.categoryId))
      .toEqual(['interior', 'performance', 'personalization', 'exterior'])
  })

  it('按 surface、component、category 优先级解析动画联动并校验顶层定义', () => {
    expect(animationIdForSelection(catalogFixture, {
      categoryId: 'exterior',
      componentId: 'chassis',
    })).toBe('hood')
    expect(animationIdForSelection(catalogFixture, {
      categoryId: 'exterior',
      componentId: 'wheel',
    })).toBeNull()

    const invalid = structuredClone(catalogFixture)
    invalid.components.find((item) => item.componentId === 'chassis')!.ui!.animationId = 'missing'
    expect(animationIdForSelection(invalid, { componentId: 'chassis' })).toBeNull()
  })

  it('由 category surfaces-as-components 配置把 surface 平铺为部件入口', () => {
    expect(componentsForCategory(catalogFixture, 'personalization').map((item) => item.displayName))
      .toEqual([
        '内饰组件',
        '门板口袋',
        '缝线',
        '头枕刺绣',
        '中板刺绣',
        '中板缝线',
        '铭牌',
        '脚垫',
      ])
  })

  it('个性化 surfaces-as-components 生成八个连续工作流页面', () => {
    expect(workflowPagesForCategory(catalogFixture, 'personalization').map((page) => ({
      componentId: page.componentId,
      surfaceIds: page.surfaces.map((surface) => surface.surfaceId),
    }))).toEqual([
      { componentId: 'interior-painted-parts', surfaceIds: ['interior-painted-parts'] },
      { componentId: 'door-sill', surfaceIds: ['door-sill'] },
      { componentId: 'embroidered-logo', surfaceIds: ['embroidered-logo'] },
      { componentId: 'headrest-embroidery', surfaceIds: ['headrest-embroidery'] },
      { componentId: 'door-panel-embroidery', surfaceIds: ['door-panel-embroidery'] },
      { componentId: 'center-panel-trim', surfaceIds: ['center-panel-trim'] },
      { componentId: 'nameplate', surfaceIds: ['nameplate'] },
      { componentId: 'pedal', surfaceIds: ['pedal'] },
    ])
  })

  it('中性色先按亮度排序，其余颜色按红橙黄绿青蓝紫色相排序', () => {
    const variants = [
      { variantId: 'blue' },
      { variantId: 'white' },
      { variantId: 'red-wrap' },
      { variantId: 'red' },
      { variantId: 'orange' },
      { variantId: 'black' },
      { variantId: 'green' },
      { variantId: 'gray' },
      { variantId: 'chroma-at-threshold' },
      { variantId: 'violet' },
    ].map(({ variantId }) => ({
      variantId,
      materialFamilyId: 'ultrasuede',
      displayName: variantId,
      colorCode: `vendor-${variantId}`,
      ui: { sortColorHex: '#000000' },
      thumbnailUrl: `/${variantId}.webp`,
      reviewRequired: false,
    }))
    const colors = new Map<string, string>([
      ['blue', '#0000ff'],
      ['white', '#ffffff'],
      ['red-wrap', '#ff002b'],
      ['red', '#ff0000'],
      ['orange', '#ff8000'],
      ['black', '#000000'],
      ['green', '#00ff00'],
      ['gray', '#777777'],
      ['chroma-at-threshold', '#8a7777'],
      ['violet', '#8000ff'],
    ])
    for (const variant of variants) variant.ui.sortColorHex = colors.get(variant.variantId)!

    expect(sortMaterialVariants(variants, 'achromatic-then-rainbow')
      .map((variant) => variant.variantId))
      .toEqual([
        'black',
        'gray',
        'chroma-at-threshold',
        'white',
        'red-wrap',
        'red',
        'orange',
        'green',
        'blue',
        'violet',
      ])
  })

  it('镜头按 surface、component、category 优先级解析并兼容数字 cameraId', () => {
    const catalog = structuredClone(catalogFixture)
    const surface = catalog.surfaces.find((item) => item.surfaceId === 'wheel-material')!
    surface.ui = { cameraId: 4 }

    expect(cameraIdForSelection(catalog, {
      categoryId: 'exterior',
      componentId: 'wheel',
      surfaceId: 'wheel-material',
    })).toBe(4)
    delete surface.ui
    expect(cameraIdForSelection(catalog, {
      categoryId: 'exterior',
      componentId: 'wheel',
      surfaceId: 'wheel-material',
    })).toBe('side')
    expect(cameraIdForSelection(catalog, { categoryId: 'exterior' })).toBe('exterior')
  })

  it('option ui 驱动排序和自定义参数默认值', () => {
    const options = optionsForSurface(catalogFixture, 'exterior-body-cover')
    expect(options.map((option) => option.optionId)).toEqual([
      'body-cover-red',
      'body-cover-silver',
      'body-cover-custom',
    ])
    const custom = options.find((option) => option.optionId === 'body-cover-custom')!
    expect(createDefaultPaintCustomization(custom)).toMatchObject({
      colorHex: '#A61D24',
      roughness: 0.28,
    })
  })

  it('按 requiresSelections 过滤轮毂造型并保留轮毂颜色', () => {
    const aluminumSelections = createInitialSelections(catalogFixture)
    expect(optionsForSurface(catalogFixture, 'wheel-style', aluminumSelections)
      .map((option) => option.optionId))
      .toEqual(['wheel-style-multispoke'])

    const magnesiumSelections = {
      ...aluminumSelections,
      'wheel-material': 'wheel-magnesium-alloy',
    }
    expect(optionsForSurface(catalogFixture, 'wheel-style', magnesiumSelections)
      .map((option) => option.displayName))
      .toEqual(['款式1', '款式2', '款式3', '款式4', '款式5', '款式6', '款式7', '款式8'])
    expect(optionsForSurface(catalogFixture, 'wheel-color', magnesiumSelections))
      .toHaveLength(optionsForSurface(catalogFixture, 'wheel-color', aluminumSelections).length)
  })

  it('轮毂材质变化时归一化不兼容造型为对应首款', () => {
    const magnesium = normalizeSelections(catalogFixture, {
      ...initialSelections,
      'wheel-material': 'wheel-magnesium-alloy',
      'wheel-style': 'wheel-style-multispoke',
    })
    expect(magnesium['wheel-style']).toBe('wheel-style-magnesium-1')

    const aluminum = normalizeSelections(catalogFixture, {
      ...magnesium,
      'wheel-material': 'wheel-aluminum-alloy',
      'wheel-style': 'wheel-style-magnesium-1',
    })
    expect(aluminum['wheel-style']).toBe('wheel-style-multispoke')
  })

  it('保留禁用选项用于展示，但不会选中或归一化为禁用项', () => {
    const options = optionsForSurface(catalogFixture, 'rear-wing', initialSelections)
    const gray = options.find((option) => option.optionId === 'rear-wing-gray')!

    expect(gray.availability).toEqual({ status: 'disabled', reason: '暂不可选' })
    expect(optionIsAvailable(gray, initialSelections)).toBe(false)
    expect(normalizeSelections(catalogFixture, {
      ...initialSelections,
      'rear-wing': 'rear-wing-gray',
    })['rear-wing']).toBe('rear-wing-none')
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
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-uf7' },
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
      'steering-wheel-skin': { materialVariantId: 'alcantara-p4-9002' },
    })).toEqual({})
    expect(normalizeCustomizations(catalogFixture, {
      ...initialSelections,
      'steering-wheel-skin': 'steering-skin-ultrasuede-custom',
    }, {
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-uf7' },
    })).toEqual({
      'steering-wheel-skin': { materialVariantId: 'ultrasuede-p6-uf7' },
    })
  })
})
