import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
  type ReactNode,
  type SyntheticEvent,
} from 'react'
import {
  ApiError,
  fetchCatalog,
  fetchConfiguration,
  fetchInitialData,
  resolveLegacyProxy,
  resolveRender,
} from './api'
import {
  animationIdForSelection,
  cameraIdForSelection,
  categoriesInUiOrder,
  componentsForCategory,
  createDefaultPaintCustomization,
  createInitialSelections,
  createRenderCanonicalKey,
  materialVariantsForOption,
  normalizeCustomizations,
  normalizeSelections,
  optionIsAvailable,
  optionsForSurface,
  sortMaterialVariants,
  supportsMaterialVariants,
  surfacesForComponent,
  usesMaterialStrip,
} from './configurator'
import type {
  CatalogV2,
  Customizations,
  LegacyCatalog,
  LegacyRender,
  PaintCustomization,
  RenderViewId,
  Selections,
} from './types'
import {
  bundledCatalog,
  resolveStaticAssetUrl,
  usesBundledCatalog,
} from './bundledCatalog'
import {
  createPortableConfiguration,
  createPortableConfigurationQr,
  parsePortableConfiguration,
} from './portableConfiguration'
import {
  applyUeConfiguration,
  CONFIGURATOR_CATEGORY_EVENT,
  CONFIGURATOR_HEADER_ACTION_EVENT,
  CONFIGURATOR_HEADER_STATE_EVENT,
  getUeConfiguratorHeaderState,
  getUeBridge,
  focusUeAnimation,
  isUeConfiguratorHeaderState,
  syncUeConfiguratorCategory,
  syncUeConfiguratorHeaderState,
  setUeCameraId,
  triggerUeConfiguratorHeaderAction,
  type UeConfiguratorCategory,
  type UeConfiguratorHeaderState,
} from './ueBridge'
import ExperienceControls from './ExperienceControls'
import InlineColorPicker from './InlineColorPicker'
import MaterialColorStrip from './MaterialColorStrip'
import {
  BLACK_REFERENCE_SURFACES,
  INTERIOR_PART_IMAGES,
} from './interiorPartImages'

const CACHE_KEY = 'automotive-v2-configurator'
const PORTABLE_CACHE_KEY = `${CACHE_KEY}-portable`
const DEFAULT_IMAGE_URL = '/sc01/option-icons/default.svg'
const FIXED_OPTION_SWATCHES: Record<string, string> = {
  'body-cover-red': '#FF3B3B',
  'body-cover-silver': 'linear-gradient(135deg, #F5F6F7 0%, #C5C9CC 48%, #8F969C 100%)',
}
const UPHOLSTERY_MATERIAL_FAMILIES = new Set([
  'ultrasuede',
  'alcantara',
  'microfiber',
  'leather',
  'woven-wool',
  'woven-fabric',
])
export const STANDALONE_LAYOUT = {
  headerHeight: 76,
  panelWidth: 480,
  stageMargin: 18,
  stageRadius: 24,
} as const
const SRGB_TO_LINEAR_TABLE = Array.from({ length: 256 }, (_, index) => {
  const value = index / 255
  return value <= 0.04045
    ? value / 12.92
    : Math.pow((value + 0.055) / 1.055, 2.4)
}).join(' ')
const SEAT_BACKPLATE_FINISH_PRESETS = {
  gloss: {
    label: '亮面',
    roughness: 0.18,
    clearCoat: 0.8,
  },
  matte: {
    label: '哑光',
    roughness: 0.72,
    clearCoat: 0.05,
  },
} as const

type SeatBackplateFinish = keyof typeof SEAT_BACKPLATE_FINISH_PRESETS
type WorkflowStepId = 'preset' | 'summary' | string
type TransferMode = 'import' | 'share'

interface WorkflowStep {
  id: WorkflowStepId
  label: string
  categoryId?: string
}

function workflowSteps(categories: CatalogV2['categories']): WorkflowStep[] {
  return [
    { id: 'preset', label: '预设' },
    ...categories.map((category) => ({
      id: category.categoryId,
      label: category.displayName,
      categoryId: category.categoryId,
    })),
    { id: 'summary', label: '总览' },
  ]
}

export function isEmbeddedView(search = window.location.search): boolean {
  return new URLSearchParams(search).get('view') === 'embedded'
}

export function isOfflineEmbeddedRuntime(): boolean {
  return usesBundledCatalog()
}

function readSavedPortableBaseline(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations,
): string {
  const value = localStorage.getItem(PORTABLE_CACHE_KEY)
  if (!value) return ''
  try {
    const imported = parsePortableConfiguration(value, catalog)
    const importedSelections = normalizeSelections(catalog, imported.selections)
    const importedCustomizations = normalizeCustomizations(
      catalog,
      importedSelections,
      imported.customizations,
    )
    const normalizedSaved = createPortableConfiguration(
      catalog,
      importedSelections,
      importedCustomizations,
    )
    const current = createPortableConfiguration(catalog, selections, customizations)
    return normalizedSaved === current ? current : ''
  } catch {
    localStorage.removeItem(PORTABLE_CACHE_KEY)
    return ''
  }
}

export type AppView = 'default' | 'embedded' | 'controls' | 'header'

export function getAppView(search = window.location.search): AppView {
  const view = new URLSearchParams(search).get('view')
  if (view === 'embedded' || view === 'controls' || view === 'header') return view
  return 'default'
}

function optionPrice(option: CatalogV2['options'][number]): string {
  if (option.pricing.isStandard || option.pricing.unitPriceMinor === 0) return '免费'
  if (option.pricing.unitPriceMinor !== null) {
    const totalPriceMinor = option.pricing.unitPriceMinor * (option.pricing.quantity ?? 1)
    return `¥${(totalPriceMinor / 100).toLocaleString('zh-CN')}`
  }
  return '价格待确认'
}

function optionSwatch(option: CatalogV2['options'][number]): string {
  if (option.parameters.color?.mode === 'custom') {
    return 'conic-gradient(#e84b4b, #e8ce4b, #55bb6a, #4b8ee8, #9855c7, #e84b4b)'
  }
  const fixedSwatch = FIXED_OPTION_SWATCHES[option.optionId]
  if (fixedSwatch) return fixedSwatch
  const color = option.parameters.color?.value ?? option.colorCode
  if (color && /^#[0-9a-f]{6}$/i.test(color)) return color
  if (option.pricing.isStandard) return '#171817'
  return '#8b8d88'
}

function useDefaultImage(event: SyntheticEvent<HTMLImageElement>) {
  const image = event.currentTarget
  if (!image.src.includes(DEFAULT_IMAGE_URL.slice(1))) {
    image.src = versionStaticAssetUrl(DEFAULT_IMAGE_URL) ?? DEFAULT_IMAGE_URL
  }
}

function customEditorLabel(displayName: string): string {
  return displayName.endsWith('颜色') ? displayName : `${displayName}颜色`
}

function resolveSeatBackplateFinish(
  customization: PaintCustomization,
): SeatBackplateFinish {
  const glossDelta = Math.abs(customization.roughness - SEAT_BACKPLATE_FINISH_PRESETS.gloss.roughness)
    + Math.abs(customization.clearCoat - SEAT_BACKPLATE_FINISH_PRESETS.gloss.clearCoat)
  const matteDelta = Math.abs(customization.roughness - SEAT_BACKPLATE_FINISH_PRESETS.matte.roughness)
    + Math.abs(customization.clearCoat - SEAT_BACKPLATE_FINISH_PRESETS.matte.clearCoat)
  return glossDelta <= matteDelta ? 'gloss' : 'matte'
}

export function versionStaticAssetUrl(
  url: string | undefined,
  search = window.location.search,
): string | undefined {
  return resolveStaticAssetUrl(url, search)
}

function Showroom() {
  return (
    <svg className="showroom" viewBox="0 0 1200 760" aria-hidden="true">
      <defs>
        <linearGradient id="wall" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#ffffff" />
          <stop offset="1" stopColor="#ececea" />
        </linearGradient>
        <radialGradient id="floor">
          <stop offset="0" stopColor="#b7b7b4" stopOpacity=".32" />
          <stop offset="1" stopColor="#ffffff" stopOpacity="0" />
        </radialGradient>
      </defs>
      <rect width="1200" height="760" fill="url(#wall)" />
      <path d="M0 0h1200v70L0 250z" fill="#f7f7f5" />
      <path d="M0 760V455L1200 70v690z" fill="#efefed" />
      <ellipse cx="575" cy="605" rx="530" ry="145" fill="url(#floor)" />
    </svg>
  )
}

interface CachedDraft {
  catalog: CatalogV2
  selections: Selections
  customizations?: Customizations
}

function readCachedDraft(): CachedDraft | null {
  try {
    const value = localStorage.getItem(CACHE_KEY)
    return value ? JSON.parse(value) as CachedDraft : null
  } catch {
    return null
  }
}

export default function App() {
  const view = getAppView()
  const shouldCorrectUeColor = (view === 'embedded' || view === 'header')
    && getUeBridge(true) !== null
  let content
  if (view === 'controls') {
    content = <ExperienceControls ueEnabled />
  } else if (view === 'header') {
    content = <ConfiguratorHeader />
  } else {
    content = <ConfiguratorApp embedded={view === 'embedded'} />
  }
  return shouldCorrectUeColor
    ? <UeColorCorrected>{content}</UeColorCorrected>
    : content
}

function UeColorCorrected({ children }: { children: ReactNode }) {
  return (
    <>
      <svg className="ue-color-filter" aria-hidden="true">
        <defs>
          <filter id="ue-srgb-to-linear" colorInterpolationFilters="sRGB">
            <feComponentTransfer>
              <feFuncR type="table" tableValues={SRGB_TO_LINEAR_TABLE} />
              <feFuncG type="table" tableValues={SRGB_TO_LINEAR_TABLE} />
              <feFuncB type="table" tableValues={SRGB_TO_LINEAR_TABLE} />
            </feComponentTransfer>
          </filter>
        </defs>
      </svg>
      <div className="ue-color-corrected">{children}</div>
    </>
  )
}

function ConfiguratorHeader() {
  const [categories, setCategories] = useState<CatalogV2['categories']>([])
  const [headerState, setHeaderState] = useState<UeConfiguratorHeaderState>({
    categoryId: 'preset',
    referenceTotalMinor: 22980000,
    syncState: 'idle',
    syncMessage: '',
    dirty: true,
    online: true,
  })

  useEffect(() => {
    const controller = new AbortController()
    document.documentElement.classList.add('header-document')
    document.body.classList.add('header-document')
    const handleHeaderState = (event: Event) => {
      const nextState = (event as CustomEvent<unknown>).detail
      if (isUeConfiguratorHeaderState(nextState)) {
        setHeaderState(nextState)
      }
    }
    window.addEventListener(CONFIGURATOR_HEADER_STATE_EVENT, handleHeaderState)
    void getUeConfiguratorHeaderState(getUeBridge(true)).then((state) => {
      if (state) {
        setHeaderState(state)
      }
    })
    const catalogRequest = isOfflineEmbeddedRuntime()
      ? Promise.resolve(bundledCatalog)
      : fetchCatalog(controller.signal)
    void catalogRequest.then((catalog) => {
      const nextCategories = categoriesInUiOrder(catalog)
      setCategories(nextCategories)
      setHeaderState((current) => (
        current.categoryId === 'preset'
        || current.categoryId === 'summary'
        || nextCategories.some((category) => category.categoryId === current.categoryId)
      )
        ? current
        : { ...current, categoryId: nextCategories[0]?.categoryId ?? current.categoryId })
    }).catch(() => {
      // 顶栏状态与动作仍可在目录暂不可用时工作。
    })
    return () => {
      controller.abort()
      window.removeEventListener(CONFIGURATOR_HEADER_STATE_EVENT, handleHeaderState)
      document.documentElement.classList.remove('header-document')
      document.body.classList.remove('header-document')
    }
  }, [])

  const triggerAction = (action: 'save') => {
    void triggerUeConfiguratorHeaderAction(getUeBridge(true), action)
  }

  const selectCategory = (stepId: string) => {
    if (
      stepId !== 'preset'
      && stepId !== 'summary'
      && !categories.some((category) => category.categoryId === stepId)
    ) return
    setHeaderState((current) => ({ ...current, categoryId: stepId }))
    void syncUeConfiguratorCategory(getUeBridge(true), stepId)
  }

  return (
    <ConfiguratorTopBar
      categories={categories}
      activeStepId={headerState.categoryId}
      headerState={headerState}
      onAction={triggerAction}
      onSelectStep={selectCategory}
    />
  )
}

function ConfiguratorTopBar({
  categories,
  activeStepId,
  headerState,
  onAction,
  onSelectStep,
  standalone = false,
}: {
  categories: CatalogV2['categories']
  activeStepId: WorkflowStepId
  headerState: UeConfiguratorHeaderState
  onAction: (action: 'save') => void
  onSelectStep: (stepId: WorkflowStepId) => void
  standalone?: boolean
}) {
  const steps = workflowSteps(categories)
  const activeStageIndex = Math.max(
    0,
    steps.findIndex((step) => step.id === activeStepId),
  )
  return (
    <header className={`configurator-header ${standalone ? 'standalone-header' : ''}`}>
      <h1 className="brand-title" aria-label="SC01 定制">
        <strong>SC</strong><em>01</em><small>定制</small>
      </h1>
      <nav
        aria-label="选配阶段"
        style={{ gridTemplateColumns: `repeat(${Math.max(steps.length, 1)}, minmax(0, 1fr))` }}
      >
        {steps.length > 0 && <span
          className="stage-indicator"
          aria-hidden="true"
          style={{
            width: `${100 / steps.length}%`,
            transform: `translateX(${activeStageIndex * 100}%)`,
          }}
        />}
        {steps.map((step, index) => (
          <div className="header-stage" key={step.id}>
            <button
              className={activeStepId === step.id ? 'active' : ''}
              aria-label={step.label}
              aria-current={activeStepId === step.id ? 'step' : undefined}
              onClick={() => onSelectStep(step.id)}
            >
              <span>{String(index + 1).padStart(2, '0')}</span>
              {step.label}
            </button>
          </div>
        ))}
      </nav>
      <div className="header-actions">
        <span className={`header-sync ${headerState.syncState}`}>
          {headerState.syncMessage || (headerState.dirty ? '未同步更改' : '已同步')}
        </span>
        <button
          className="header-save"
          onClick={() => onAction('save')}
          disabled={headerState.syncState === 'saving' || !headerState.dirty}
        >
          {headerState.syncState === 'saving' ? '保存中…' : '存草稿'}
        </button>
      </div>
    </header>
  )
}

function ConfiguratorApp({ embedded }: { embedded: boolean }) {
  const [catalog, setCatalog] = useState<CatalogV2 | null>(null)
  const [legacyCatalog, setLegacyCatalog] = useState<LegacyCatalog | null>(null)
  const [selections, setSelections] = useState<Selections | null>(null)
  const [customizations, setCustomizations] = useState<Customizations>({})
  const [savedPortableValue, setSavedPortableValue] = useState('')
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [offlineDraft, setOfflineDraft] = useState(false)
  const [reloadKey, setReloadKey] = useState(0)
  const [online, setOnline] = useState(() => navigator.onLine)

  const reload = useCallback(() => setReloadKey((value) => value + 1), [])

  useEffect(() => {
    const handleOnline = () => setOnline(true)
    const handleOffline = () => setOnline(false)
    window.addEventListener('online', handleOnline)
    window.addEventListener('offline', handleOffline)
    return () => {
      window.removeEventListener('online', handleOnline)
      window.removeEventListener('offline', handleOffline)
    }
  }, [])

  useEffect(() => {
    const controller = new AbortController()
    setLoading(true)
    setError('')
    setOfflineDraft(false)
    const initialData = embedded
      ? (
          isOfflineEmbeddedRuntime()
            ? Promise.resolve(bundledCatalog)
            : fetchCatalog(controller.signal)
        ).then((nextCatalog) => ({
          catalog: nextCatalog,
          legacyCatalog: null,
        }))
      : fetchInitialData(controller.signal)
    initialData
      .then(async ({ catalog: nextCatalog, legacyCatalog: nextLegacyCatalog }) => {
        if (controller.signal.aborted) return
        const search = new URLSearchParams(window.location.search)
        const portableValue = search.get('config')
        const configurationId = search.get('configuration')
        let initialSelections = createInitialSelections(nextCatalog)
        let initialCustomizations = normalizeCustomizations(nextCatalog, initialSelections, {})
        let initialPortableValue = ''
        if (portableValue) {
          const imported = parsePortableConfiguration(portableValue, nextCatalog)
          initialSelections = normalizeSelections(nextCatalog, imported.selections)
          initialCustomizations = normalizeCustomizations(
            nextCatalog,
            initialSelections,
            imported.customizations,
          )
          initialPortableValue = createPortableConfiguration(
            nextCatalog,
            initialSelections,
            initialCustomizations,
          )
        } else if (!embedded && configurationId) {
          const loadedConfiguration = await fetchConfiguration(configurationId, controller.signal)
          initialSelections = normalizeSelections(nextCatalog, loadedConfiguration.selections)
          initialCustomizations = normalizeCustomizations(
            nextCatalog,
            initialSelections,
            loadedConfiguration.customizations,
          )
        } else {
          const cached = readCachedDraft()
          if (cached?.catalog.catalogVersion === nextCatalog.catalogVersion) {
            initialSelections = normalizeSelections(nextCatalog, cached.selections)
            initialCustomizations = normalizeCustomizations(
              nextCatalog,
              initialSelections,
              cached.customizations ?? {},
            )
          }
        }
        if (!initialPortableValue) {
          initialPortableValue = readSavedPortableBaseline(
            nextCatalog,
            initialSelections,
            initialCustomizations,
          )
        }
        if (controller.signal.aborted) return
        setCatalog(nextCatalog)
        setLegacyCatalog(nextLegacyCatalog)
        setSelections(initialSelections)
        setCustomizations(initialCustomizations)
        setSavedPortableValue(initialPortableValue)
      })
      .catch((reason: unknown) => {
        if (controller.signal.aborted) return
        const cached = readCachedDraft()
        if (!navigator.onLine && cached) {
          setCatalog(cached.catalog)
          const cachedSelections = normalizeSelections(cached.catalog, cached.selections)
          setSelections(cachedSelections)
          const cachedCustomizations = normalizeCustomizations(
            cached.catalog,
            cachedSelections,
            cached.customizations ?? {},
          )
          setCustomizations(cachedCustomizations)
          setSavedPortableValue(readSavedPortableBaseline(
            cached.catalog,
            cachedSelections,
            cachedCustomizations,
          ))
          setOfflineDraft(true)
        } else {
          setError(reason instanceof Error ? reason.message : '目录加载失败')
        }
      })
      .finally(() => {
        if (!controller.signal.aborted) setLoading(false)
      })
    return () => controller.abort()
  }, [embedded, reloadKey])

  useEffect(() => {
    if (catalog && selections) {
      localStorage.setItem(CACHE_KEY, JSON.stringify({ catalog, selections, customizations }))
    }
  }, [catalog, customizations, selections])

  if (loading) {
    return (
      <StatusScreen
        title="正在准备您的专属座驾"
        detail="正在加载 SC01 草案目录…"
        embedded={embedded}
      />
    )
  }
  if (error || !catalog || !selections) {
    return (
      <StatusScreen
        title="暂时无法进入展厅"
        detail={error || '目录数据不可用'}
        action={reload}
        embedded={embedded}
      />
    )
  }

  return (
    <Configurator
      catalog={catalog}
      legacyCatalog={legacyCatalog}
      selections={selections}
      setSelections={setSelections}
      customizations={customizations}
      setCustomizations={setCustomizations}
      savedPortableValue={savedPortableValue}
      setSavedPortableValue={setSavedPortableValue}
      online={online}
      offlineDraft={offlineDraft}
      embedded={embedded}
    />
  )
}

interface ConfiguratorProps {
  catalog: CatalogV2
  legacyCatalog: LegacyCatalog | null
  selections: Selections
  setSelections: (value: Selections) => void
  customizations: Customizations
  setCustomizations: (value: Customizations) => void
  savedPortableValue: string
  setSavedPortableValue: (value: string) => void
  online: boolean
  offlineDraft: boolean
  embedded: boolean
}

function Configurator({
  catalog,
  legacyCatalog,
  selections,
  setSelections,
  customizations,
  setCustomizations,
  savedPortableValue,
  setSavedPortableValue,
  online,
  offlineDraft,
  embedded,
}: ConfiguratorProps) {
  const initialCategoryId = categoriesInUiOrder(catalog)[0]?.categoryId ?? ''
  const [categoryId, setCategoryId] = useState(initialCategoryId)
  const [activeStepId, setActiveStepId] = useState<WorkflowStepId>('preset')
  const [selectedPresetId, setSelectedPresetId] = useState<'default' | 'imported' | null>(
    savedPortableValue ? null : 'default',
  )
  const [componentId, setComponentId] = useState('all')
  const [surfaceId, setSurfaceId] = useState(catalog.selectionOrder[0] ?? '')
  const [activeView, setActiveView] = useState<RenderViewId>('front-left')
  const [render, setRender] = useState<LegacyRender | null>(null)
  const [pendingRender, setPendingRender] = useState<LegacyRender | null>(null)
  const [renderRefreshKey, setRenderRefreshKey] = useState(0)
  const renderRequestRef = useRef('')
  const focusedAnimationIdRef = useRef<string | null>(null)
  const [renderLoading, setRenderLoading] = useState(!embedded)
  const [renderMessage, setRenderMessage] = useState('')
  const [syncState, setSyncState] = useState<'idle' | 'saving' | 'saved' | 'error'>(
    savedPortableValue ? 'saved' : 'idle',
  )
  const [syncMessage, setSyncMessage] = useState('')
  const [transferOpen, setTransferOpen] = useState(false)
  const [transferMode, setTransferMode] = useState<TransferMode>('import')
  const [importValue, setImportValue] = useState('')
  const currentCategory = catalog.categories.find((category) => category.categoryId === categoryId)
  const surfacesAsComponents = currentCategory?.ui?.navigationMode === 'surfaces-as-components'
  const components = useMemo(
    () => componentsForCategory(catalog, categoryId),
    [catalog, categoryId],
  )
  const surfaces = useMemo(
    () => surfacesAsComponents
      ? catalog.surfaces.filter((surface) => surface.surfaceId === componentId)
      : surfacesForComponent(catalog, componentId)
          .filter((surface) => components.some((component) => component.componentId === surface.componentId)),
    [catalog, componentId, components, surfacesAsComponents],
  )
  const categories = useMemo(
    () => categoriesInUiOrder(catalog),
    [catalog],
  )
  const currentComponent = components.find((component) => component.componentId === componentId)
  const currentSurface = catalog.surfaces.find((surface) => surface.surfaceId === surfaceId)
    ?? surfaces[0]
    ?? catalog.surfaces[0]
  const portableValue = useMemo(
    () => createPortableConfiguration(catalog, selections, customizations),
    [catalog, customizations, selections],
  )
  const portableQr = useMemo(
    () => transferOpen && transferMode === 'share'
      ? createPortableConfigurationQr(portableValue)
      : '',
    [portableValue, transferMode, transferOpen],
  )
  const dirty = portableValue !== savedPortableValue
  const renderSelectionKey = createRenderCanonicalKey(catalog, selections, customizations)
  const referenceTotal = catalog.vehicle.basePriceMinor + catalog.selectionOrder.reduce(
    (total, id) => {
      const option = catalog.options.find((item) => item.optionId === selections[id])
      return total + (
        option?.pricing.unitPriceMinor === null || option?.pricing.unitPriceMinor === undefined
          ? 0
          : option.pricing.unitPriceMinor * (option.pricing.quantity ?? 1)
      )
    },
    0,
  )
  const categorySurfaces = useMemo(() => {
    const componentIds = new Set(
      catalog.components
        .filter((component) => component.categoryId === categoryId)
        .map((component) => component.componentId),
    )
    return catalog.surfaces
      .filter((surface) => componentIds.has(surface.componentId))
      .sort((left, right) =>
        catalog.selectionOrder.indexOf(left.surfaceId)
        - catalog.selectionOrder.indexOf(right.surfaceId))
  }, [catalog.components, catalog.selectionOrder, catalog.surfaces, categoryId])
  const currentSurfaceIndex = Math.max(
    0,
    categorySurfaces.findIndex((surface) => surface.surfaceId === currentSurface.surfaceId),
  )

  const focusCatalogNode = useCallback((selection: {
    categoryId?: string
    componentId?: string
    surfaceId?: string
  }) => {
    if (!embedded) return
    const bridge = getUeBridge(true)
    void setUeCameraId(bridge, cameraIdForSelection(catalog, selection))
    const nextAnimationId = animationIdForSelection(catalog, selection)
    void focusUeAnimation(
      bridge,
      focusedAnimationIdRef.current,
      nextAnimationId,
    ).then((accepted) => {
      if (accepted) focusedAnimationIdRef.current = nextAnimationId
    })
  }, [catalog, embedded])

  const selectCategory = useCallback((nextCategoryId: string) => {
    if (!catalog.categories.some((category) => category.categoryId === nextCategoryId)) return
    setActiveStepId(nextCategoryId)
    setCategoryId(nextCategoryId)
    if (embedded) {
      void syncUeConfiguratorCategory(getUeBridge(true), nextCategoryId)
      focusCatalogNode({ categoryId: nextCategoryId })
    }
  }, [catalog, embedded, focusCatalogNode])

  const selectWorkflowStep = useCallback((stepId: WorkflowStepId) => {
    if (stepId === 'preset' || stepId === 'summary') {
      setActiveStepId(stepId)
      if (embedded) void syncUeConfiguratorCategory(getUeBridge(true), stepId)
      return
    }
    selectCategory(stepId)
  }, [embedded, selectCategory])

  const selectComponent = (nextComponentId: string) => {
    setComponentId(nextComponentId)
    if (surfacesAsComponents) setSurfaceId(nextComponentId)
    focusCatalogNode({
      categoryId,
      componentId: surfacesAsComponents ? undefined : nextComponentId,
      surfaceId: surfacesAsComponents ? nextComponentId : undefined,
    })
  }

  const selectSurface = (nextSurfaceId: string) => {
    setSurfaceId(nextSurfaceId)
    focusCatalogNode({ categoryId, componentId, surfaceId: nextSurfaceId })
  }

  const focusSurface = useCallback((
    nextCategoryId: string,
    nextSurface: CatalogV2['surfaces'][number],
  ) => {
    const nextCategory = catalog.categories.find(
      (category) => category.categoryId === nextCategoryId,
    )
    const nextSurfacesAsComponents = nextCategory?.ui?.navigationMode === 'surfaces-as-components'
    setActiveStepId(nextCategoryId)
    setCategoryId(nextCategoryId)
    setComponentId(nextSurfacesAsComponents ? nextSurface.surfaceId : nextSurface.componentId)
    setSurfaceId(nextSurface.surfaceId)
    if (embedded) void syncUeConfiguratorCategory(getUeBridge(true), nextCategoryId)
    focusCatalogNode({
      categoryId: nextCategoryId,
      componentId: nextSurfacesAsComponents ? undefined : nextSurface.componentId,
      surfaceId: nextSurface.surfaceId,
    })
  }, [catalog.categories, embedded, focusCatalogNode])

  const moveBySurface = (direction: -1 | 1) => {
    const nextSurface = categorySurfaces[currentSurfaceIndex + direction]
    if (nextSurface) {
      focusSurface(categoryId, nextSurface)
      return
    }
    const categoryIndex = categories.findIndex((category) => category.categoryId === categoryId)
    const adjacentCategory = categories[categoryIndex + direction]
    if (!adjacentCategory) {
      selectWorkflowStep(direction < 0 ? 'preset' : 'summary')
      return
    }
    const adjacentComponentIds = new Set(
      catalog.components
        .filter((component) => component.categoryId === adjacentCategory.categoryId)
        .map((component) => component.componentId),
    )
    const adjacentSurfaces = catalog.surfaces
      .filter((surface) => adjacentComponentIds.has(surface.componentId))
      .sort((left, right) =>
        catalog.selectionOrder.indexOf(left.surfaceId)
        - catalog.selectionOrder.indexOf(right.surfaceId))
    const target = direction < 0 ? adjacentSurfaces.at(-1) : adjacentSurfaces[0]
    if (target) focusSurface(adjacentCategory.categoryId, target)
  }

  const resetCurrentSurface = () => {
    const defaults = createInitialSelections(catalog)
    const nextSelections = { ...selections }
    const nextCustomizations = { ...customizations }
    const defaultOptionId = defaults[currentSurface.surfaceId]
    if (defaultOptionId) nextSelections[currentSurface.surfaceId] = defaultOptionId
    else delete nextSelections[currentSurface.surfaceId]
    delete nextCustomizations[currentSurface.surfaceId]
    setSelections(nextSelections)
    setCustomizations(normalizeCustomizations(catalog, nextSelections, nextCustomizations))
    setSyncState('idle')
    setSyncMessage(`已复位${currentSurface.displayName}`)
  }

  useEffect(() => {
    if (!embedded) return
    const handleCategory = (event: Event) => {
      const nextCategoryId = (event as CustomEvent<string>).detail
      if (nextCategoryId === 'preset' || nextCategoryId === 'summary') {
        setActiveStepId(nextCategoryId)
        return
      }
      if (catalog.categories.some((category) => category.categoryId === nextCategoryId)) {
        setActiveStepId(nextCategoryId)
        setCategoryId(nextCategoryId)
        focusCatalogNode({ categoryId: nextCategoryId })
      }
    }
    window.addEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
    return () => window.removeEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
  }, [catalog.categories, embedded, focusCatalogNode])

  useEffect(() => {
    const firstComponent = components[0]?.componentId
    setComponentId((current) =>
      components.some((component) => component.componentId === current)
        ? current
        : (firstComponent ?? 'all'))
  }, [categoryId, components])

  useEffect(() => {
    if (!surfaces.some((surface) => surface.surfaceId === surfaceId)) {
      setSurfaceId(surfaces[0]?.surfaceId ?? catalog.selectionOrder[0])
    }
  }, [catalog.selectionOrder, surfaceId, surfaces])

  useEffect(() => {
    if (embedded) {
      setRenderLoading(false)
      setRender(null)
      setPendingRender(null)
      setRenderMessage('')
      return
    }
    const controller = new AbortController()
    setRenderLoading(true)
    resolveRender({
      catalogVersion: catalog.catalogVersion,
      vehicleId: catalog.vehicle.vehicleId,
      selections,
      customizations,
      renderViewId: activeView,
    }, controller.signal)
      .then(async (result) => {
        if (controller.signal.aborted) return
        const requestKey = `${result.renderKey}::${activeView}`
        if (requestKey === renderRequestRef.current) {
          setRenderLoading(false)
          return
        }
        renderRequestRef.current = requestKey
        if (result.imageUrl) {
          setPendingRender({
            configurationKey: result.configurationId,
            renderViewId: activeView,
            imageUrl: result.imageUrl,
          })
          setRenderMessage('')
          return
        }
        const proxy = legacyCatalog
          ? await resolveLegacyProxy(legacyCatalog, activeView, controller.signal)
          : null
        if (controller.signal.aborted) return
        if (proxy) setPendingRender(proxy)
        setRenderMessage(
          proxy || render
            ? '当前展示烘焙车辆预览'
            : '车辆预览暂时不可用',
        )
        if (!proxy) setRenderLoading(false)
      })
      .catch((reason: unknown) => {
        if (controller.signal.aborted) return
        setRenderMessage(reason instanceof ApiError ? reason.message : '车辆预览暂时无法更新，已保留当前图片')
        setRenderLoading(false)
      })
    return () => controller.abort()
  // activeView 会触发解析，但只有 renderKey 变化时才替换图片。
  }, [
    activeView,
    catalog.catalogVersion,
    catalog.vehicle.vehicleId,
    embedded,
    legacyCatalog,
    renderRefreshKey,
    renderSelectionKey,
  ])

  useEffect(() => {
    const bridge = getUeBridge(embedded)
    if (bridge) applyUeConfiguration(bridge, selections, customizations)
  }, [customizations, embedded, selections])

  const selectOption = (surfaceId: string, optionId?: string) => {
    const requestedSelections = { ...selections }
    if (optionId) requestedSelections[surfaceId] = optionId
    else delete requestedSelections[surfaceId]
    const nextSelections = normalizeSelections(catalog, requestedSelections)
    setSelections(nextSelections)
    const option = catalog.options.find((item) => item.optionId === optionId)
    const existing = customizations[surfaceId]
    let nextCustomizations = normalizeCustomizations(catalog, nextSelections, customizations)
    if (option?.parameters.color?.mode === 'custom' && !existing) {
      const paint = createDefaultPaintCustomization(option)
      nextCustomizations = {
        ...nextCustomizations,
        [surfaceId]: paint,
      }
    }
    setCustomizations(nextCustomizations)
    setSyncState('idle')
    setSyncMessage('')
  }

  const setMaterialVariant = (surfaceId: string, materialVariantId: string, optionId: string) => {
    const nextSelections = { ...selections, [surfaceId]: optionId }
    setSelections(nextSelections)
    setCustomizations(normalizeCustomizations(catalog, nextSelections, {
      ...customizations,
      [surfaceId]: { materialVariantId },
    }))
    setSyncState('idle')
    setSyncMessage('')
  }

  const setPaintParameter = (
    surfaceId: string,
    key: keyof PaintCustomization,
    value: string | number,
  ) => {
    const customization = customizations[surfaceId]
    const paint = customization && !('materialVariantId' in customization)
      ? customization
      : null
    if (!paint) return
    setCustomizations({
      ...customizations,
      [surfaceId]: { ...paint, [key]: value },
    })
    setSyncState('idle')
    setSyncMessage('')
  }

  const patchPaintCustomization = (
    surfaceId: string,
    patch: Partial<PaintCustomization>,
  ) => {
    const customization = customizations[surfaceId]
    const paint = customization && !('materialVariantId' in customization)
      ? customization
      : null
    if (!paint) return
    setCustomizations({
      ...customizations,
      [surfaceId]: { ...paint, ...patch },
    })
    setSyncState('idle')
    setSyncMessage('')
  }

  const persist = (): string => {
    localStorage.setItem(PORTABLE_CACHE_KEY, portableValue)
    setSavedPortableValue(portableValue)
    setSyncState('saved')
    setSyncMessage('草稿已保存')
    return portableValue
  }

  const copyPortableValue = async (value: string) => {
    try {
      if (!navigator.clipboard) throw new Error('clipboard unavailable')
      await navigator.clipboard.writeText(value)
      setSyncMessage('自包含配置字符串已复制')
    } catch {
      setSyncMessage('剪贴板不可用，请从弹窗手动复制配置字符串')
    }
  }

  const share = async () => {
    const value = portableValue
    setImportValue(value)
    setTransferMode('share')
    setTransferOpen(true)
    const url = new URL(window.location.href)
    url.searchParams.delete('configuration')
    url.searchParams.set('config', value)
    if (!embedded) window.history.replaceState(null, '', url)
    await copyPortableValue(value)
  }

  const importPortable = () => {
    try {
      const imported = parsePortableConfiguration(importValue, catalog)
      const nextSelections = normalizeSelections(catalog, imported.selections)
      const nextCustomizations = normalizeCustomizations(
        catalog,
        nextSelections,
        imported.customizations,
      )
      setSelections(nextSelections)
      setCustomizations(nextCustomizations)
      const normalized = createPortableConfiguration(catalog, nextSelections, nextCustomizations)
      localStorage.setItem(PORTABLE_CACHE_KEY, normalized)
      setSavedPortableValue(normalized)
      setImportValue(normalized)
      setSyncState('saved')
      setSyncMessage('配置已导入')
      setSelectedPresetId('imported')
      setTransferOpen(false)
      selectWorkflowStep(categories[0]?.categoryId ?? 'summary')
    } catch (reason) {
      setSyncState('error')
      setSyncMessage(reason instanceof Error ? reason.message : '配置导入失败')
    }
  }

  const resetConfiguration = () => {
    const initialSelections = createInitialSelections(catalog)
    const initialCustomizations = normalizeCustomizations(catalog, initialSelections, {})
    const url = new URL(window.location.href)
    url.searchParams.delete('configuration')
    url.searchParams.delete('config')
    window.history.replaceState(null, '', `${url.pathname}${url.search}${url.hash}`)
    localStorage.removeItem(CACHE_KEY)
    localStorage.removeItem(PORTABLE_CACHE_KEY)
    setSelections(initialSelections)
    setCustomizations(initialCustomizations)
    setSavedPortableValue('')
    setCategoryId(categories[0]?.categoryId ?? '')
    setActiveStepId('preset')
    setComponentId('all')
    setSurfaceId(catalog.selectionOrder[0] ?? '')
    setActiveView('front-left')
    renderRequestRef.current = ''
    setRender(null)
    setPendingRender(null)
    setRenderLoading(!embedded)
    setRenderRefreshKey((value) => value + 1)
    setRenderMessage('')
    setSyncState('idle')
    setSyncMessage('已恢复默认配置')
  }

  const applyDefaultPreset = () => {
    const initialSelections = createInitialSelections(catalog)
    const initialCustomizations = normalizeCustomizations(catalog, initialSelections, {})
    setSelections(initialSelections)
    setCustomizations(initialCustomizations)
    setSelectedPresetId('default')
    setSyncState('idle')
    setSyncMessage('已选择默认配置')
  }

  useEffect(() => {
    if (!embedded) return
    void syncUeConfiguratorHeaderState(getUeBridge(true), {
      categoryId: activeStepId as UeConfiguratorCategory,
      referenceTotalMinor: referenceTotal,
      syncState,
      syncMessage,
      dirty,
      online,
    })
  }, [activeStepId, dirty, embedded, online, referenceTotal, syncMessage, syncState])

  useEffect(() => {
    if (!embedded) return
    const handleHeaderAction = (event: Event) => {
      const action = (event as CustomEvent<unknown>).detail
      if (action === 'save') void persist()
      if (action === 'share') void share()
      if (action === 'reset') resetConfiguration()
    }
    window.addEventListener(CONFIGURATOR_HEADER_ACTION_EVENT, handleHeaderAction)
    return () => window.removeEventListener(CONFIGURATOR_HEADER_ACTION_EVENT, handleHeaderAction)
  })

  const visibleSurfaces = [currentSurface]

  const renderSurfaceOptions = (surface: CatalogV2['surfaces'][number]) => {
    const options = optionsForSurface(catalog, surface.surfaceId, selections)
    const groupedFamilyIds = new Set(
      options.flatMap((option) =>
        option.materialFamilyId
          && (supportsMaterialVariants(option)
            || UPHOLSTERY_MATERIAL_FAMILIES.has(option.materialFamilyId))
          ? [option.materialFamilyId]
          : []),
    )
    const flatOptions = options.filter(
      (option) => !option.materialFamilyId || !groupedFamilyIds.has(option.materialFamilyId),
    )
    const materialGroups = catalog.materialFamilies.flatMap((materialFamily) => {
      if (!groupedFamilyIds.has(materialFamily.materialFamilyId)) return []
      const familyOptions = options.filter(
        (option) => option.materialFamilyId === materialFamily.materialFamilyId,
      )
      return familyOptions.length > 0 ? [{ materialFamily, options: familyOptions }] : []
    })
    const selectedOption = catalog.options.find(
      (option) => option.optionId === selections[surface.surfaceId],
    )
    const currentCustomization = customizations[surface.surfaceId]
    const referenceImageUrl = INTERIOR_PART_IMAGES[surface.surfaceId]
    const blackReference = BLACK_REFERENCE_SURFACES.has(surface.surfaceId)
    const showSeatBackplateFinish = surface.surfaceId === 'seat-shell-back'
      && selectedOption?.optionId === 'seat-shell-custom'
      && currentCustomization
      && !('materialVariantId' in currentCustomization)
    const seatBackplateFinish = showSeatBackplateFinish
      ? resolveSeatBackplateFinish(currentCustomization)
      : null

    const renderFlatOption = (
      option: CatalogV2['options'][number],
      displayName = option.displayName,
      thumbnailUrl = option.thumbnailUrl,
    ) => {
      const optionSelected = selections[surface.surfaceId] === option.optionId
        && !(currentCustomization && 'materialVariantId' in currentCustomization)
      const optionAvailable = optionIsAvailable(option, selections)
      const optionStatus = option.availability?.reason
        ?? (optionAvailable ? optionPrice(option) : '当前不可用')
      return (
        <button
          key={option.optionId}
          className={`color-choice ${optionSelected ? 'selected' : ''} ${optionAvailable ? '' : 'unavailable'}`}
          onClick={() => selectOption(surface.surfaceId, option.optionId)}
          aria-pressed={optionSelected}
          aria-label={`${displayName}，${optionStatus}`}
          disabled={!optionAvailable}
        >
          {(option.ui?.iconUrl ?? thumbnailUrl)
            ? <img
                src={versionStaticAssetUrl(
                  option.ui?.iconUrl ?? thumbnailUrl ?? DEFAULT_IMAGE_URL,
                )}
                alt=""
                loading="lazy"
                onError={useDefaultImage}
              />
            : <span
                className="color-choice-swatch"
                style={{ background: optionSwatch(option) }}
                aria-hidden="true"
              />}
          <span className="color-choice-name">{displayName}</span>
          <small>{optionStatus}</small>
        </button>
      )
    }

    return (
      <section className="surface-options" key={surface.surfaceId} aria-label={`${surface.displayName}配置`}>
        <div className="section-title">
          <h3>{surface.displayName}</h3>
        </div>
        {(referenceImageUrl || blackReference) && (
          <div
            className={`surface-reference ${blackReference ? 'surface-reference-black' : ''}`}
            aria-label={`${surface.displayName}定制项目参考`}
          >
            {referenceImageUrl && (
              <img
                src={versionStaticAssetUrl(referenceImageUrl)}
                alt={`${surface.displayName}定制项目参考`}
                loading="lazy"
              />
            )}
          </div>
        )}
        {(flatOptions.length > 0 || !surface.required) && (
          <div className="choice-grid flat-options">
            {!surface.required && (
              <button
                className={`color-choice ${selections[surface.surfaceId] ? '' : 'selected'}`}
                onClick={() => selectOption(surface.surfaceId)}
                aria-pressed={!selections[surface.surfaceId]}
                aria-label="默认，免费"
              >
                <img src={versionStaticAssetUrl(DEFAULT_IMAGE_URL)} alt="" />
                <span className="color-choice-name">默认</span>
                <small>免费</small>
              </button>
            )}
            {flatOptions.map((option) => renderFlatOption(option))}
          </div>
        )}
        <div className="material-options">
          {materialGroups.map(({ materialFamily, options: unsortedFamilyOptions }) => {
            const familyOptions = unsortedFamilyOptions.slice().sort((left, right) =>
              Number(right.pricing.isStandard) - Number(left.pricing.isStandard))
            const familyOptionIds = new Set(familyOptions.map((option) => option.optionId))
            const familySelected = selectedOption
              ? familyOptionIds.has(selectedOption.optionId)
              : false
            const standardFamilyOption = familyOptions.find(
              (option) => option.pricing.isStandard && !supportsMaterialVariants(option),
            )
            const variantOptions = familyOptions.filter(supportsMaterialVariants)
            const variantChoices = variantOptions.flatMap((option) =>
              sortMaterialVariants(
                materialVariantsForOption(catalog, option),
                materialFamily.ui?.variantSort,
              )
                .map((variant) => ({ option, variant })))
            const variantStripChoices = variantChoices.filter(({ option }) => usesMaterialStrip(option))
            const cardVariantChoices = variantChoices.filter(({ option }) => !usesMaterialStrip(option))
            const standardInStrip = Boolean(standardFamilyOption && variantStripChoices.length > 0)
            const standardFamilyVariant = materialFamily.ui?.defaultVariantId
              ? catalog.materialVariants.find(
                  (variant) => variant.variantId === materialFamily.ui?.defaultVariantId,
                )
              : undefined
            const stripChoices = [
              ...(standardInStrip && standardFamilyOption
                ? [{
                    option: standardFamilyOption,
                    choiceId: standardFamilyOption.optionId,
                    displayName: standardFamilyVariant?.displayName
                      ?? standardFamilyOption.displayName,
                    imageUrl: standardFamilyVariant?.thumbnailUrl
                      ?? standardFamilyOption.ui?.iconUrl
                      ?? standardFamilyOption.thumbnailUrl
                      ?? DEFAULT_IMAGE_URL,
                    colorHex: standardFamilyVariant?.ui?.sortColorHex
                      ?? optionSwatch(standardFamilyOption),
                  }]
                : []),
              ...variantStripChoices.map(({ option, variant }) => ({
                option,
                choiceId: `${option.optionId}:${variant.variantId}`,
                displayName: variant.displayName,
                imageUrl: variant.thumbnailUrl,
                colorHex: variant.ui?.sortColorHex ?? '#777a74',
                materialVariantId: variant.variantId,
              })),
            ]
            const remainingFamilyOptions = familyOptions.filter(
              (option) => option !== standardFamilyOption && !supportsMaterialVariants(option),
            )
            const firstVariantChoice = variantChoices[0]
            const activateFamily = () => {
              if (standardFamilyOption) {
                selectOption(surface.surfaceId, standardFamilyOption.optionId)
              } else if (firstVariantChoice) {
                setMaterialVariant(
                  surface.surfaceId,
                  firstVariantChoice.variant.variantId,
                  firstVariantChoice.option.optionId,
                )
              } else if (familyOptions[0]) {
                selectOption(surface.surfaceId, familyOptions[0].optionId)
              }
            }
            return (
              <section
                className={`material-family clickable ${familySelected ? 'selected' : 'muted'}`}
                key={materialFamily.materialFamilyId}
                aria-label={`${materialFamily.displayName}材质`}
                tabIndex={0}
                onClick={(event) => {
                  if (!(event.target as HTMLElement).closest('button, input')) activateFamily()
                }}
                onKeyDown={(event) => {
                  if (event.target === event.currentTarget && (event.key === 'Enter' || event.key === ' ')) {
                    event.preventDefault()
                    activateFamily()
                  }
                }}
              >
                <button
                  className="material-family-title"
                  onClick={activateFamily}
                  aria-pressed={familySelected}
                >
                  <strong>{materialFamily.displayName}</strong>
                  {familySelected && <span className="check" aria-hidden="true">✓</span>}
                </button>
                <div className="choice-grid material-family-defaults">
                  {standardFamilyOption && !standardInStrip && renderFlatOption(
                    standardFamilyOption,
                    standardFamilyOption.displayName,
                    standardFamilyOption.ui?.iconUrl ?? standardFamilyOption.thumbnailUrl,
                  )}
                  {remainingFamilyOptions.map((option) => renderFlatOption(option))}
                  {cardVariantChoices
                    .map(({ option, variant }) => {
                      const variantSelected = selections[surface.surfaceId] === option.optionId
                        && currentCustomization
                        && 'materialVariantId' in currentCustomization
                        && currentCustomization.materialVariantId === variant.variantId
                      return (
                      <button
                        key={`${option.optionId}:${variant.variantId}`}
                        className={`color-choice ${variantSelected ? 'selected' : ''}`}
                        onClick={() => setMaterialVariant(
                          surface.surfaceId,
                          variant.variantId,
                          option.optionId,
                        )}
                        aria-pressed={Boolean(variantSelected)}
                        aria-label={`${variant.displayName}，${materialFamily.displayName}，${optionPrice(option)}`}
                      >
                        <img
                          src={versionStaticAssetUrl(variant.thumbnailUrl)}
                          alt=""
                          loading="lazy"
                          onError={useDefaultImage}
                        />
                        <span className="color-choice-name">{variant.displayName}</span>
                        <small>{optionPrice(option)}</small>
                      </button>
                      )
                    })}
                </div>
                {stripChoices.length > 0 && (
                  <MaterialColorStrip
                    familyName={materialFamily.displayName}
                    choices={stripChoices}
                    selectedOptionId={selectedOption?.optionId}
                    selectedVariantId={
                      currentCustomization && 'materialVariantId' in currentCustomization
                        ? currentCustomization.materialVariantId
                        : undefined
                    }
                    formatPrice={optionPrice}
                    resolveImageUrl={versionStaticAssetUrl}
                    onImageError={useDefaultImage}
                    onCommit={({ option, materialVariantId }) => {
                      if (materialVariantId) {
                        setMaterialVariant(surface.surfaceId, materialVariantId, option.optionId)
                      } else {
                        selectOption(surface.surfaceId, option.optionId)
                      }
                    }}
                  />
                )}
              </section>
            )
          })}
        </div>

        {(selectedOption?.ui?.control === 'color-picker'
          || (!selectedOption?.ui?.control && selectedOption?.parameters.color?.mode === 'custom'))
          && currentCustomization
          && !('materialVariantId' in currentCustomization) && (
          <section
            className={`paint-editor ${showSeatBackplateFinish ? 'custom-finish-editor' : ''}`}
            aria-label={customEditorLabel(selectedOption.displayName)}
          >
            <div className="section-title">
              <h3>自定义颜色</h3>
              <span>{optionPrice(selectedOption)}</span>
            </div>
            <div className={showSeatBackplateFinish ? 'custom-finish-layout' : undefined}>
              {showSeatBackplateFinish && seatBackplateFinish && (
                <div className="finish-control">
                  <span>饰面</span>
                  <div role="group" aria-label={`${surface.displayName}表面效果`}>
                    {(Object.entries(SEAT_BACKPLATE_FINISH_PRESETS) as Array<
                      [SeatBackplateFinish, (typeof SEAT_BACKPLATE_FINISH_PRESETS)[SeatBackplateFinish]]
                    >).map(([finishId, preset]) => (
                      <button
                        key={finishId}
                        className={seatBackplateFinish === finishId ? 'selected' : ''}
                        aria-pressed={seatBackplateFinish === finishId}
                        onClick={() => patchPaintCustomization(surface.surfaceId, {
                          roughness: preset.roughness,
                          clearCoat: preset.clearCoat,
                        })}
                      >
                        {preset.label}
                      </button>
                    ))}
                  </div>
                </div>
              )}
              <div className="color-control">
                <span>颜色</span>
                <div className="color-picker-wrap">
                  <InlineColorPicker
                    value={currentCustomization.colorHex}
                    onChange={(value) => setPaintParameter(surface.surfaceId, 'colorHex', value)}
                  />
                  <input
                    className="color-hex-input"
                    aria-label={`${surface.displayName}颜色`}
                    type="text"
                    maxLength={7}
                    value={currentCustomization.colorHex}
                    onChange={(event) => {
                      const value = event.target.value.toUpperCase()
                      if (/^#[0-9A-F]{6}$/.test(value)) {
                        setPaintParameter(surface.surfaceId, 'colorHex', value)
                      }
                    }}
                  />
                </div>
              </div>
            </div>
          </section>
        )}
      </section>
    )
  }

  return (
    <main
      className={`app-shell ${embedded ? 'embedded' : 'standalone'}`}
      style={embedded ? undefined : {
        '--standalone-header-height': `${STANDALONE_LAYOUT.headerHeight}px`,
        '--standalone-panel-width': `${STANDALONE_LAYOUT.panelWidth}px`,
        '--standalone-stage-margin': `${STANDALONE_LAYOUT.stageMargin}px`,
        '--standalone-stage-radius': `${STANDALONE_LAYOUT.stageRadius}px`,
      } as CSSProperties}
    >
      {!embedded && (
        <ConfiguratorTopBar
          standalone
          categories={categories}
          activeStepId={activeStepId}
          headerState={{
            categoryId: activeStepId as UeConfiguratorCategory,
            referenceTotalMinor: referenceTotal,
            syncState,
            syncMessage: syncMessage || (
              online
                ? (offlineDraft ? '本地草稿' : (dirty ? '未保存更改' : '已保存'))
                : '离线 · 已保存本地'
            ),
            dirty,
            online,
          }}
          onAction={(action) => {
            if (action === 'save') void persist()
          }}
          onSelectStep={selectWorkflowStep}
        />
      )}
      <div className="configurator-workspace">
        {!embedded && <section className="stage" aria-label="车辆展示区">
          <Showroom />
          <div className="vehicle-frame">
            {renderLoading && <div className="render-status" role="status">正在加载车辆预览…</div>}
            {render && (
              <img
                className="vehicle-image vehicle-image-visible"
                src={render.imageUrl}
                alt={`${catalog.vehicle.displayName} 车辆预览`}
              />
            )}
            {pendingRender && (
              <img
                className="vehicle-image vehicle-image-preload"
                src={pendingRender.imageUrl}
                alt=""
                aria-hidden="true"
                onLoad={() => {
                  setRender(pendingRender)
                  setPendingRender(null)
                  setRenderLoading(false)
                }}
                onError={() => {
                  setPendingRender(null)
                  setRenderLoading(false)
                  setRenderMessage('车辆预览暂时无法更新，已保留上一张图片')
                }}
              />
            )}
          </div>
          {legacyCatalog && (
            <div className="view-switcher" role="group" aria-label="车辆视角">
              {legacyCatalog.renderViews.map((view) => (
                <button
                  key={view.renderViewId}
                  className={activeView === view.renderViewId ? 'active' : ''}
                  aria-pressed={activeView === view.renderViewId}
                  onClick={() => setActiveView(view.renderViewId)}
                >
                  {view.zhName}
                </button>
              ))}
            </div>
          )}
          {renderMessage && <p className="render-message" role="status">{renderMessage}</p>}
        </section>}

        <aside className="config-panel" aria-label="车辆选配">
          {embedded && <div className="panel-head">
            <div>
              <span className="eyebrow">SC01 / CONFIGURATOR</span>
              <h2 aria-label="SC01 定制">SC01 定制</h2>
            </div>
            <span className="step">4 阶段顺序选配</span>
          </div>}

          <div className="panel-scroll">
          {activeStepId === 'preset' && (
            <section className="preset-page" aria-label="预设配置">
              <div className="page-heading">
                <span>01 / PRESET</span>
                <h2>从预设开始</h2>
                <p>使用 SC01 默认配置，或导入已有选配码继续编辑。</p>
              </div>
              <div className="preset-grid">
                <button
                  className={`preset-card default-preset ${selectedPresetId === 'default' ? 'selected' : ''}`}
                  aria-label="默认配置"
                  aria-pressed={selectedPresetId === 'default'}
                  onClick={applyDefaultPreset}
                >
                  <span className="preset-card-visual" aria-hidden="true">
                    <img
                      src={versionStaticAssetUrl('/sc01/presets/default-exterior.webp')}
                      alt=""
                    />
                    <img
                      src={versionStaticAssetUrl('/sc01/presets/default-interior.webp')}
                      alt=""
                    />
                  </span>
                  <strong>默认配置</strong>
                  <small>¥{(catalog.vehicle.basePriceMinor / 100).toLocaleString('zh-CN')}</small>
                </button>
                <button
                  className="preset-card import-preset"
                  aria-label="导入配置"
                  onClick={() => {
                    setImportValue('')
                    setTransferMode('import')
                    setTransferOpen(true)
                  }}
                >
                  <span className="preset-plus" aria-hidden="true">＋</span>
                  <strong>导入配置</strong>
                  <small>粘贴 SC01CFG1 选配码</small>
                </button>
              </div>
            </section>
          )}
          {activeStepId !== 'preset' && activeStepId !== 'summary' && (
            <>
              <section className="part-selector" aria-label="部件与子项">
                <FilterGroup label="部件" items={components.map((item) => ({
                  id: item.componentId,
                  name: item.displayName,
                }))} value={componentId} onChange={selectComponent} />
                {!surfacesAsComponents
                  && surfaces.length > 1
                  && currentComponent?.ui?.navigationMode !== 'none' && (
                  <FilterGroup label="子项" items={surfaces.map((item) => ({
                    id: item.surfaceId,
                    name: item.displayName,
                  }))} value={currentSurface.surfaceId} onChange={selectSurface} />
                )}
              </section>
              <section className="options" aria-live="polite">
                {visibleSurfaces.map(renderSurfaceOptions)}
              </section>
            </>
          )}
          {activeStepId === 'summary' && (
            <section className="summary-page" aria-label="配置总览">
              <div className="page-heading">
                <span>06 / SUMMARY</span>
                <h2>配置总览</h2>
                <p>确认每个定制项目及对应参考价格。</p>
              </div>
              <div className="summary-list">
                {catalog.selectionOrder.flatMap((summarySurfaceId) => {
                  const summarySurface = catalog.surfaces.find(
                    (surface) => surface.surfaceId === summarySurfaceId,
                  )
                  const option = catalog.options.find(
                    (item) => item.optionId === selections[summarySurfaceId],
                  )
                  if (!summarySurface || !option) return []
                  return [(
                    <div className="summary-row" key={summarySurfaceId}>
                      <span>
                        <small>{summarySurface.displayName}</small>
                        <strong>{option.displayName}</strong>
                      </span>
                      <b>{optionPrice(option)}</b>
                    </div>
                  )]
                })}
              </div>
            </section>
          )}
          </div>
          <div className="panel-footer">
            <div className="panel-reference-total">
              <span>参考总价</span>
              <strong>¥{(referenceTotal / 100).toLocaleString('zh-CN')}</strong>
            </div>
            <div className="page-actions">
              {activeStepId === 'preset' && (
                <button className="primary" onClick={() => selectWorkflowStep(categories[0]?.categoryId ?? 'summary')}>
                  下一步
                </button>
              )}
              {activeStepId !== 'preset' && activeStepId !== 'summary' && (
                <>
                  <button onClick={() => moveBySurface(-1)}>上一步</button>
                  <button onClick={resetCurrentSurface}>复位</button>
                  <button className="primary" onClick={() => moveBySurface(1)}>下一步</button>
                </>
              )}
              {activeStepId === 'summary' && (
                <>
                  <button onClick={() => {
                    const lastCategory = categories.at(-1)
                    if (!lastCategory) return
                    const lastComponentIds = new Set(
                      catalog.components
                        .filter((component) => component.categoryId === lastCategory.categoryId)
                        .map((component) => component.componentId),
                    )
                    const lastSurface = catalog.surfaces
                      .filter((surface) => lastComponentIds.has(surface.componentId))
                      .sort((left, right) =>
                        catalog.selectionOrder.indexOf(left.surfaceId)
                        - catalog.selectionOrder.indexOf(right.surfaceId))
                      .at(-1)
                    if (lastSurface) focusSurface(lastCategory.categoryId, lastSurface)
                  }}>上一步</button>
                  <button className="primary" onClick={() => { void share() }}>分享</button>
                </>
              )}
            </div>
          </div>
        </aside>
      </div>
      {transferOpen && (
        <div className="portable-dialog-backdrop" role="presentation">
          <section
            className={`portable-dialog ${transferMode === 'import' ? 'portable-import-dialog' : ''}`}
            role="dialog"
            aria-modal="true"
            aria-label={transferMode === 'import' ? '配置导入' : '配置传输'}
          >
            <button
              className="portable-dialog-close"
              aria-label="关闭配置传输"
              onClick={() => setTransferOpen(false)}
            >×</button>
            {transferMode === 'share' && (
              <>
                <h2>配置传输</h2>
                <p>保存与分享使用同一个自包含字符串；二维码编码的也是该字符串。</p>
                <img src={portableQr} alt="当前配置二维码" />
              </>
            )}
            <textarea
              aria-label="配置字符串"
              value={importValue}
              onChange={(event) => setImportValue(event.target.value)}
              placeholder="粘贴 SC01CFG1. 开头的配置字符串"
            />
            <div className="portable-dialog-actions">
              <button onClick={importPortable}>导入</button>
              {transferMode === 'share' && (
                <button onClick={() => {
                  setImportValue(portableValue)
                  void copyPortableValue(portableValue)
                }}>复制当前配置</button>
              )}
            </div>
          </section>
        </div>
      )}
    </main>
  )
}

function FilterGroup({
  className = '',
  label,
  items,
  value,
  onChange,
}: {
  className?: string
  label: string
  items: Array<{ id: string; name: string }>
  value: string
  onChange: (value: string) => void
}) {
  return (
    <section className={`filter-group ${className}`.trim()} aria-label={`${label}筛选`}>
      <span>{label}</span>
      <div>
        {items.map((item) => (
          <button
            key={item.id}
            className={value === item.id ? 'active' : ''}
            onClick={() => onChange(item.id)}
            aria-pressed={value === item.id}
          >
            {item.name}
          </button>
        ))}
      </div>
    </section>
  )
}

function StatusScreen({
  title,
  detail,
  action,
  embedded = false,
}: {
  title: string
  detail: string
  action?: () => void
  embedded?: boolean
}) {
  return (
    <main className={`status-screen ${embedded ? 'embedded' : ''}`}>
      {!embedded && <Showroom />}
      <div className="status-card" role={action ? 'alert' : 'status'}>
        <span className={action ? 'status-icon error' : 'status-icon'} aria-hidden="true">
          {action ? '!' : ''}
        </span>
        <h1>{title}</h1>
        <p>{detail}</p>
        {action && <button onClick={action}>重新加载</button>}
      </div>
    </main>
  )
}
