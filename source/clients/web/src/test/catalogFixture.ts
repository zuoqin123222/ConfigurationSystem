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
  parameters: {
    color: null,
    material: { materialFamilyId, variantId: null },
  },
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

const optionalSurfaceIds = new Set([
  'lower-skirt',
  'engine-bay-cover',
  'steering-wheel-addon',
  'interior-painted-parts',
  'door-sill',
  'center-panel-trim',
  'shift-knob',
  'brake-handle',
  'pedal',
])

function componentForSurface(surfaceId: string): string {
  if (surfaceId.startsWith('wheel-')) return 'wheel'
  if (surfaceId === 'lower-skirt') return 'underbody'
  if (surfaceId.includes('caliper')) return 'caliper'
  if (surfaceId === 'engine-bay-cover') return 'chassis'
  if (surfaceId.startsWith('steering-')) return 'steering-wheel'
  if (surfaceId.startsWith('seat-')) return 'seat'
  if (surfaceId.startsWith('door-')) return 'door-trim'
  if (surfaceId.startsWith('ip-')) return 'instrument-panel'
  if (surfaceId.startsWith('storage-')) return 'storage-box'
  if (surfaceId.startsWith('console-') || surfaceId === 'handbrake') return 'center-console'
  if (surfaceId.startsWith('roof-')) return 'roof'
  if (surfaceId.startsWith('a-pillar-')) return 'a-pillar'
  return 'personalization'
}

export const catalogFixture: CatalogV2 = {
  schemaVersion: '2.0.0',
  catalogVersion: 'sc01-draft-20260121',
  lifecycle: 'draft',
  currency: 'CNY',
  vehicle: {
    vehicleId: 'sc01',
    displayName: 'SC01',
    basePriceMinor: 22980000,
    priceStatus: 'confirmed',
    quotable: false,
  },
  selectionOrder: [
    'exterior-body-cover',
    'wheel-material',
    ...additionalDefaults.slice(0, 6).map(([surfaceId]) => surfaceId),
    'steering-wheel-skin',
    ...additionalDefaults.slice(6).map(([surfaceId]) => surfaceId),
  ],
  defaultSelections: {
    'exterior-body-cover': 'body-cover-red',
    'wheel-material': 'wheel-aluminum-alloy',
    'wheel-style': 'wheel-style-multispoke',
    'wheel-color': 'wheel-color-bright-silver',
    'front-caliper-color': 'front-caliper-black',
    'rear-caliper-color': 'rear-caliper-black',
    'steering-wheel-skin': 'steering-skin-ultrasuede-black',
    'steering-center-mark': 'steering-center-standard',
    'seat-backrest': 'seat-back-ultrasuede-black',
    'seat-bolster': 'seat-bolster-microfiber-black',
    'seat-shell-back': 'seat-shell-black',
    'seat-headrest-mark': 'seat-headrest-standard',
    'door-upper': 'door-upper-microfiber-black',
    'door-middle': 'door-middle-ultrasuede-black',
    'door-armrest': 'door-armrest-microfiber-black',
    'door-armrest-skin': 'door-armrest-skin-microfiber-black',
    'ip-wings': 'ip-wings-microfiber-black',
    'ip-middle': 'ip-middle-microfiber-black',
    'ip-instrument-cover': 'ip-instrument-cover-microfiber-black',
    'ip-upper-trim': 'ip-upper-trim-microfiber-black',
    'ip-lower-trim': 'ip-lower-trim-microfiber-black',
    'ip-center-mark': 'ip-center-mark-black-paint',
    'storage-soft-bag': 'storage-soft-bag-microfiber-black',
    'console-armrest-cover': 'console-armrest-cover-microfiber-black',
    'console-armrest-side': 'console-armrest-side-microfiber-black',
    'handbrake': 'handbrake-microfiber-black',
    'roof-surface': 'roof-woven-standard',
    'a-pillar-surface': 'a-pillar-woven',
    'embroidered-logo': 'embroidered-logo-standard',
  },
  regions: [
    { regionId: 'exterior', displayName: '外观' },
    { regionId: 'interior', displayName: '内饰部件' },
    { regionId: 'performance', displayName: '性能' },
    { regionId: 'personalization', displayName: '其他个性化配置' },
  ],
  categories: [
    { categoryId: 'exterior', regionId: 'exterior', displayName: '外饰' },
    { categoryId: 'interior', regionId: 'interior', displayName: '内饰' },
    { categoryId: 'performance', regionId: 'performance', displayName: '性能' },
    { categoryId: 'personalization', regionId: 'personalization', displayName: '个性化' },
  ],
  components: [
    { componentId: 'car-paint', categoryId: 'exterior', displayName: '车漆' },
    { componentId: 'chassis', categoryId: 'exterior', displayName: '车架' },
    { componentId: 'wheel', categoryId: 'exterior', displayName: '轮毂' },
    { componentId: 'caliper', categoryId: 'exterior', displayName: '卡钳' },
    { componentId: 'steering-wheel', categoryId: 'interior', displayName: '方向盘' },
    { componentId: 'instrument-panel', categoryId: 'interior', displayName: 'IP' },
    { componentId: 'a-pillar', categoryId: 'interior', displayName: 'A柱' },
    { componentId: 'seat', categoryId: 'interior', displayName: '座椅' },
    { componentId: 'door-trim', categoryId: 'interior', displayName: '门板' },
    { componentId: 'storage-box', categoryId: 'interior', displayName: '储物盒盖' },
    { componentId: 'center-console', categoryId: 'interior', displayName: '副仪表台' },
    { componentId: 'roof', categoryId: 'interior', displayName: '车顶' },
    { componentId: 'underbody', categoryId: 'performance', displayName: '下护板' },
    { componentId: 'personalization', categoryId: 'personalization', displayName: '个性化' },
  ],
  surfaces: [
    { surfaceId: 'exterior-body-cover', componentId: 'car-paint', displayName: '车漆', required: true, reviewRequired: false },
    { surfaceId: 'wheel-material', componentId: 'wheel', displayName: '轮毂材质', required: true, reviewRequired: false },
    ...additionalDefaults.slice(0, 6).map(([surfaceId, , displayName]) => ({
      surfaceId,
      componentId: componentForSurface(surfaceId),
      displayName,
      required: !optionalSurfaceIds.has(surfaceId),
      reviewRequired: false,
    })),
    { surfaceId: 'steering-wheel-skin', componentId: 'steering-wheel', displayName: '表皮', required: true, reviewRequired: false },
    ...additionalDefaults.slice(6).map(([surfaceId, , displayName]) => ({
      surfaceId,
      componentId: componentForSurface(surfaceId),
      displayName,
      required: !optionalSurfaceIds.has(surfaceId),
      reviewRequired: false,
    })),
  ],
  materialFamilies: [
    { materialFamilyId: 'paint', displayName: '喷漆' },
    { materialFamilyId: 'aluminum-alloy', displayName: '铝合金' },
    { materialFamilyId: 'magnesium-alloy', displayName: '镁合金' },
    { materialFamilyId: 'ultrasuede', displayName: 'Ultrasuede' },
    { materialFamilyId: 'alcantara', displayName: 'Alcantara' },
    { materialFamilyId: 'leather', displayName: '真皮' },
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
    {
      variantId: 'leather-p10-1217',
      materialFamilyId: 'leather',
      displayName: '1217',
      colorCode: '1217',
      thumbnailUrl: '/sc01/thumbnails/leather-p10-1217.webp',
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
      parameters: {
        color: { mode: 'custom', value: null, required: true },
        material: { materialFamilyId: 'paint', variantId: null },
      },
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
    {
      ...option(
        'seat-back-ultrasuede-custom',
        'seat-backrest',
        'ultrasuede',
        'Ultrasuede 定制',
        false,
      ),
      pricing: {
        ...pricing,
        unitPriceMinor: 248000,
        quantity: 1,
        isStandard: false,
        status: 'confirmed',
      },
    },
    {
      ...option(
        'steering-skin-ultrasuede-custom',
        'steering-wheel-skin',
        'ultrasuede',
        'Ultrasuede（PDF 色卡定制）',
        false,
      ),
      parameters: {
        color: { mode: 'variant', value: null, required: true },
        material: { materialFamilyId: 'ultrasuede', variantId: null },
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 118000,
        quantity: 1,
        isStandard: false,
        status: 'confirmed',
      },
    },
    {
      ...option('steering-skin-alcantara', 'steering-wheel-skin', 'alcantara', 'Alcantara', false),
      parameters: {
        color: { mode: 'variant', value: null, required: true },
        material: { materialFamilyId: 'alcantara', variantId: null },
      },
    },
    {
      ...option('steering-skin-leather', 'steering-wheel-skin', 'leather', '真皮定制', false),
      parameters: {
        color: { mode: 'variant', value: null, required: true },
        material: { materialFamilyId: 'leather', variantId: null },
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 128000,
        quantity: 1,
        isStandard: false,
        status: 'confirmed',
      },
    },
    {
      ...option('steering-skin-leather-user', 'steering-wheel-skin', 'leather', '真皮用户定制', false),
      parameters: {
        color: { mode: 'variant', value: null, required: true },
        material: { materialFamilyId: 'leather', variantId: null },
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 128000,
        quantity: 1,
        isStandard: false,
        status: 'confirmed',
      },
    },
    ...additionalDefaults.map(([surfaceId, optionId, displayName]) => (
      option(optionId, surfaceId, 'paint', displayName, !optionalSurfaceIds.has(surfaceId))
    )),
  ],
}

export const initialSelections = { ...catalogFixture.defaultSelections }

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
