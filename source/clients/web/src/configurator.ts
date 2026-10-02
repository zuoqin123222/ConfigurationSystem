import type {
  CatalogComponent,
  CatalogOption,
  CatalogSurface,
  CatalogV2,
  Selections,
} from './types'

export function createInitialSelections(catalog: CatalogV2): Selections {
  return Object.fromEntries(
    catalog.selectionOrder.map((surfaceId) => {
      const option = catalog.options.find((item) => item.surfaceId === surfaceId)
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
      const fallback = catalog.options.find((option) => option.surfaceId === surfaceId)
      if (!selected && !fallback) throw new Error(`表面 ${surfaceId} 没有可用选项`)
      return [surfaceId, (selected ?? fallback)!.optionId]
    }),
  )
}

export function createCanonicalKey(catalog: CatalogV2, selections: Selections): string {
  return catalog.selectionOrder.map((surfaceId) => `${surfaceId}=${selections[surfaceId]}`).join('__')
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
