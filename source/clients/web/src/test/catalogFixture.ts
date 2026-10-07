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
  materialFamilyId: string | null,
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
    material: materialFamilyId ? { materialFamilyId, variantId: null } : null,
  },
  pricing: { ...pricing, isStandard },
})

const additionalDefaults = [
  ['wheel-style', 'wheel-style-multispoke', '轮毂造型'],
  ['wheel-color', 'wheel-color-bright-silver', '轮毂颜色'],
  ['lower-skirt', 'lower-skirt-aluminum', '车辆下护板'],
  ['front-caliper-color', 'front-caliper-black', '前卡钳'],
  ['rear-caliper-color', 'rear-caliper-black', '后卡钳'],
  ['engine-bay-cover', 'engine-cover-silver', '车架'],
  ['steering-wheel-addon', 'steering-addon-eva', '加粗(EVA海绵)'],
  ['steering-center-mark', 'steering-center-standard', '回中标'],
  ['seat-backrest', 'seat-back-ultrasuede-black', '接触面'],
  ['seat-bolster', 'seat-bolster-microfiber-black', '边皮'],
  ['seat-shell-back', 'seat-shell-carbon-original', '背板'],
  ['seat-headrest-mark', 'seat-headrest-mark-ultrasuede', '回中标'],
  ['door-upper', 'door-upper-microfiber-black', '上段'],
  ['door-middle', 'door-middle-ultrasuede-black', '中面板'],
  ['door-armrest', 'door-armrest-microfiber-black', '扶手'],
  ['door-armrest-skin', 'door-armrest-skin-microfiber-black', '扶手表皮'],
  ['ip-wings', 'ip-wings-microfiber-black', '两侧翼'],
  ['ip-middle', 'ip-middle-microfiber-black', '中翼'],
  ['ip-instrument-cover', 'ip-instrument-cover-microfiber-black', '仪表盖'],
  ['ip-upper-trim', 'ip-upper-trim-microfiber-black', '上层软包'],
  ['ip-lower-trim', 'ip-lower-trim-microfiber-black', '下层软包'],
  ['ip-center-mark', 'ip-center-mark-uncovered-black', '回中标'],
  ['storage-soft-bag', 'storage-soft-bag-microfiber-black', '软包'],
  ['console-armrest-cover', 'console-armrest-cover-microfiber-black', '扶手盖子'],
  ['console-armrest-side', 'console-armrest-side-microfiber-black', '扶手侧边'],
  ['handbrake', 'handbrake-microfiber-black', '手刹把'],
  ['roof-surface', 'roof-woven-standard', '棚面'],
  ['a-pillar-surface', 'a-pillar-woven', 'A柱'],
  ['interior-painted-parts', 'interior-painted-spray', '内饰全车黑色喷漆件'],
  ['door-sill', 'door-sill-none', '门板口袋'],
  ['embroidered-logo', 'embroidered-logo-black', '缝线'],
  ['headrest-embroidery', 'headrest-embroidery-none', '头枕刺绣'],
  ['door-panel-embroidery', 'door-panel-embroidery-none', '门中板刺绣'],
  ['center-panel-trim', 'center-panel-trim-black', '中面板缝线'],
  ['nameplate', 'nameplate-stainless', '铭牌'],
  ['pedal', 'pedal-racing', '脚垫'],
  ['rear-wing', 'rear-wing-none', '尾翼'],
] as const

const optionalSurfaceIds = new Set([
  'lower-skirt',
  'steering-wheel-addon',
  'interior-painted-parts',
  'nameplate',
  'pedal',
])

const catalogSelectionOrder: string[] = [
  'exterior-body-cover',
  'wheel-material',
  ...additionalDefaults.slice(0, 6).map(([surfaceId]) => surfaceId),
  'steering-wheel-skin',
  ...additionalDefaults.slice(6).map(([surfaceId]) => surfaceId),
]

function proxySlotId(surfaceId: string): string {
  return `A5Proxy_${surfaceId
    .split('-')
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join('')}`
}

function componentForSurface(surfaceId: string): string {
  if (surfaceId === 'door-sill') return 'personalization'
  if (surfaceId === 'door-panel-embroidery') return 'personalization'
  if (surfaceId.startsWith('wheel-')) return 'wheel'
  if (surfaceId === 'lower-skirt') return 'underbody'
  if (surfaceId.includes('caliper')) return 'caliper'
  if (surfaceId === 'engine-bay-cover') return 'chassis'
  if (surfaceId === 'rear-wing') return 'rear-wing'
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
  catalogVersion: 'sc01-draft-20261007',
  lifecycle: 'draft',
  currency: 'CNY',
  vehicle: {
    vehicleId: 'sc01',
    displayName: 'SC01',
    basePriceMinor: 22980000,
    priceStatus: 'confirmed',
    quotable: false,
  },
  selectionOrder: catalogSelectionOrder,
  defaultSelections: {
    'exterior-body-cover': 'body-cover-red',
    'wheel-material': 'wheel-aluminum-alloy',
    'wheel-style': 'wheel-style-multispoke',
    'wheel-color': 'wheel-color-bright-silver',
    'front-caliper-color': 'front-caliper-black',
    'rear-caliper-color': 'rear-caliper-black',
    'engine-bay-cover': 'engine-cover-silver',
    'steering-wheel-skin': 'steering-skin-ultrasuede-black',
    'steering-center-mark': 'steering-center-standard',
    'seat-backrest': 'seat-back-ultrasuede-black',
    'seat-bolster': 'seat-bolster-microfiber-black',
    'seat-shell-back': 'seat-shell-carbon-original',
    'seat-headrest-mark': 'seat-headrest-mark-ultrasuede',
    'door-upper': 'door-upper-microfiber-black',
    'door-middle': 'door-middle-ultrasuede-black',
    'door-armrest': 'door-armrest-microfiber-black',
    'door-armrest-skin': 'door-armrest-skin-microfiber-black',
    'ip-wings': 'ip-wings-microfiber-black',
    'ip-middle': 'ip-middle-microfiber-black',
    'ip-instrument-cover': 'ip-instrument-cover-microfiber-black',
    'ip-upper-trim': 'ip-upper-trim-microfiber-black',
    'ip-lower-trim': 'ip-lower-trim-microfiber-black',
    'ip-center-mark': 'ip-center-mark-uncovered-black',
    'storage-soft-bag': 'storage-soft-bag-microfiber-black',
    'console-armrest-cover': 'console-armrest-cover-microfiber-black',
    'console-armrest-side': 'console-armrest-side-microfiber-black',
    'handbrake': 'handbrake-microfiber-black',
    'roof-surface': 'roof-woven-standard',
    'a-pillar-surface': 'a-pillar-woven',
    'door-sill': 'door-sill-none',
    'embroidered-logo': 'embroidered-logo-black',
    'headrest-embroidery': 'headrest-embroidery-none',
    'door-panel-embroidery': 'door-panel-embroidery-none',
    'center-panel-trim': 'center-panel-trim-black',
    'rear-wing': 'rear-wing-none',
  },
  optionIdAliases: {
    'embroidered-logo-standard': 'embroidered-logo-black',
    'embroidered-logo-custom': 'embroidered-logo-black',
    'brake-handle-flamed-blue': 'headrest-embroidery-custom',
    'brake-handle-door-panel': 'door-panel-embroidery-custom',
  },
  interactionCameras: [
    { cameraId: 'exterior', legacyIndex: 0, zone: 'exterior', order: 0, displayName: '外观', iconUrl: '/camera-exterior.svg' },
    { cameraId: 'wheel', legacyIndex: 2, zone: 'exterior', order: 1, displayName: '轮毂', iconUrl: '/camera-wheel.svg' },
    { cameraId: 'driver', legacyIndex: 4, zone: 'interior', order: 2, displayName: '驾驶位', iconUrl: '/camera-driver.svg' },
    { cameraId: 'seat', legacyIndex: null, zone: 'interior', order: 3, displayName: '座椅', iconUrl: '/camera-seat.svg' },
    { cameraId: 'front-cabin', legacyIndex: 5, zone: 'interior', order: 4, displayName: '前舱', iconUrl: '/camera-front-cabin.svg' },
  ],
  skeletalMeshPath: '/Game/Configurator/AuthorizedAudiA5/SK_A5_Car.SK_A5_Car',
  sequencePath: '/Game/Configurator/AuthorizedAudiA5/Animations/A_A5_FullVehicle.A_A5_FullVehicle',
  vehicleSurfaceBinding: {
    schemaVersion: '1.0.0',
    capability: 'proxy',
    bindings: catalogSelectionOrder.map((surfaceId) => ({
      surfaceId,
      materialSlotIds: [proxySlotId(surfaceId)],
    })),
    unsupportedSurfaceIds: [],
  },
  animations: [
    {
      animationId: 'hood',
      displayName: '开启机舱盖',
      frameRate: 30,
      startFrame: 0,
      endFrame: 30,
      loopMode: 'none',
      closeMode: 'reverse',
    },
    {
      animationId: 'trunk',
      displayName: '后盖往复',
      frameRate: 30,
      startFrame: 93,
      endFrame: 123,
      loopMode: 'none',
      closeMode: 'reverse',
    },
    {
      animationId: 'wheel-spin',
      displayName: '车轮旋转',
      frameRate: 30,
      startFrame: 124,
      endFrame: 184,
      loopMode: 'forward',
      closeMode: 'stop',
    },
  ],
  regions: [
    { regionId: 'exterior', displayName: '外观' },
    { regionId: 'interior', displayName: '内饰部件' },
    { regionId: 'performance', displayName: '性能' },
    { regionId: 'personalization', displayName: '其他个性化配置' },
  ],
  categories: [
    { categoryId: 'exterior', regionId: 'exterior', displayName: '外饰', ui: { order: 0, iconUrl: '/category-exterior.svg', cameraId: 'exterior', navigationMode: 'tabs', layout: 'single' } },
    { categoryId: 'interior', regionId: 'interior', displayName: '内饰', ui: { order: 1, iconUrl: '/category-interior.svg', cameraId: 'front-cabin', navigationMode: 'tabs', layout: 'single' } },
    { categoryId: 'performance', regionId: 'performance', displayName: '性能', ui: { order: 2, iconUrl: '/category-performance.svg', cameraId: 'exterior', navigationMode: 'tabs', layout: 'single' } },
    { categoryId: 'personalization', regionId: 'personalization', displayName: '个性化', ui: { order: 3, iconUrl: '/category-personalization.svg', cameraId: 'front-cabin', navigationMode: 'surfaces-as-components', layout: 'single' } },
  ],
  components: [
    { componentId: 'car-paint', categoryId: 'exterior', displayName: '车漆' },
    { componentId: 'chassis', categoryId: 'exterior', displayName: '车架', ui: { animationId: 'hood' } },
    { componentId: 'wheel', categoryId: 'exterior', displayName: '轮毂', ui: { order: 2, cameraId: 'wheel', navigationMode: 'none', layout: 'stack' } },
    { componentId: 'caliper', categoryId: 'exterior', displayName: '卡钳' },
    { componentId: 'steering-wheel', categoryId: 'interior', displayName: '方向盘', ui: { order: 0, cameraId: 'driver', navigationMode: 'tabs', layout: 'single' } },
    { componentId: 'instrument-panel', categoryId: 'interior', displayName: '仪表台' },
    { componentId: 'a-pillar', categoryId: 'interior', displayName: 'A柱' },
    { componentId: 'seat', categoryId: 'interior', displayName: '座椅', ui: { order: 3, cameraId: 'seat', navigationMode: 'tabs', layout: 'single' } },
    { componentId: 'door-trim', categoryId: 'interior', displayName: '门板' },
    { componentId: 'storage-box', categoryId: 'interior', displayName: '储物盒盖' },
    { componentId: 'center-console', categoryId: 'interior', displayName: '副仪表台' },
    { componentId: 'roof', categoryId: 'interior', displayName: '车顶' },
    { componentId: 'underbody', categoryId: 'performance', displayName: '下护板' },
    { componentId: 'rear-wing', categoryId: 'performance', displayName: '尾翼' },
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
    { materialFamilyId: 'ultrasuede', displayName: '奥司维', ui: { variantSort: 'achromatic-then-rainbow' } },
    { materialFamilyId: 'alcantara', displayName: 'Alcantara', ui: { variantSort: 'achromatic-then-rainbow' } },
    { materialFamilyId: 'leather', displayName: '牛皮', ui: { variantSort: 'achromatic-then-rainbow' } },
    { materialFamilyId: 'microfiber', displayName: '超纤皮', ui: { variantSort: 'achromatic-then-rainbow' } },
  ],
  materialVariants: [
    {
      variantId: 'ultrasuede-p6-uf7',
      materialFamilyId: 'ultrasuede',
      displayName: 'Black UF7',
      colorCode: 'UF7',
      thumbnailUrl: '/sc01/thumbnails/ultrasuede-p6-uf7.webp',
      reviewRequired: false,
    },
    {
      variantId: 'alcantara-p4-9002',
      materialFamilyId: 'alcantara',
      displayName: '9002',
      colorCode: '9002',
      thumbnailUrl: '/sc01/thumbnails/alcantara-p4-9002.webp',
      reviewRequired: false,
    },
    {
      variantId: 'leather-p9-9743',
      materialFamilyId: 'leather',
      displayName: '9743 Nero',
      colorCode: '9743',
      thumbnailUrl: '/sc01/thumbnails/leather-p9-9743.webp',
      reviewRequired: false,
    },
    {
      variantId: 'microfiber-p16-np-3048',
      materialFamilyId: 'microfiber',
      displayName: 'NP-3048 黑',
      colorCode: 'NP-3048',
      thumbnailUrl: '/sc01/thumbnails/microfiber-p16-np-3048.webp',
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
      ui: {
        order: 2,
        iconUrl: '/sc01/option-icons/rainbow.svg',
        control: 'color-picker',
        defaultParameters: {
          colorHex: '#A61D24',
          metallic: 0.35,
          roughness: 0.28,
          clearCoat: 0.8,
          orangePeel: 0.15,
          flakeIntensity: 0.25,
        },
      },
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
    ...Array.from({ length: 9 }, (_, index) => ({
      ...option(
        index === 0 ? 'wheel-style-magnesium-default' : `wheel-style-magnesium-${index}`,
        'wheel-style',
        'magnesium-alloy',
        index === 0 ? '默认同款' : `款式${index}`,
        index === 0,
      ),
      requiresSelections: { 'wheel-material': 'wheel-magnesium-alloy' },
      pricing: {
        ...pricing,
        unitPriceMinor: index === 0 ? 0 : null,
        quantity: 1,
        isStandard: index === 0,
        status: index === 0 ? 'confirmed' as const : 'unconfirmed' as const,
      },
    })),
    option('steering-skin-ultrasuede-black', 'steering-wheel-skin', 'ultrasuede', '奥司维（黑）'),
    {
      ...option(
        'seat-back-ultrasuede-custom',
        'seat-backrest',
        'ultrasuede',
        '奥司维定制',
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
        '奥司维',
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
      ...option('steering-skin-leather', 'steering-wheel-skin', 'leather', '牛皮', false),
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
      ...option(
        'engine-cover-ppg-custom',
        'engine-bay-cover',
        'paint',
        '自定义颜色(PPG涂层)',
        false,
      ),
      ui: {
        order: 1,
        iconUrl: '/sc01/option-icons/rainbow.svg',
        control: 'color-picker',
        defaultParameters: {
          colorHex: '#A61D24',
          metallic: 0.35,
          roughness: 0.28,
          clearCoat: 0.8,
          orangePeel: 0.15,
          flakeIntensity: 0.25,
        },
      },
      parameters: {
        color: { mode: 'custom' as const, value: null, required: true },
        material: { materialFamilyId: 'paint', variantId: null },
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 960000,
        quantity: 1,
        isStandard: false,
        status: 'confirmed' as const,
      },
    },
    {
      ...option('seat-shell-custom', 'seat-shell-back', 'paint', '自定义颜色', false),
      ui: {
        order: 1,
        iconUrl: '/sc01/option-icons/rainbow.svg',
        control: 'color-picker',
        defaultParameters: {
          colorHex: '#A61D24',
          metallic: 0.35,
          roughness: 0.18,
          clearCoat: 0.8,
          orangePeel: 0.15,
          flakeIntensity: 0.25,
        },
      },
      parameters: {
        color: { mode: 'custom' as const, value: null, required: true },
        material: { materialFamilyId: 'paint', variantId: null },
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 168000,
        quantity: 2,
        isStandard: false,
        status: 'confirmed' as const,
      },
    },
    ...([
      ['seat-headrest-mark-alcantara', 'alcantara', 'Alcantara'],
      ['seat-headrest-mark-microfiber', 'microfiber', '超纤皮'],
      ['seat-headrest-mark-leather', 'leather', '牛皮'],
    ] as const).map(([optionId, materialFamilyId, displayName]) => ({
      ...option(optionId, 'seat-headrest-mark', materialFamilyId, displayName, false),
      parameters: {
        color: { mode: 'variant' as const, value: null, required: true },
        material: { materialFamilyId, variantId: null },
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 0,
        quantity: 1,
        isStandard: false,
        status: 'confirmed' as const,
      },
    })),
    ...([
      ['embroidered-logo-red', '红色', '#D71920'],
      ['embroidered-logo-yellow', '黄色', '#FFD400'],
      ['embroidered-logo-blue', '蓝色', '#1769E0'],
      ['embroidered-logo-green', '绿色', '#159447'],
    ] as const).map(([optionId, displayName, colorCode], index) => ({
      ...option(optionId, 'embroidered-logo', null, displayName, false),
      colorCode,
      parameters: {
        color: { mode: 'fixed' as const, value: colorCode, required: true },
        material: null,
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 0,
        quantity: 1,
        isStandard: false,
        status: 'confirmed' as const,
      },
      ui: { order: index + 1, control: 'swatch' as const },
    })),
    {
      ...option('headrest-embroidery-custom', 'headrest-embroidery', null, '刺绣', false),
      pricing: {
        ...pricing,
        unitPriceMinor: 128800,
        quantity: 1,
        isStandard: false,
        status: 'confirmed' as const,
      },
    },
    {
      ...option('door-panel-embroidery-custom', 'door-panel-embroidery', null, '刺绣', false),
      pricing: {
        ...pricing,
        unitPriceMinor: 168800,
        quantity: 1,
        isStandard: false,
        status: 'confirmed' as const,
      },
    },
    {
      ...option('center-panel-trim-custom', 'center-panel-trim', null, '自定义颜色', false),
      parameters: {
        color: { mode: 'custom' as const, value: null, required: true },
        material: null,
      },
      pricing: {
        ...pricing,
        unitPriceMinor: 30000,
        quantity: 2,
        isStandard: false,
        status: 'confirmed' as const,
      },
      ui: {
        order: 1,
        iconUrl: '/sc01/option-icons/rainbow.svg',
        control: 'color-picker' as const,
        defaultParameters: {
          colorHex: '#D71920',
          metallic: 0,
          roughness: 0.65,
          clearCoat: 0,
          orangePeel: 0,
          flakeIntensity: 0,
        },
      },
    },
    {
      ...option('pedal-luxury-carpet', 'pedal', null, '金属板+豪华地毯', false),
      pricing: {
        ...pricing,
        unitPriceMinor: 168000,
        quantity: 1,
        isStandard: false,
        status: 'confirmed' as const,
      },
    },
    {
      ...option('rear-wing-gray', 'rear-wing', 'paint', '灰色', false),
      availability: { status: 'disabled' as const, reason: '暂不可选' },
      colorCode: '#808080',
      parameters: {
        color: { mode: 'fixed' as const, value: '#808080', required: true },
        material: { materialFamilyId: 'paint', variantId: null },
      },
      pricing: {
        ...pricing,
        unitPriceMinor: null,
        quantity: 1,
        isStandard: false,
        status: 'unconfirmed' as const,
      },
      ui: { order: 1, control: 'swatch' as const },
    },
    ...additionalDefaults.map(([surfaceId, optionId, displayName]) => {
      const result = option(
        optionId,
        surfaceId,
        'paint',
        displayName,
        !optionalSurfaceIds.has(surfaceId),
      )
      if (surfaceId === 'engine-bay-cover') {
        return {
          ...result,
          displayName: '银色',
          colorCode: '#C0C0C0',
          parameters: {
            color: { mode: 'fixed' as const, value: '#C0C0C0', required: true },
            material: { materialFamilyId: 'paint', variantId: null },
          },
        }
      }
      if (surfaceId === 'wheel-style') {
        return {
          ...result,
          displayName: '多条幅轮毂',
          requiresSelections: { 'wheel-material': 'wheel-aluminum-alloy' },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'door-sill') {
        return {
          ...result,
          displayName: '无口袋',
          materialFamilyId: null,
          parameters: { color: null, material: null },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'seat-headrest-mark') {
        return {
          ...result,
          displayName: '奥司维',
          materialFamilyId: 'ultrasuede',
          parameters: {
            color: { mode: 'variant' as const, value: null, required: true },
            material: { materialFamilyId: 'ultrasuede', variantId: null },
          },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'seat-shell-back') {
        return {
          ...result,
          displayName: '高光原色碳纤维',
          materialFamilyId: 'paint',
          finish: 'gloss',
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'steering-wheel-addon') {
        return {
          ...result,
          displayName: '加粗(EVA海绵)',
        }
      }
      if (surfaceId === 'embroidered-logo') {
        return {
          ...result,
          displayName: '黑色',
          materialFamilyId: null,
          colorCode: '#111111',
          parameters: {
            color: { mode: 'fixed' as const, value: '#111111', required: true },
            material: null,
          },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'headrest-embroidery' || surfaceId === 'door-panel-embroidery') {
        return {
          ...result,
          displayName: '无刺绣',
          materialFamilyId: null,
          parameters: { color: null, material: null },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'center-panel-trim') {
        return {
          ...result,
          displayName: '黑色',
          materialFamilyId: null,
          colorCode: '#111111',
          parameters: {
            color: { mode: 'fixed' as const, value: '#111111', required: true },
            material: null,
          },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'rear-wing') {
        return {
          ...result,
          displayName: '无尾翼',
          materialFamilyId: null,
          parameters: { color: null, material: null },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
          ui: { order: 0, control: 'swatch' as const },
        }
      }
      if (surfaceId === 'ip-center-mark') {
        return {
          ...result,
          displayName: '无包覆(黑)',
          materialFamilyId: 'paint',
          colorCode: '#111111',
          parameters: {
            color: { mode: 'fixed' as const, value: '#111111', required: true },
            material: null,
          },
          pricing: {
            ...pricing,
            unitPriceMinor: 0,
            quantity: 1,
            isStandard: true,
            status: 'confirmed' as const,
          },
        }
      }
      return result
    }),
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
