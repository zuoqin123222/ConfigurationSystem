export const PART_ORDER = ['paint', 'wheel', 'interior', 'frame'] as const
export const RENDER_VIEW_IDS = ['front', 'front-left', 'side', 'rear-right'] as const

export type PartId = (typeof PART_ORDER)[number]
export type RenderViewId = (typeof RENDER_VIEW_IDS)[number]

export interface CatalogOption {
  optionId: string
  zhName: string
  priceDeltaMinor: number
  previewImageUrl: string
  uePrimaryAssetId: string
}

export interface CatalogPart {
  partId: PartId
  zhName: string
  displayOrder: number
  options: CatalogOption[]
}

export interface CatalogTemplate {
  templateId: string
  zhName: string
  selections: Selections
}

export interface RenderView {
  renderViewId: RenderViewId
  zhName: string
}

export interface Catalog {
  schemaVersion: string
  catalogVersion: string
  currency: 'CNY'
  vehicle: {
    vehicleId: string
    zhName: string
    basePriceMinor: number
  }
  parts: CatalogPart[]
  templates: CatalogTemplate[]
  interactionCameras: string[]
  renderViews: RenderView[]
}

export type Selections = Record<PartId, string>

export interface Health {
  status: 'ok'
  catalogVersion: string
  publicationVersion: string
}

export interface ResolveRenderRequest {
  catalogVersion: string
  publicationVersion: string
  vehicleId: string
  selections: Selections
  renderViewId: RenderViewId
}

export interface ResolveRenderResponse {
  configurationKey: string
  renderViewId: RenderViewId
  imageUrl: string
}
