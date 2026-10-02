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
  reviewRequired: false,
  thumbnailUrl: null,
  pricing: { ...pricing, isStandard },
})

const additionalDefaults = [
  ['wheel-style', 'wheel-style-multispoke', '轮毂造型'],
  ['wheel-color', 'wheel-color-bright-silver', '轮毂颜色'],
  ['lower-skirt', 'lower-skirt-aluminum', '车辆下护板'],
  ['front-caliper-color', 'front-caliper-black', '前卡钳'],
  ['rear-caliper-color', 'rear-caliper-black', '后卡钳'],
  ['engine-bay-cover', 'engine-cover-ppg', '机舱盖板'],
  ['steering-wheel-addon', 'steering-addon-eva', '加粗'],
  ['steering-center-mark', 'steering-center-standard', '回中标'],
  ['seat-backrest', 'seat-back-ultrasuede-black', '座椅接触面'],
  ['seat-bolster', 'seat-bolster-microfiber-black', '座椅边皮'],
  ['seat-shell-back', 'seat-shell-black', '碳纤维背板颜色'],
  ['seat-headrest-mark', 'seat-headrest-standard', '头枕回中标'],
  ['door-upper', 'door-upper-microfiber-black', '门板上段'],
  ['door-middle', 'door-middle-ultrasuede-black', '门板中面板'],
  ['door-armrest', 'door-armrest-microfiber-black', '门板扶手'],
  ['door-armrest-skin', 'door-armrest-skin-microfiber-black', '扶手表皮'],
  ['ip-wings', 'ip-wings-microfiber-black', '两侧翼'],
  ['ip-middle', 'ip-middle-microfiber-black', '中翼'],
  ['ip-instrument-cover', 'ip-instrument-cover-microfiber-black', '仪表盖'],
  ['ip-upper-trim', 'ip-upper-trim-microfiber-black', '上层软包'],
  ['ip-lower-trim', 'ip-lower-trim-microfiber-black', '下层软包'],
  ['ip-center-mark', 'ip-center-mark-black-paint', '仪表台回中标'],
  ['storage-soft-bag', 'storage-soft-bag-microfiber-black', '储物盒软包'],
  ['console-armrest-cover', 'console-armrest-cover-microfiber-black', '扶手盖子'],
  ['console-armrest-side', 'console-armrest-side-microfiber-black', '扶手侧边'],
  ['handbrake', 'handbrake-microfiber-black', '手刹把'],
  ['roof-surface', 'roof-woven-standard', '棚面'],
  ['a-pillar-surface', 'a-pillar-woven', 'A柱'],
  ['interior-painted-parts', 'interior-painted-spray', '内饰全车黑色喷漆件'],
  ['door-sill', 'door-sill-leather', '门板口袋'],
  ['embroidered-logo', 'embroidered-logo-standard', '缝线徽标'],
  ['center-panel-trim', 'center-panel-trim-custom', '中面板横饰板'],
  ['shift-knob', 'shift-knob-stainless', '换挡'],
  ['brake-handle', 'brake-handle-flamed-blue', '刹车'],
  ['pedal', 'pedal-racing', '脚垫'],
] as const

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
  selectionOrder: [
    'exterior-body-cover',
    'wheel-material',
    ...additionalDefaults.slice(0, 6).map(([surfaceId]) => surfaceId),
    'steering-wheel-skin',
    ...additionalDefaults.slice(6).map(([surfaceId]) => surfaceId),
  ],
  regions: [
    { regionId: 'exterior', displayName: '外观' },
    { regionId: 'interior', displayName: '内饰部件' },
  ],
  categories: [
    { categoryId: 'exterior', regionId: 'exterior', displayName: '外观' },
    { categoryId: 'steering-wheel', regionId: 'interior', displayName: '方向盘' },
  ],
  components: [
    { componentId: 'body', categoryId: 'exterior', displayName: '车身' },
    { componentId: 'wheel', categoryId: 'exterior', displayName: '轮毂' },
    { componentId: 'steering-wheel', categoryId: 'steering-wheel', displayName: '方向盘' },
  ],
  surfaces: [
    { surfaceId: 'exterior-body-cover', componentId: 'body', displayName: '全车身覆盖件', required: true, reviewRequired: false },
    { surfaceId: 'wheel-material', componentId: 'wheel', displayName: '轮毂材质', required: true, reviewRequired: false },
    ...additionalDefaults.slice(0, 6).map(([surfaceId, , displayName]) => ({
      surfaceId,
      componentId: 'wheel',
      displayName,
      required: true,
      reviewRequired: false,
    })),
    { surfaceId: 'steering-wheel-skin', componentId: 'steering-wheel', displayName: '表皮', required: true, reviewRequired: false },
    ...additionalDefaults.slice(6).map(([surfaceId, , displayName]) => ({
      surfaceId,
      componentId: 'steering-wheel',
      displayName,
      required: true,
      reviewRequired: false,
    })),
  ],
  materialFamilies: [
    { materialFamilyId: 'paint', displayName: '喷漆' },
    { materialFamilyId: 'aluminum-alloy', displayName: '铝合金' },
    { materialFamilyId: 'magnesium-alloy', displayName: '镁合金' },
    { materialFamilyId: 'ultrasuede', displayName: 'Ultrasuede' },
    { materialFamilyId: 'alcantara', displayName: 'Alcantara' },
  ],
  materialVariants: [
    {
      variantId: 'ultrasuede-p6-sf4',
      materialFamilyId: 'ultrasuede',
      displayName: 'SF4',
      colorCode: 'SF4',
      thumbnailUrl: '/sc01/thumbnails/ultrasuede-p6-sf4.png',
      reviewRequired: false,
    },
    {
      variantId: 'alcantara-p2-2911',
      materialFamilyId: 'alcantara',
      displayName: '2911',
      colorCode: '2911',
      thumbnailUrl: '/sc01/thumbnails/alcantara-p2-2911.png',
      reviewRequired: false,
    },
  ],
  assetManifest: '/sc01/crop-manifest.json',
  options: [
    {
      ...option('body-cover-red', 'exterior-body-cover', 'paint', '红色'),
      pricing: { ...pricing, unitPriceMinor: 0, quantity: 1, status: 'confirmed' },
    },
    {
      ...option('body-cover-silver', 'exterior-body-cover', 'paint', '银色'),
      pricing: { ...pricing, unitPriceMinor: 0, quantity: 1, status: 'confirmed' },
    },
    {
      ...option('body-cover-custom', 'exterior-body-cover', 'paint', '自定义车漆', false),
      pricing: {
        ...pricing,
        unitPriceMinor: 960000,
        quantity: 1,
        isStandard: false,
        status: 'confirmed',
      },
    },
    option('wheel-aluminum-alloy', 'wheel-material', 'aluminum-alloy', '铝合金'),
    option('wheel-magnesium-alloy', 'wheel-material', 'magnesium-alloy', '镁合金', false),
    option('steering-skin-ultrasuede-black', 'steering-wheel-skin', 'ultrasuede', 'Ultrasuede（黑）'),
    option('steering-skin-alcantara', 'steering-wheel-skin', 'alcantara', 'Alcantara', false),
    ...additionalDefaults.map(([surfaceId, optionId, displayName]) => (
      option(optionId, surfaceId, 'paint', displayName)
    )),
  ],
}

export const initialSelections = Object.fromEntries(
  catalogFixture.selectionOrder.map((surfaceId) => {
    const options = catalogFixture.options.filter((item) => item.surfaceId === surfaceId)
    return [surfaceId, (options.find((item) => item.pricing.isStandard) ?? options[0]).optionId]
  }),
)

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
