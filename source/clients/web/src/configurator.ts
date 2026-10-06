import type {
  CatalogComponent,
  CatalogMaterialFamily,
  CatalogMaterialVariant,
  CatalogOption,
  CatalogSurface,
  CatalogV2,
  CatalogCameraId,
  CatalogCategory,
  CatalogVariantSort,
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

export function createDefaultPaintCustomization(option?: CatalogOption): PaintCustomization {
  return {
    colorHex: '#A61D24',
    metallic: 0.35,
    roughness: 0.28,
    clearCoat: 0.8,
    orangePeel: 0.15,
    flakeIntensity: 0.25,
    ...option?.ui?.defaultParameters,
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
        normalized[surfaceId] = createDefaultPaintCustomization(option)
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
  const category = catalog.categories.find((item) => item.categoryId === categoryId)
  if (category?.ui?.navigationMode === 'surfaces-as-components') {
    const componentIds = new Set(
      catalog.components
        .filter((component) => component.categoryId === categoryId)
        .map((component) => component.componentId),
    )
    return catalog.surfaces
      .filter((surface) => componentIds.has(surface.componentId))
      .map((surface, index) => ({ surface, index }))
      .sort((left, right) =>
        (left.surface.ui?.order ?? left.index) - (right.surface.ui?.order ?? right.index),
      )
      .map(({ surface }) => ({
        componentId: surface.surfaceId,
        categoryId,
        displayName: surface.displayName,
        ui: surface.ui,
      }))
  }
  return catalog.components
    .filter((component) => component.categoryId === categoryId)
    .map((component, index) => ({ component, index }))
    .sort((left, right) =>
      (left.component.ui?.order ?? left.index) - (right.component.ui?.order ?? right.index),
    )
    .map(({ component }) => component)
}

export function surfacesForComponent(
  catalog: CatalogV2,
  componentId: string,
): CatalogSurface[] {
  return catalog.surfaces
    .filter((surface) => componentId === 'all' || surface.componentId === componentId)
    .map((surface, index) => ({ surface, index }))
    .sort((left, right) =>
      (left.surface.ui?.order ?? left.index) - (right.surface.ui?.order ?? right.index),
    )
    .map(({ surface }) => surface)
}

export function optionsForSurface(catalog: CatalogV2, surfaceId: string): CatalogOption[] {
  return catalog.options
    .filter((option) => option.surfaceId === surfaceId)
    .map((option, index) => ({ option, index }))
    .sort((left, right) =>
      (left.option.ui?.order ?? left.index) - (right.option.ui?.order ?? right.index),
    )
    .map(({ option }) => option)
}

export function categoriesInUiOrder(catalog: CatalogV2): CatalogCategory[] {
  return catalog.categories
    .map((category, index) => ({ category, index }))
    .sort((left, right) =>
      (left.category.ui?.order ?? left.index) - (right.category.ui?.order ?? right.index),
    )
    .map(({ category }) => category)
}

export function cameraIdForSelection(
  catalog: CatalogV2,
  selection: {
    categoryId?: string
    componentId?: string
    surfaceId?: string
  },
): CatalogCameraId | null {
  const surfaceCamera = catalog.surfaces.find(
    (surface) => surface.surfaceId === selection.surfaceId,
  )?.ui?.cameraId
  const componentCamera = catalog.components.find(
    (component) => component.componentId === selection.componentId,
  )?.ui?.cameraId
  const categoryCamera = catalog.categories.find(
    (category) => category.categoryId === selection.categoryId,
  )?.ui?.cameraId
  return surfaceCamera ?? componentCamera ?? categoryCamera ?? null
}

export function animationIdForSelection(
  catalog: CatalogV2,
  selection: {
    categoryId?: string
    componentId?: string
    surfaceId?: string
  },
): string | null {
  const animationId = catalog.surfaces.find(
    (surface) => surface.surfaceId === selection.surfaceId,
  )?.ui?.animationId
    ?? catalog.components.find(
      (component) => component.componentId === selection.componentId,
    )?.ui?.animationId
    ?? catalog.categories.find(
      (category) => category.categoryId === selection.categoryId,
    )?.ui?.animationId
    ?? null
  return animationId !== null
    && (catalog.animations ?? []).some((animation) => animation.animationId === animationId)
    ? animationId
    : null
}

export interface MaterialOptionGroup {
  materialFamily: CatalogMaterialFamily
  options: CatalogOption[]
}

export function supportsMaterialVariants(option: CatalogOption): boolean {
  return (option.ui?.control === 'material-variant'
    || option.ui?.control === 'material-strip'
    || (!option.ui?.control && option.parameters.color?.mode === 'variant'))
    && option.materialFamilyId !== null
}

export function usesMaterialStrip(option: CatalogOption): boolean {
  return option.ui?.control === 'material-strip'
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

function colorMetrics(color: string) {
  const match = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(color)
  if (!match) return null
  const [red, green, blue] = match.slice(1).map((value) => Number.parseInt(value, 16) / 255)
  const max = Math.max(red, green, blue)
  const min = Math.min(red, green, blue)
  const delta = max - min
  const brightness = 0.2126 * red + 0.7152 * green + 0.0722 * blue
  let hue = 0
  if (delta !== 0) {
    if (max === red) hue = 60 * (((green - blue) / delta) % 6)
    else if (max === green) hue = 60 * ((blue - red) / delta + 2)
    else hue = 60 * ((red - green) / delta + 4)
  }
  const normalizedHue = hue < 0 ? hue + 360 : hue
  return {
    hue: normalizedHue >= 345 ? normalizedHue - 360 : normalizedHue,
    brightness,
    chroma: delta,
  }
}

export function sortMaterialVariants(
  variants: CatalogMaterialVariant[],
  variantSort: CatalogVariantSort | undefined,
): CatalogMaterialVariant[] {
  if (variantSort !== 'achromatic-then-rainbow') return variants
  return variants
    .map((variant, index) => ({
      variant,
      index,
      metrics: colorMetrics(variant.ui?.sortColorHex ?? ''),
    }))
    .sort((left, right) => {
      if (!left.metrics || !right.metrics) return left.index - right.index
      // 直接比较 RGB 通道差，避免近白色因 HSL 分母过小被误判为高饱和色。
      const leftAchromatic = left.metrics.chroma <= 0.08
      const rightAchromatic = right.metrics.chroma <= 0.08
      if (leftAchromatic !== rightAchromatic) return leftAchromatic ? -1 : 1
      const difference = leftAchromatic
        ? left.metrics.brightness - right.metrics.brightness
        : left.metrics.hue - right.metrics.hue
      return difference || left.index - right.index
    })
    .map(({ variant }) => variant)
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
