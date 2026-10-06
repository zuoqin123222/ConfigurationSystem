export const RENDER_VIEW_IDS = ['front', 'front-left', 'side', 'rear-right'] as const

export type RenderViewId = (typeof RENDER_VIEW_IDS)[number]
export type Selections = Record<string, string>
export interface MaterialCustomization {
  materialVariantId: string
}
export interface PaintCustomization {
  colorHex: string
  metallic: number
  roughness: number
  clearCoat: number
  orangePeel: number
  flakeIntensity: number
}
export type Customization = MaterialCustomization | PaintCustomization
export type Customizations = Record<string, Customization>

export interface CatalogNode {
  displayName: string
}

export type CatalogCameraId = string | number
export type CatalogNavigationMode = 'tabs' | 'list' | 'none' | 'surfaces-as-components'
export type CatalogLayout = 'single' | 'stack' | 'grid'
export type CatalogVariantSort = 'achromatic-then-rainbow'
export type CatalogAnimationLoopMode = 'none' | 'forward' | 'ping-pong'
export type CatalogAnimationCloseMode = 'reverse' | 'reset-to-start' | 'stop'

export interface CatalogAnimation extends CatalogNode {
  animationId: string
  frameRate: number
  startFrame: number
  endFrame: number
  loopMode: CatalogAnimationLoopMode
  closeMode: CatalogAnimationCloseMode
}

export interface CatalogNodeUi {
  order?: number
  iconUrl?: string | null
  cameraId?: CatalogCameraId | null
  navigationMode?: CatalogNavigationMode
  layout?: CatalogLayout
  animationId?: string | null
}

export interface CatalogInteractionCamera extends CatalogNode {
  cameraId: CatalogCameraId
  legacyIndex?: 0 | 1 | 2 | 3 | 4 | 5 | null
  zone: string
  order: number
  iconUrl: string
}

export interface CatalogCategory extends CatalogNode {
  categoryId: string
  regionId: string
  ui?: CatalogNodeUi
}

export interface CatalogComponent extends CatalogNode {
  componentId: string
  categoryId: string
  ui?: CatalogNodeUi
}

export interface CatalogSurface extends CatalogNode {
  surfaceId: string
  componentId: string
  required: boolean
  reviewRequired: boolean
  ui?: CatalogNodeUi
}

export interface CatalogMaterialFamily extends CatalogNode {
  materialFamilyId: string
  ui?: {
    variantSort?: CatalogVariantSort
  }
}

export interface CatalogPricing {
  unitPriceMinor: number | null
  quantity: number | null
  pricingUnit: string
  isStandard: boolean
  status: 'confirmed' | 'unconfirmed'
  quotable: false
}

export interface CatalogOption extends CatalogNode {
  optionId: string
  surfaceId: string
  materialFamilyId: string | null
  renderRelevant: boolean
  colorCode: string | null
  finish: string | null
  parameters: {
    color: {
      mode: 'fixed' | 'custom' | 'choice' | 'variant'
      value: string | null
      required: boolean
    } | null
    material: {
      materialFamilyId: string
      variantId: string | null
    } | null
  }
  pricing: CatalogPricing
  reviewRequired: boolean
  thumbnailUrl: string | null
  ui?: {
    order?: number
    iconUrl?: string | null
    control?: 'swatch' | 'thumbnail' | 'color-picker' | 'material-variant' | 'material-strip'
    defaultParameters?: Partial<PaintCustomization>
  }
}

export interface CatalogMaterialVariant extends CatalogNode {
  variantId: string
  materialFamilyId: string
  colorCode: string | null
  ui?: {
    sortColorHex?: string
  }
  thumbnailUrl: string
  reviewRequired: boolean
}

export interface CatalogV2 {
  schemaVersion: '2.0.0'
  catalogVersion: string
  lifecycle: 'draft'
  currency: 'CNY'
  vehicle: {
    vehicleId: string
    displayName: string
    basePriceMinor: number
    priceStatus: 'confirmed'
    quotable: false
  }
  selectionOrder: string[]
  defaultSelections: Selections
  interactionCameras?: CatalogInteractionCamera[]
  skeletalMeshPath: string
  sequencePath: string
  animations: CatalogAnimation[]
  regions: Array<CatalogNode & { regionId: string }>
  categories: CatalogCategory[]
  components: CatalogComponent[]
  surfaces: CatalogSurface[]
  materialFamilies: CatalogMaterialFamily[]
  materialVariants: CatalogMaterialVariant[]
  assetManifest: string
  options: CatalogOption[]
}

export interface PriceResultV2 {
  basePriceMinor: number
  totalPriceMinor: number
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
  customizations: Customizations
  revision: number
  priceResult: PriceResultV2
  createdAt: string
  updatedAt: string
}

export interface SaveConfigurationRequest {
  catalogVersion: string
  vehicleId: string
  selections: Selections
  customizations: Customizations
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
