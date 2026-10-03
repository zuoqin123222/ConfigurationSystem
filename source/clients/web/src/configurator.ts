import type {
  CatalogComponent,
  CatalogMaterialFamily,
  CatalogMaterialVariant,
  CatalogOption,
  CatalogSurface,
  CatalogV2,
  Customizations,
  PaintCustomization,
  Selections,
} from './types'

function defaultOption(catalog: CatalogV2, surfaceId: string): CatalogOption | undefined {
  const optionId = catalog.defaultSelections[surfaceId]
  return optionId
    ? catalog.options.find(
        (item) => item.surfaceId === surfaceId && item.optionId === optionId,
      )
    : undefined
}

export function createInitialSelections(catalog: CatalogV2): Selections {
  return Object.fromEntries(
    catalog.selectionOrder.flatMap((surfaceId) => {
      const option = defaultOption(catalog, surfaceId)
      return option ? [[surfaceId, option.optionId]] : []
    }),
  )
}

export function normalizeSelections(
  catalog: CatalogV2,
  selections: Selections,
): Selections {
  return Object.fromEntries(
    catalog.selectionOrder.flatMap((surfaceId) => {
      const selected = catalog.options.find(
        (option) => option.surfaceId === surfaceId && option.optionId === selections[surfaceId],
      )
      const fallback = defaultOption(catalog, surfaceId)
      const option = selected ?? fallback
      return option ? [[surfaceId, option.optionId]] : []
    }),
  )
}

const PAINT_KEYS: Array<keyof PaintCustomization> = [
  'colorHex',
  'metallic',
  'roughness',
  'clearCoat',
  'orangePeel',
  'flakeIntensity',
]

export function createDefaultPaintCustomization(): PaintCustomization {
  return {
    colorHex: '#A61D24',
    metallic: 0.35,
    roughness: 0.28,
    clearCoat: 0.8,
    orangePeel: 0.15,
    flakeIntensity: 0.25,
  }
}

export function normalizeCustomizations(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations,
): Customizations {
  const normalized: Customizations = {}
  for (const surfaceId of catalog.selectionOrder) {
    const customization = customizations[surfaceId]
    const option = catalog.options.find((item) => item.optionId === selections[surfaceId])
    if (!option) continue
    if (!customization) {
      if (option.parameters.color?.mode === 'custom') {
        normalized[surfaceId] = createDefaultPaintCustomization()
      }
      continue
    }
    if ('materialVariantId' in customization) {
      const variant = catalog.materialVariants.find(
        (item) => item.variantId === customization.materialVariantId,
      )
      if (
        supportsMaterialVariants(option)
        && variant?.materialFamilyId === option.materialFamilyId
      ) {
        normalized[surfaceId] = { materialVariantId: variant.variantId }
      }
      continue
    }
    if (option.parameters.color?.mode === 'custom') normalized[surfaceId] = customization
  }
  return normalized
}

function customizationParts(
  catalog: CatalogV2,
  customizations: Customizations,
  includedSurfaces?: ReadonlySet<string>,
): string[] {
  return catalog.selectionOrder.flatMap((surfaceId) => {
    if (includedSurfaces && !includedSurfaces.has(surfaceId)) return []
    const customization = customizations[surfaceId]
    if (!customization) return []
    if ('materialVariantId' in customization) {
      return [`${surfaceId}.materialVariantId=${customization.materialVariantId}`]
    }
    return PAINT_KEYS.map((key) => `${surfaceId}.${key}=${customization[key]}`)
  })
}

export function createCanonicalKey(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations = {},
): string {
  return [
    ...catalog.selectionOrder.flatMap((surfaceId) =>
      selections[surfaceId] ? [`${surfaceId}=${selections[surfaceId]}`] : [],
    ),
    ...customizationParts(catalog, customizations),
  ].join('__')
}

export function componentsForCategory(
  catalog: CatalogV2,
  categoryId: string,
): CatalogComponent[] {
  const order = [
    'car-paint',
    'chassis',
    'wheel',
    'caliper',
    'steering-wheel',
    'instrument-panel',
    'a-pillar',
    'seat',
    'door-trim',
    'storage-box',
    'center-console',
    'roof',
    'underbody',
    'personalization',
  ]
  return catalog.components
    .filter((component) => component.categoryId === categoryId)
    .sort((left, right) =>
      order.indexOf(left.componentId) - order.indexOf(right.componentId),
    )
}

export function surfacesForComponent(
  catalog: CatalogV2,
  componentId: string,
): CatalogSurface[] {
  return catalog.surfaces.filter((surface) => componentId === 'all' || surface.componentId === componentId)
}

export interface MaterialOptionGroup {
  materialFamily: CatalogMaterialFamily
  options: CatalogOption[]
}

export function supportsMaterialVariants(option: CatalogOption): boolean {
  return option.parameters.color?.mode === 'variant'
    && option.materialFamilyId !== null
}

export function materialVariantsForOption(
  catalog: CatalogV2,
  option: CatalogOption,
): CatalogMaterialVariant[] {
  if (!supportsMaterialVariants(option) || option.materialFamilyId === null) return []
  return catalog.materialVariants.filter(
    (variant) => variant.materialFamilyId === option.materialFamilyId,
  )
}

export function materialGroupsForSurface(
  catalog: CatalogV2,
  surfaceId: string,
): MaterialOptionGroup[] {
  const options = catalog.options.filter((option) => option.surfaceId === surfaceId)
  return catalog.materialFamilies.flatMap((materialFamily) => {
    const familyOptions = options.filter(
      (option) => option.materialFamilyId === materialFamily.materialFamilyId,
    )
    return familyOptions.length > 0
      ? [{ materialFamily, options: familyOptions }]
      : []
  })
}

export function renderRelevantSelections(
  catalog: CatalogV2,
  selections: Selections,
): Selections {
  return Object.fromEntries(
    catalog.selectionOrder.flatMap((surfaceId) => {
      const option = catalog.options.find((item) => item.optionId === selections[surfaceId])
      return option?.renderRelevant ? [[surfaceId, option.optionId]] : []
    }),
  )
}

export function createRenderCanonicalKey(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations,
): string {
  const relevantSelections = renderRelevantSelections(catalog, selections)
  const relevantSurfaces = new Set(Object.keys(relevantSelections))
  return [
    ...Object.entries(relevantSelections).map(([surfaceId, optionId]) => `${surfaceId}=${optionId}`),
    ...customizationParts(catalog, customizations, relevantSurfaces),
  ].join('__')
}
