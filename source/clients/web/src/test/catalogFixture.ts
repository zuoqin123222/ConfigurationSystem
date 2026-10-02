import type { CatalogOption, CatalogV2, LegacyCatalog } from '../types'

const pricing = {
  unitPriceMinor: null,
  quantity: null,
  pricingUnit: 'per-vehicle',
  isStandard: true,
  status: 'unconfirmed',
  quotable: false,
} as const

const option = (
  optionId: string,
  surfaceId: string,
  materialFamilyId: string,
  displayName: string,
  isStandard = true,
  renderRelevant = true,
): CatalogOption => ({
  optionId,
  surfaceId,
  materialFamilyId,
  displayName,
  colorCode: null,
  finish: null,
  renderRelevant,
  pricing: { ...pricing, isStandard },
})

export const catalogFixture: CatalogV2 = {
  schemaVersion: '2.0.0',
  catalogVersion: 'sc01-draft-20260121',
  lifecycle: 'draft',
  currency: 'CNY',
  vehicle: {
    vehicleId: 'sc01',
    displayName: 'SC01',
    basePriceMinor: null,
    priceStatus: 'unconfirmed',
    quotable: false,
  },
  selectionOrder: ['exterior-body-cover', 'wheel-material', 'steering-wheel-skin'],
  categories: [
    { categoryId: 'exterior', displayName: '外观' },
    { categoryId: 'steering-wheel', displayName: '方向盘' },
  ],
  components: [
    { componentId: 'body', categoryId: 'exterior', displayName: '车身' },
    { componentId: 'wheel', categoryId: 'exterior', displayName: '轮毂' },
    { componentId: 'steering-wheel', categoryId: 'steering-wheel', displayName: '方向盘' },
  ],
  surfaces: [
    { surfaceId: 'exterior-body-cover', componentId: 'body', displayName: '全车身覆盖件', required: true },
    { surfaceId: 'wheel-material', componentId: 'wheel', displayName: '轮毂材质', required: true },
    { surfaceId: 'steering-wheel-skin', componentId: 'steering-wheel', displayName: '表皮', required: true },
  ],
  materialFamilies: [
    { materialFamilyId: 'paint', displayName: '喷漆' },
    { materialFamilyId: 'aluminum-alloy', displayName: '铝合金' },
    { materialFamilyId: 'magnesium-alloy', displayName: '镁合金' },
    { materialFamilyId: 'ultrasuede', displayName: 'Ultrasuede' },
    { materialFamilyId: 'alcantara', displayName: 'Alcantara' },
  ],
  options: [
    option('body-cover-red', 'exterior-body-cover', 'paint', '红色'),
    option('body-cover-yellow', 'exterior-body-cover', 'paint', '黄色'),
    option('wheel-aluminum-alloy', 'wheel-material', 'aluminum-alloy', '铝合金'),
    option('wheel-magnesium-alloy', 'wheel-material', 'magnesium-alloy', '镁合金', false),
    option('steering-skin-ultrasuede-black', 'steering-wheel-skin', 'ultrasuede', 'Ultrasuede（黑）'),
    option('steering-skin-alcantara', 'steering-wheel-skin', 'alcantara', 'Alcantara', false),
  ],
}

export const legacyCatalogFixture: LegacyCatalog = {
  catalogVersion: 'mvp-v1',
  vehicle: { vehicleId: 'demo-car' },
  parts: [
    { partId: 'paint', options: [{ optionId: 'paint-red' }] },
    { partId: 'wheel', options: [{ optionId: 'wheel-sport' }] },
    { partId: 'interior', options: [{ optionId: 'interior-dark' }] },
    { partId: 'frame', options: [{ optionId: 'frame-black' }] },
  ],
  renderViews: [
    { renderViewId: 'front', zhName: '正前' },
    { renderViewId: 'front-left', zhName: '左前' },
    { renderViewId: 'side', zhName: '侧面' },
    { renderViewId: 'rear-right', zhName: '右后' },
  ],
}
