export const RENDER_VIEW_IDS = ['front', 'front-left', 'side', 'rear-right'] as const

export type RenderViewId = (typeof RENDER_VIEW_IDS)[number]
export type Selections = Record<string, string>

export interface CatalogNode {
  displayName: string
}

export interface CatalogCategory extends CatalogNode {
  categoryId: string
}

export interface CatalogComponent extends CatalogNode {
  componentId: string
  categoryId: string
}

export interface CatalogSurface extends CatalogNode {
  surfaceId: string
  componentId: string
  required: boolean
}

export interface CatalogMaterialFamily extends CatalogNode {
  materialFamilyId: string
}

export interface CatalogPricing {
  unitPriceMinor: null
  quantity: number | null
  pricingUnit: string
  isStandard: boolean
  status: 'unconfirmed'
  quotable: false
}

export interface CatalogOption extends CatalogNode {
  optionId: string
  surfaceId: string
  materialFamilyId: string
  renderRelevant: boolean
  colorCode: string | null
  finish: string | null
  pricing: CatalogPricing
}

export interface CatalogV2 {
  schemaVersion: '2.0.0'
  catalogVersion: string
  lifecycle: 'draft'
  currency: 'CNY'
  vehicle: {
    vehicleId: string
    displayName: string
    basePriceMinor: null
    priceStatus: 'unconfirmed'
    quotable: false
  }
  selectionOrder: string[]
  categories: CatalogCategory[]
  components: CatalogComponent[]
  surfaces: CatalogSurface[]
  materialFamilies: CatalogMaterialFamily[]
  options: CatalogOption[]
}

export interface PriceResultV2 {
  totalPriceMinor: null
  quoteAllowed: false
  blockingReasons: string[]
}

export interface ConfigurationV2 {
  schemaVersion: '2.0.0'
  catalogVersion: string
  vehicleId: string
  configurationId: string
  renderKey: string
  selections: Selections
  revision: number
  priceResult: PriceResultV2
  createdAt: string
  updatedAt: string
}

export interface SaveConfigurationRequest {
  catalogVersion: string
  vehicleId: string
  selections: Selections
  configurationId?: string
  revision?: number
}

export interface ResolveRenderV2Response {
  schemaVersion: '2.0.0'
  catalogVersion: string
  vehicleId: string
  configurationId: string
  renderKey: string
  renderViewId?: string
  imageUrl?: string
}

export interface LegacyCatalogOption {
  optionId: string
}

export interface LegacyCatalog {
  catalogVersion: string
  vehicle: { vehicleId: string }
  parts: Array<{ partId: string; options: LegacyCatalogOption[] }>
  renderViews: Array<{ renderViewId: RenderViewId; zhName: string }>
}

export interface LegacyRender {
  configurationKey: string
  renderViewId: RenderViewId
  imageUrl: string
}
