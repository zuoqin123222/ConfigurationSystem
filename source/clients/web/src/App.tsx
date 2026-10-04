import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
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
  saveConfiguration,
} from './api'
import {
  animationIdForSelection,
  cameraIdForSelection,
  categoriesInUiOrder,
  componentsForCategory,
  createCanonicalKey,
  createDefaultPaintCustomization,
  createInitialSelections,
  createRenderCanonicalKey,
  materialVariantsForOption,
  normalizeCustomizations,
  normalizeSelections,
  optionsForSurface,
  sortMaterialVariants,
  supportsMaterialVariants,
  surfacesForComponent,
} from './configurator'
import type {
  CatalogV2,
  ConfigurationV2,
  Customizations,
  LegacyCatalog,
  LegacyRender,
  PaintCustomization,
  RenderViewId,
  Selections,
} from './types'
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

const CACHE_KEY = 'automotive-v2-configurator'
const DEFAULT_IMAGE_URL = '/sc01/option-icons/default.svg'
const SRGB_TO_LINEAR_TABLE = Array.from({ length: 256 }, (_, index) => {
  const value = index / 255
  return value <= 0.04045
    ? value / 12.92
    : Math.pow((value + 0.055) / 1.055, 2.4)
}).join(' ')

export function isEmbeddedView(search = window.location.search): boolean {
  return new URLSearchParams(search).get('view') === 'embedded'
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
    return `¥${(option.pricing.unitPriceMinor / 100).toLocaleString('zh-CN')}`
  }
  return '价格待确认'
}

function optionSwatch(option: CatalogV2['options'][number]): string {
  if (option.parameters.color?.mode === 'custom') {
    return 'conic-gradient(#e84b4b, #e8ce4b, #55bb6a, #4b8ee8, #9855c7, #e84b4b)'
  }
  const color = option.parameters.color?.value ?? option.colorCode
  if (color && /^#[0-9a-f]{6}$/i.test(color)) return color
  if (option.pricing.isStandard) return '#171817'
  return '#8b8d88'
}

function useDefaultImage(event: SyntheticEvent<HTMLImageElement>) {
  const image = event.currentTarget
  if (!image.src.endsWith(DEFAULT_IMAGE_URL)) image.src = DEFAULT_IMAGE_URL
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
  const hasEmbeddedUeBridge = view !== 'default' && getUeBridge(true) !== null
  let content
  if (view === 'controls') {
    content = <ExperienceControls ueEnabled />
  } else if (view === 'header') {
    content = <ConfiguratorHeader />
  } else {
    content = <ConfiguratorApp embedded={view === 'embedded'} />
  }
  return hasEmbeddedUeBridge
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
    categoryId: 'exterior',
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
    void fetchCatalog(controller.signal).then((catalog) => {
      const nextCategories = categoriesInUiOrder(catalog)
      setCategories(nextCategories)
      setHeaderState((current) => nextCategories.some(
        (category) => category.categoryId === current.categoryId,
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

  const triggerAction = (action: 'save' | 'share') => {
    void triggerUeConfiguratorHeaderAction(getUeBridge(true), action)
  }

  const selectCategory = (categoryId: string) => {
    if (!categories.some((category) => category.categoryId === categoryId)) return
    setHeaderState((current) => ({ ...current, categoryId }))
    void syncUeConfiguratorCategory(getUeBridge(true), categoryId)
  }

  return (
    <ConfiguratorTopBar
      categories={categories}
      headerState={headerState}
      onAction={triggerAction}
      onSelectCategory={selectCategory}
    />
  )
}

function ConfiguratorTopBar({
  categories,
  headerState,
  onAction,
  onSelectCategory,
  standalone = false,
}: {
  categories: CatalogV2['categories']
  headerState: UeConfiguratorHeaderState
  onAction: (action: 'save' | 'share') => void
  onSelectCategory: (categoryId: string) => void
  standalone?: boolean
}) {
  const activeStageIndex = Math.max(
    0,
    categories.findIndex((category) => category.categoryId === headerState.categoryId),
  )
  return (
    <header className={`configurator-header ${standalone ? 'standalone-header' : ''}`}>
      <h1 className="brand-title" aria-label="SC01 定制">
        <strong>SC</strong><em>01</em><small>定制</small>
      </h1>
      <nav
        aria-label="选配阶段"
        style={{ gridTemplateColumns: `repeat(${Math.max(categories.length, 1)}, minmax(0, 1fr))` }}
      >
        {categories.length > 0 && <span
          className="stage-indicator"
          aria-hidden="true"
          style={{
            width: `${100 / categories.length}%`,
            transform: `translateX(${activeStageIndex * 100}%)`,
          }}
        />}
        {categories.map((category, index) => (
          <div className="header-stage" key={category.categoryId}>
            <button
              className={headerState.categoryId === category.categoryId ? 'active' : ''}
              aria-label={category.displayName}
              aria-current={headerState.categoryId === category.categoryId ? 'step' : undefined}
              onClick={() => onSelectCategory(category.categoryId)}
            >
              <span>{String(index + 1).padStart(2, '0')}</span>
              {category.displayName}
            </button>
          </div>
        ))}
      </nav>
      <div className="header-actions">
        <span className={`header-sync ${headerState.syncState}`}>
          {headerState.syncMessage || (headerState.dirty ? '未同步更改' : '已同步')}
        </span>
        <span className="header-total">
          <small>参考总价</small>
          <strong>¥{(headerState.referenceTotalMinor / 100).toLocaleString('zh-CN')}</strong>
        </span>
        <button
          className="header-save"
          onClick={() => onAction('save')}
          disabled={!headerState.online || headerState.syncState === 'saving' || !headerState.dirty}
        >
          {headerState.syncState === 'saving' ? '保存中…' : '保存'}
        </button>
        <button
          className="header-share"
          onClick={() => onAction('share')}
          disabled={!headerState.online || headerState.syncState === 'saving'}
        >
          分享
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
  const [savedConfiguration, setSavedConfiguration] = useState<ConfigurationV2 | null>(null)
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
      ? fetchCatalog(controller.signal).then((nextCatalog) => ({
          catalog: nextCatalog,
          legacyCatalog: null,
        }))
      : fetchInitialData(controller.signal)
    initialData
      .then(async ({ catalog: nextCatalog, legacyCatalog: nextLegacyCatalog }) => {
        if (controller.signal.aborted) return
        const configurationId = new URLSearchParams(window.location.search).get('configuration')
        let initialSelections = createInitialSelections(nextCatalog)
        let initialCustomizations = normalizeCustomizations(nextCatalog, initialSelections, {})
        let loadedConfiguration: ConfigurationV2 | null = null
        if (configurationId) {
          loadedConfiguration = await fetchConfiguration(configurationId, controller.signal)
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
        if (controller.signal.aborted) return
        setCatalog(nextCatalog)
        setLegacyCatalog(nextLegacyCatalog)
        setSelections(initialSelections)
        setCustomizations(initialCustomizations)
        setSavedConfiguration(loadedConfiguration)
      })
      .catch((reason: unknown) => {
        if (controller.signal.aborted) return
        const cached = readCachedDraft()
        if (!navigator.onLine && cached) {
          setCatalog(cached.catalog)
          const cachedSelections = normalizeSelections(cached.catalog, cached.selections)
          setSelections(cachedSelections)
          setCustomizations(normalizeCustomizations(
            cached.catalog,
            cachedSelections,
            cached.customizations ?? {},
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
      savedConfiguration={savedConfiguration}
      setSavedConfiguration={setSavedConfiguration}
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
  savedConfiguration: ConfigurationV2 | null
  setSavedConfiguration: (value: ConfigurationV2 | null) => void
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
  savedConfiguration,
  setSavedConfiguration,
  online,
  offlineDraft,
  embedded,
}: ConfiguratorProps) {
  const [categoryId, setCategoryId] = useState(
    categoriesInUiOrder(catalog)[0]?.categoryId ?? '',
  )
  const [componentId, setComponentId] = useState('all')
  const [surfaceId, setSurfaceId] = useState(catalog.selectionOrder[0] ?? '')
  const [activeView, setActiveView] = useState<RenderViewId>('front-left')
  const [render, setRender] = useState<LegacyRender | null>(null)
  const [pendingRender, setPendingRender] = useState<LegacyRender | null>(null)
  const renderRequestRef = useRef('')
  const focusedAnimationIdRef = useRef<string | null>(null)
  const [renderLoading, setRenderLoading] = useState(!embedded)
  const [renderMessage, setRenderMessage] = useState('')
  const [syncState, setSyncState] = useState<'idle' | 'saving' | 'saved' | 'error'>(
    savedConfiguration ? 'saved' : 'idle',
  )
  const [syncMessage, setSyncMessage] = useState('')
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
  const canonicalKey = createCanonicalKey(catalog, selections, customizations)
  const savedKey = savedConfiguration
    ? createCanonicalKey(
        catalog,
        normalizeSelections(catalog, savedConfiguration.selections),
        savedConfiguration.customizations,
      )
    : ''
  const dirty = canonicalKey !== savedKey
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
    setCategoryId(nextCategoryId)
    if (embedded) {
      void syncUeConfiguratorCategory(getUeBridge(true), nextCategoryId)
      focusCatalogNode({ categoryId: nextCategoryId })
    }
  }, [catalog, embedded, focusCatalogNode])

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

  useEffect(() => {
    if (!embedded) return
    const handleCategory = (event: Event) => {
      const nextCategoryId = (event as CustomEvent<string>).detail
      if (catalog.categories.some((category) => category.categoryId === nextCategoryId)) {
        setCategoryId(nextCategoryId)
        focusCatalogNode({ categoryId: nextCategoryId })
      }
    }
    window.addEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
    return () => window.removeEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
  }, [catalog.categories, embedded, focusCatalogNode])

  useEffect(() => {
    const firstComponent = components[0]?.componentId
    setComponentId(firstComponent ?? 'all')
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
    renderSelectionKey,
  ])

  useEffect(() => {
    const bridge = getUeBridge(embedded)
    if (bridge) applyUeConfiguration(bridge, selections, customizations)
  }, [customizations, embedded, selections])

  const selectOption = (surfaceId: string, optionId?: string) => {
    const nextSelections = { ...selections }
    if (optionId) nextSelections[surfaceId] = optionId
    else delete nextSelections[surfaceId]
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

  const persist = async (): Promise<ConfigurationV2 | null> => {
    if (!online) {
      setSyncState('error')
      setSyncMessage('当前离线，草稿已保存在本机，联网后可同步')
      return null
    }
    setSyncState('saving')
    setSyncMessage('')
    try {
      const stored = await saveConfiguration({
        catalogVersion: catalog.catalogVersion,
        vehicleId: catalog.vehicle.vehicleId,
        selections,
        customizations,
        ...(savedConfiguration && dirty
          ? {
              configurationId: savedConfiguration.configurationId,
              revision: savedConfiguration.revision,
            }
          : {}),
      })
      setSavedConfiguration(stored)
      setSyncState('saved')
      setSyncMessage(`已同步 · revision ${stored.revision}`)
      return stored
    } catch (reason) {
      setSyncState('error')
      setSyncMessage(reason instanceof Error ? reason.message : '保存失败')
      return null
    }
  }

  const share = async () => {
    const stored = dirty || !savedConfiguration ? await persist() : savedConfiguration
    if (!stored) return
    const url = new URL(window.location.href)
    url.searchParams.set('configuration', stored.configurationId)
    window.history.replaceState(null, '', url)
    try {
      await navigator.clipboard.writeText(url.toString())
      setSyncMessage('分享链接已复制')
    } catch {
      setSyncMessage(`分享链接：${url.toString()}`)
    }
  }

  useEffect(() => {
    if (!embedded) return
    void syncUeConfiguratorHeaderState(getUeBridge(true), {
      categoryId: categoryId as UeConfiguratorCategory,
      referenceTotalMinor: referenceTotal,
      syncState,
      syncMessage,
      dirty,
      online,
    })
  }, [categoryId, dirty, embedded, online, referenceTotal, syncMessage, syncState])

  useEffect(() => {
    if (!embedded) return
    const handleHeaderAction = (event: Event) => {
      const action = (event as CustomEvent<unknown>).detail
      if (action === 'save') void persist()
      if (action === 'share') void share()
    }
    window.addEventListener(CONFIGURATOR_HEADER_ACTION_EVENT, handleHeaderAction)
    return () => window.removeEventListener(CONFIGURATOR_HEADER_ACTION_EVENT, handleHeaderAction)
  })

  const visibleSurfaces = currentComponent?.ui?.layout === 'stack' ? surfaces : [currentSurface]

  const renderSurfaceOptions = (surface: CatalogV2['surfaces'][number]) => {
    const options = optionsForSurface(catalog, surface.surfaceId)
    const variantFamilyIds = new Set(
      options.flatMap((option) =>
        supportsMaterialVariants(option) && option.materialFamilyId
          ? [option.materialFamilyId]
          : []),
    )
    const flatOptions = options.filter(
      (option) => !option.materialFamilyId || !variantFamilyIds.has(option.materialFamilyId),
    )
    const materialGroups = catalog.materialFamilies.flatMap((materialFamily) => {
      if (!variantFamilyIds.has(materialFamily.materialFamilyId)) return []
      const familyOptions = options.filter(
        (option) => option.materialFamilyId === materialFamily.materialFamilyId,
      )
      return familyOptions.length > 0 ? [{ materialFamily, options: familyOptions }] : []
    })
    const selectedOption = catalog.options.find(
      (option) => option.optionId === selections[surface.surfaceId],
    )
    const currentCustomization = customizations[surface.surfaceId]

    const renderFlatOption = (
      option: CatalogV2['options'][number],
      displayName = option.displayName,
      thumbnailUrl = option.thumbnailUrl,
    ) => {
      const optionSelected = selections[surface.surfaceId] === option.optionId
        && !(currentCustomization && 'materialVariantId' in currentCustomization)
      return (
        <button
          key={option.optionId}
          className={`color-choice ${optionSelected ? 'selected' : ''}`}
          onClick={() => selectOption(surface.surfaceId, option.optionId)}
          aria-pressed={optionSelected}
          aria-label={`${displayName}，${optionPrice(option)}`}
        >
          {(option.ui?.iconUrl ?? thumbnailUrl)
            ? <img
                src={option.ui?.iconUrl ?? thumbnailUrl ?? DEFAULT_IMAGE_URL}
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
          <small>{optionPrice(option)}</small>
        </button>
      )
    }

    return (
      <section className="surface-options" key={surface.surfaceId} aria-label={`${surface.displayName}配置`}>
        <div className="section-title">
          <h3>{surface.displayName}</h3>
        </div>
        {(flatOptions.length > 0 || !surface.required) && (
          <div className="choice-grid flat-options">
            {!surface.required && (
              <button
                className={`color-choice ${selections[surface.surfaceId] ? '' : 'selected'}`}
                onClick={() => selectOption(surface.surfaceId)}
                aria-pressed={!selections[surface.surfaceId]}
                aria-label="默认，免费"
              >
                <img src={DEFAULT_IMAGE_URL} alt="" />
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
              }
            }
            return (
              <section
                className={`material-family ${familySelected ? 'selected' : 'muted'}`}
                key={materialFamily.materialFamilyId}
                aria-label={`${materialFamily.displayName}材质`}
              >
                <button
                  className="material-family-title"
                  onClick={activateFamily}
                  aria-pressed={familySelected}
                >
                  <strong>{materialFamily.displayName}</strong>
                  <span className="check" aria-hidden="true">{familySelected ? '✓' : ''}</span>
                </button>
                <div className="choice-grid">
                  {standardFamilyOption && renderFlatOption(
                    standardFamilyOption,
                    standardFamilyOption.displayName,
                    standardFamilyOption.ui?.iconUrl ?? standardFamilyOption.thumbnailUrl,
                  )}
                  {remainingFamilyOptions.map((option) => renderFlatOption(option))}
                  {variantChoices
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
                          src={variant.thumbnailUrl}
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
              </section>
            )
          })}
        </div>

        {(selectedOption?.ui?.control === 'color-picker'
          || (!selectedOption?.ui?.control && selectedOption?.parameters.color?.mode === 'custom'))
          && currentCustomization
          && !('materialVariantId' in currentCustomization) && (
          <section className="paint-editor" aria-label={`${selectedOption.displayName}颜色`}>
            <div className="section-title">
              <h3>自定义色</h3>
              <span>{optionPrice(selectedOption)}</span>
            </div>
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
          </section>
        )}
      </section>
    )
  }

  return (
    <main className={`app-shell ${embedded ? 'embedded' : 'standalone'}`}>
      {!embedded && (
        <ConfiguratorTopBar
          standalone
          categories={categories}
          headerState={{
            categoryId: categoryId as UeConfiguratorCategory,
            referenceTotalMinor: referenceTotal,
            syncState,
            syncMessage: syncMessage || (
              online
                ? (offlineDraft ? '本地草稿待同步' : (dirty ? '未同步更改' : '已同步'))
                : '离线 · 已保存本地'
            ),
            dirty,
            online,
          }}
          onAction={(action) => {
            if (action === 'save') void persist()
            if (action === 'share') void share()
          }}
          onSelectCategory={selectCategory}
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
          <FilterGroup
              label="分类"
              items={categories.map((item) => ({
                id: item.categoryId,
                name: item.displayName,
                iconUrl: item.ui?.iconUrl,
              }))}
              value={categoryId}
              onChange={selectCategory}
            />
          <section className="part-selector" aria-label="部件与子项">
            <FilterGroup label="部件" items={components.map((item) => ({
              id: item.componentId,
              name: item.displayName,
              iconUrl: item.ui?.iconUrl,
            }))} value={componentId} onChange={selectComponent} />
            {!surfacesAsComponents
              && surfaces.length > 1
              && currentComponent?.ui?.navigationMode !== 'none' && (
              <FilterGroup label="子项" items={surfaces.map((item) => ({
                id: item.surfaceId,
                name: item.displayName,
                iconUrl: item.ui?.iconUrl,
              }))} value={currentSurface.surfaceId} onChange={selectSurface} />
            )}
          </section>
          <section className="options" aria-live="polite">
            {visibleSurfaces.map(renderSurfaceOptions)}
          </section>
          </div>

        </aside>
      </div>
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
  items: Array<{ id: string; name: string; iconUrl?: string | null }>
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
            <img src={item.iconUrl ?? DEFAULT_IMAGE_URL} alt="" onError={useDefaultImage} />
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
