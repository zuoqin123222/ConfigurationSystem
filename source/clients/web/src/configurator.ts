import type {
  CatalogComponent,
  CatalogOption,
  CatalogSurface,
  CatalogV2,
  Customizations,
  PaintCustomization,
  Selections,
} from './types'

function defaultOption(catalog: CatalogV2, surfaceId: string): CatalogOption | undefined {
  const options = catalog.options.filter((item) => item.surfaceId === surfaceId)
  return options.find((item) => item.pricing.isStandard)
    ?? [...options].sort((left, right) => {
      const reviewDelta = Number(left.reviewRequired) - Number(right.reviewRequired)
      if (reviewDelta !== 0) return reviewDelta
      return (left.pricing.unitPriceMinor ?? Number.MAX_SAFE_INTEGER)
        - (right.pricing.unitPriceMinor ?? Number.MAX_SAFE_INTEGER)
    })[0]
}

export function createInitialSelections(catalog: CatalogV2): Selections {
  return Object.fromEntries(
    catalog.selectionOrder.map((surfaceId) => {
      const option = defaultOption(catalog, surfaceId)
      if (!option) {
        throw new Error(`表面 ${surfaceId} 没有可用选项`)
      }
      return [surfaceId, option.optionId]
    }),
  )
}

export function normalizeSelections(
  catalog: CatalogV2,
  selections: Selections,
): Selections {
  return Object.fromEntries(
    catalog.selectionOrder.map((surfaceId) => {
      const selected = catalog.options.find(
        (option) => option.surfaceId === surfaceId && option.optionId === selections[surfaceId],
      )
      const fallback = defaultOption(catalog, surfaceId)
      if (!selected && !fallback) throw new Error(`表面 ${surfaceId} 没有可用选项`)
      return [surfaceId, (selected ?? fallback)!.optionId]
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

export function normalizeCustomizations(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations,
): Customizations {
  const normalized: Customizations = {}
  for (const surfaceId of catalog.selectionOrder) {
    const customization = customizations[surfaceId]
    const option = catalog.options.find((item) => item.optionId === selections[surfaceId])
    if (!customization || !option) continue
    if ('materialVariantId' in customization) {
      const variant = catalog.materialVariants.find(
        (item) => item.variantId === customization.materialVariantId,
      )
      if (variant?.materialFamilyId === option.materialFamilyId) {
        normalized[surfaceId] = { materialVariantId: variant.variantId }
      }
      continue
    }
    if (option.optionId === 'body-cover-custom') normalized[surfaceId] = customization
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
    ...catalog.selectionOrder.map((surfaceId) => `${surfaceId}=${selections[surfaceId]}`),
    ...customizationParts(catalog, customizations),
  ].join('__')
}

export function componentsForCategory(
  catalog: CatalogV2,
  categoryId: string,
): CatalogComponent[] {
  return catalog.components.filter((component) => component.categoryId === categoryId)
}

export function surfacesForComponent(
  catalog: CatalogV2,
  componentId: string,
): CatalogSurface[] {
  return catalog.surfaces.filter((surface) => componentId === 'all' || surface.componentId === componentId)
}

export function optionsForFilters(
  catalog: CatalogV2,
  surfaceId: string,
  materialFamilyId: string,
): CatalogOption[] {
  return catalog.options.filter(
    (option) =>
      option.surfaceId === surfaceId &&
      (materialFamilyId === 'all' || option.materialFamilyId === materialFamilyId),
  )
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
