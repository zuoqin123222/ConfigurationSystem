import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
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
  componentsForCategory,
  createCanonicalKey,
  createInitialSelections,
  createRenderCanonicalKey,
  materialGroupsForSurface,
  materialVariantsForOption,
  normalizeCustomizations,
  normalizeSelections,
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
  CONFIGURATOR_CATEGORIES,
  CONFIGURATOR_CATEGORY_EVENT,
  CONFIGURATOR_HEADER_ACTION_EVENT,
  CONFIGURATOR_HEADER_STATE_EVENT,
  getUeConfiguratorHeaderState,
  getUeBridge,
  isUeConfiguratorHeaderState,
  syncUeConfiguratorCategory,
  syncUeConfiguratorHeaderState,
  triggerUeConfiguratorHeaderAction,
  type UeConfiguratorCategory,
  type UeConfiguratorHeaderState,
} from './ueBridge'
import ExperienceControls from './ExperienceControls'

const CACHE_KEY = 'sc01-v2-configurator'

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
  if (option.pricing.unitPriceMinor === 0) return '免费'
  if (option.pricing.unitPriceMinor !== null) {
    return `¥${(option.pricing.unitPriceMinor / 100).toLocaleString('zh-CN')}`
  }
  return option.pricing.isStandard ? '标配' : '价格待确认'
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
  if (view === 'controls') {
    return <ExperienceControls ueEnabled />
  }
  if (view === 'header') {
    return <ConfiguratorHeader />
  }
  return <ConfiguratorApp embedded={view === 'embedded'} />
}

function ConfiguratorHeader() {
  const [categoryId, setCategoryId] = useState<UeConfiguratorCategory>('exterior')
  const [headerState, setHeaderState] = useState<UeConfiguratorHeaderState>({
    categoryId: 'exterior',
    referenceTotalMinor: 22980000,
    syncState: 'idle',
    syncMessage: '',
    dirty: true,
    online: true,
  })

  useEffect(() => {
    document.documentElement.classList.add('header-document')
    document.body.classList.add('header-document')
    const handleCategory = (event: Event) => {
      const nextCategory = (event as CustomEvent<string>).detail
      if (CONFIGURATOR_CATEGORIES.some((category) => category.id === nextCategory)) {
        setCategoryId(nextCategory as UeConfiguratorCategory)
      }
    }
    const handleHeaderState = (event: Event) => {
      const nextState = (event as CustomEvent<unknown>).detail
      if (isUeConfiguratorHeaderState(nextState)) {
        setHeaderState(nextState)
        setCategoryId(nextState.categoryId)
      }
    }
    window.addEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
    window.addEventListener(CONFIGURATOR_HEADER_STATE_EVENT, handleHeaderState)
    void getUeConfiguratorHeaderState(getUeBridge(true)).then((state) => {
      if (state) {
        setHeaderState(state)
        setCategoryId(state.categoryId)
      }
    })
    return () => {
      window.removeEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
      window.removeEventListener(CONFIGURATOR_HEADER_STATE_EVENT, handleHeaderState)
      document.documentElement.classList.remove('header-document')
      document.body.classList.remove('header-document')
    }
  }, [])

  const selectCategory = (nextCategory: UeConfiguratorCategory) => {
    setCategoryId(nextCategory)
    void syncUeConfiguratorCategory(getUeBridge(true), nextCategory)
  }

  const triggerAction = (action: 'save' | 'share') => {
    void triggerUeConfiguratorHeaderAction(getUeBridge(true), action)
  }

  return (
    <header className="configurator-header">
      <h1>打造你的座驾</h1>
      <nav aria-label="选配阶段">
        {CONFIGURATOR_CATEGORIES.map((category, index) => (
          <div className="header-stage" key={category.id}>
            {index > 0 && <span className="header-stage-separator" aria-hidden="true">&gt;&gt;</span>}
            <button
              className={categoryId === category.id ? 'active' : ''}
              aria-current={categoryId === category.id ? 'step' : undefined}
              onClick={() => selectCategory(category.id)}
            >
              <span>{String(index + 1).padStart(2, '0')}</span>
              {category.label}
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
          onClick={() => triggerAction('save')}
          disabled={!headerState.online || headerState.syncState === 'saving' || !headerState.dirty}
        >
          {headerState.syncState === 'saving' ? '保存中…' : '保存'}
        </button>
        <button
          className="header-share"
          onClick={() => triggerAction('share')}
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
        let initialCustomizations: Customizations = {}
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
  const [categoryId, setCategoryId] = useState(catalog.categories[0]?.categoryId ?? '')
  const [componentId, setComponentId] = useState('all')
  const [surfaceId, setSurfaceId] = useState(catalog.selectionOrder[0] ?? '')
  const [activeView, setActiveView] = useState<RenderViewId>('front-left')
  const [render, setRender] = useState<LegacyRender | null>(null)
  const [pendingRender, setPendingRender] = useState<LegacyRender | null>(null)
  const [renderKey, setRenderKey] = useState('')
  const renderRequestRef = useRef('')
  const [renderLoading, setRenderLoading] = useState(!embedded)
  const [renderMessage, setRenderMessage] = useState('')
  const [syncState, setSyncState] = useState<'idle' | 'saving' | 'saved' | 'error'>(
    savedConfiguration ? 'saved' : 'idle',
  )
  const [syncMessage, setSyncMessage] = useState('')

  const components = useMemo(
    () => componentsForCategory(catalog, categoryId),
    [catalog, categoryId],
  )
  const surfaces = useMemo(
    () => surfacesForComponent(catalog, componentId)
      .filter((surface) => components.some((component) => component.componentId === surface.componentId)),
    [catalog, componentId, components],
  )
  const currentSurface = catalog.surfaces.find((surface) => surface.surfaceId === surfaceId)
    ?? surfaces[0]
    ?? catalog.surfaces[0]
  const materialGroups = materialGroupsForSurface(catalog, currentSurface.surfaceId)
  const options = materialGroups.flatMap((group) => group.options)
  const selectedOption = catalog.options.find(
    (option) => option.optionId === selections[currentSurface.surfaceId],
  )
  const currentCustomization = customizations[currentSurface.surfaceId]
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

  const selectCategory = useCallback((nextCategoryId: string) => {
    if (!catalog.categories.some((category) => category.categoryId === nextCategoryId)) return
    setCategoryId(nextCategoryId)
    if (embedded) {
      void syncUeConfiguratorCategory(getUeBridge(true), nextCategoryId)
    }
  }, [catalog.categories, embedded])

  useEffect(() => {
    if (!embedded) return
    const handleCategory = (event: Event) => {
      const nextCategoryId = (event as CustomEvent<string>).detail
      if (catalog.categories.some((category) => category.categoryId === nextCategoryId)) {
        setCategoryId(nextCategoryId)
      }
    }
    window.addEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
    return () => window.removeEventListener(CONFIGURATOR_CATEGORY_EVENT, handleCategory)
  }, [catalog.categories, embedded])

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
        setRenderKey(result.renderKey)
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
            ? 'v2 暂无渲染图，当前保留 v1 代理图'
            : 'v2 暂无渲染图，且 v1 代理图不可用',
        )
        if (!proxy) setRenderLoading(false)
      })
      .catch((reason: unknown) => {
        if (controller.signal.aborted) return
        setRenderMessage(reason instanceof ApiError ? reason.message : '渲染状态同步失败，已保留当前图片')
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

  const selectOption = (optionId?: string) => {
    const nextSelections = { ...selections }
    if (optionId) nextSelections[currentSurface.surfaceId] = optionId
    else delete nextSelections[currentSurface.surfaceId]
    setSelections(nextSelections)
    const option = catalog.options.find((item) => item.optionId === optionId)
    const existing = customizations[currentSurface.surfaceId]
    let nextCustomizations = normalizeCustomizations(catalog, nextSelections, customizations)
    if (option?.optionId === 'body-cover-custom' && !existing) {
      nextCustomizations = {
        ...nextCustomizations,
        [currentSurface.surfaceId]: {
          colorHex: '#A61D24',
          metallic: 0.35,
          roughness: 0.28,
          clearCoat: 0.8,
          orangePeel: 0.15,
          flakeIntensity: 0.25,
        },
      }
    }
    setCustomizations(nextCustomizations)
    setSyncState('idle')
    setSyncMessage('')
  }

  const setMaterialVariant = (materialVariantId: string, optionId: string) => {
    const nextSelections = { ...selections, [currentSurface.surfaceId]: optionId }
    setSelections(nextSelections)
    setCustomizations(normalizeCustomizations(catalog, nextSelections, {
      ...customizations,
      [currentSurface.surfaceId]: { materialVariantId },
    }))
    setSyncState('idle')
    setSyncMessage('')
  }

  const setPaintParameter = (
    key: keyof PaintCustomization,
    value: string | number,
  ) => {
    const paint = currentCustomization && !('materialVariantId' in currentCustomization)
      ? currentCustomization
      : null
    if (!paint) return
    setCustomizations({
      ...customizations,
      [currentSurface.surfaceId]: { ...paint, [key]: value },
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

  return (
    <main className={`app-shell ${embedded ? 'embedded' : ''}`}>
      {!embedded && <section className="stage" aria-label="车辆展示区">
        <Showroom />
        <header className="brand">
          <span className="brand-mark">S</span>
          <span>SC01</span>
        </header>
        <div className="connection" role="status">
          <span className={online ? 'online-dot' : 'offline-dot'} />
          {online ? (offlineDraft ? '已联网 · 本地草稿' : '在线') : '离线 · 本地草稿'}
        </div>
        <div className="vehicle-title">
          <p>高定制纯电跑车 · 技术预览</p>
          <h1>{catalog.vehicle.displayName}</h1>
          <span className="draft-badge">DRAFT · 不可报价</span>
        </div>
        <div className="vehicle-frame">
          {renderLoading && <div className="render-status" role="status">正在同步渲染标识…</div>}
          {render && (
            <img
              className="vehicle-image vehicle-image-visible"
              src={render.imageUrl}
              alt={`${catalog.vehicle.displayName} 代理车辆`}
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
                setRenderMessage('v2 暂无渲染图，v1 代理图加载失败，已保留上一张图片')
              }}
            />
          )}
        </div>
        <div className="image-meta">
          <strong>{renderMessage || 'v2 渲染图已就绪'}</strong>
          <code title={renderKey}>renderKey · {renderKey || '解析中'}</code>
        </div>
      </section>}

      <aside className="config-panel" aria-label="车辆选配">
        <div className="panel-head">
          <div>
            <span className="eyebrow">SC01 / CONFIGURATOR</span>
            <h2>打造你的座驾</h2>
          </div>
          <span className="step">4 阶段顺序选配</span>
        </div>

        <div className="panel-scroll">
          <FilterGroup
            className="filter-group-stage"
            label="阶段"
            items={catalog.categories.map((item) => ({
              id: item.categoryId,
              name: item.displayName,
            }))}
            value={categoryId}
            onChange={selectCategory}
          />
          <FilterGroup label="部件" items={components.map((item) => ({
            id: item.componentId,
            name: item.displayName,
          }))} value={componentId} onChange={setComponentId} />
          <FilterGroup label="项目" items={surfaces.map((item) => ({
            id: item.surfaceId,
            name: item.displayName,
          }))} value={currentSurface.surfaceId} onChange={setSurfaceId} />
          <section className="options" aria-live="polite">
          <div className="section-title">
            <h3>选择{currentSurface.displayName}</h3>
            <span>{currentSurface.required ? `${options.length} 款可选` : '默认不选装'}</span>
          </div>
          {!currentSurface.required && (
            <button
              className={`none-option ${selections[currentSurface.surfaceId] ? '' : 'selected'}`}
              onClick={() => selectOption()}
              aria-pressed={!selections[currentSurface.surfaceId]}
            >
              不选装 <small>默认 · ¥0</small>
            </button>
          )}
          <div className="material-groups">
            {materialGroups.map(({ materialFamily, options: familyOptions }) => {
              const active = selectedOption?.materialFamilyId === materialFamily.materialFamilyId
              const colorCardOptions = familyOptions.filter(
                (option) => materialVariantsForOption(catalog, option).length > 0,
              )
              return (
                <section
                  className={`material-group ${active ? 'active' : ''}`}
                  key={materialFamily.materialFamilyId}
                  aria-label={`${materialFamily.displayName}材质组`}
                >
                  <div className="material-group-title">
                    <strong>{materialFamily.displayName}</strong>
                    <span>{active ? '当前材质' : '可选材质'}</span>
                  </div>
                  <div className="option-grid">
                    {familyOptions.map((option) => {
                      const selected = selections[currentSurface.surfaceId] === option.optionId
                      return (
                <button
                  key={option.optionId}
                  className={`option-card ${selected ? 'selected' : ''}`}
                  onClick={() => selectOption(option.optionId)}
                  aria-pressed={selected}
                >
                  <span className={`option-swatch swatch-${option.optionId}`} aria-hidden="true">
                    {option.displayName.slice(0, 1)}
                  </span>
                  <span className="option-info">
                    <strong>{option.displayName}</strong>
                    <small>
                      {option.pricing.isStandard ? '默认色 · 免费' : optionPrice(option)}
                    </small>
                  </span>
                  <span className="check" aria-hidden="true">{selected ? '✓' : ''}</span>
                </button>
                      )
                    })}
                  </div>
                  {colorCardOptions.map((colorCardOption) => (
                    <div
                      className="variant-strip"
                      role="region"
                      key={colorCardOption.optionId}
                      aria-label={colorCardOptions.length === 1
                        ? `${materialFamily.displayName} PDF 色卡`
                        : `${colorCardOption.displayName} PDF 色卡`}
                    >
                      {materialVariantsForOption(catalog, colorCardOption).map((variant) => {
                        const selected = selections[currentSurface.surfaceId] === colorCardOption.optionId
                          && currentCustomization
                          && 'materialVariantId' in currentCustomization
                          && currentCustomization.materialVariantId === variant.variantId
                        return (
                          <button
                            key={variant.variantId}
                            className={selected ? 'selected' : ''}
                            onClick={() => setMaterialVariant(
                              variant.variantId,
                              colorCardOption.optionId,
                            )}
                            aria-pressed={Boolean(selected)}
                            aria-label={`${variant.displayName}，PDF 色卡定制，${optionPrice(colorCardOption)}`}
                            title={`${variant.displayName} · PDF 色卡定制`}
                          >
                            <img src={variant.thumbnailUrl} alt="" loading="lazy" />
                            <span>{variant.displayName}</span>
                          </button>
                        )
                      })}
                    </div>
                  ))}
                </section>
              )
            })}
          </div>
          </section>

          {selectedOption?.optionId === 'body-cover-custom'
            && currentCustomization
            && !('materialVariantId' in currentCustomization) && (
            <section className="paint-editor" aria-label="自定义车漆参数">
            <div className="section-title">
              <h3>自定义车漆</h3>
              <span>¥9,600</span>
            </div>
            <label className="color-control">
              <span>颜色</span>
              <input
                aria-label="车漆颜色"
                type="color"
                value={currentCustomization.colorHex}
                onChange={(event) => setPaintParameter('colorHex', event.target.value.toUpperCase())}
              />
              <code>{currentCustomization.colorHex}</code>
            </label>
            {([
              ['metallic', '金属度'],
              ['roughness', '粗糙度'],
              ['clearCoat', '清漆层'],
              ['orangePeel', '橘皮纹'],
              ['flakeIntensity', '金属闪片'],
            ] as const).map(([key, label]) => (
              <label className="range-control" key={key}>
                <span>{label}</span>
                <input
                  aria-label={label}
                  type="range"
                  min="0"
                  max="1"
                  step="0.01"
                  value={currentCustomization[key]}
                  onChange={(event) => setPaintParameter(key, Number(event.target.value))}
                />
                <output>{currentCustomization[key].toFixed(2)}</output>
              </label>
            ))}
            </section>
          )}
        </div>

      </aside>
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
