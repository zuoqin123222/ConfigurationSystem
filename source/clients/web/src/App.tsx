import { useCallback, useEffect, useMemo, useState } from 'react'
import { ApiError, fetchInitialData, resolveRender } from './api'
import {
  calculateTotalPrice,
  createCanonicalKey,
  createInitialSelections,
  formatPrice,
} from './configurator'
import type {
  Catalog,
  CatalogOption,
  PartId,
  RenderViewId,
  ResolveRenderResponse,
  Selections,
} from './types'

function Showroom() {
  return (
    <svg className="showroom" viewBox="0 0 1200 760" aria-hidden="true">
      <defs>
        <linearGradient id="wall" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#252823" />
          <stop offset="1" stopColor="#11130f" />
        </linearGradient>
        <radialGradient id="floor">
          <stop offset="0" stopColor="#5d6354" stopOpacity=".42" />
          <stop offset="1" stopColor="#11130f" stopOpacity="0" />
        </radialGradient>
      </defs>
      <rect width="1200" height="760" fill="url(#wall)" />
      <path d="M0 0h1200v70L0 250z" fill="#30332c" />
      <path d="M0 760V455L1200 70v690z" fill="#151713" />
      <ellipse cx="575" cy="605" rx="530" ry="145" fill="url(#floor)" />
      <g stroke="#d8dfc9" strokeOpacity=".2" strokeWidth="3">
        <path d="M150 0v225M350 0v165M850 0v92M1060 0v62" />
        <path d="M95 226l970-173" />
      </g>
    </svg>
  )
}

interface PreviewImageProps {
  src: string
  alt: string
  className: string
}

function PreviewImage({ src, alt, className }: PreviewImageProps) {
  const [failed, setFailed] = useState(false)
  useEffect(() => setFailed(false), [src])

  if (failed) {
    return <div className={`${className} image-placeholder`} role="img" aria-label={`${alt}图片暂缺`}>暂无图片</div>
  }
  return <img className={className} src={src} alt={alt} onError={() => setFailed(true)} />
}

export default function App() {
  const [catalog, setCatalog] = useState<Catalog | null>(null)
  const [publicationVersion, setPublicationVersion] = useState('')
  const [selections, setSelections] = useState<Selections | null>(null)
  const [activePart, setActivePart] = useState<PartId>('paint')
  const [activeView, setActiveView] = useState<RenderViewId>('front-left')
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState('')
  const [reloadKey, setReloadKey] = useState(0)

  const loadCatalog = useCallback(() => setReloadKey((value) => value + 1), [])

  useEffect(() => {
    const controller = new AbortController()
    setLoading(true)
    setError('')
    fetchInitialData(controller.signal)
      .then(({ catalog: nextCatalog, publicationVersion: nextPublicationVersion }) => {
        if (controller.signal.aborted) return
        setCatalog(nextCatalog)
        setPublicationVersion(nextPublicationVersion)
        setSelections(createInitialSelections(nextCatalog))
        setActiveView(nextCatalog.renderViews[1]?.renderViewId ?? nextCatalog.renderViews[0].renderViewId)
      })
      .catch((reason: unknown) => {
        if (!controller.signal.aborted) {
          setError(reason instanceof Error ? reason.message : '目录加载失败')
        }
      })
      .finally(() => {
        if (!controller.signal.aborted) setLoading(false)
      })
    return () => controller.abort()
  }, [reloadKey])

  if (loading) return <StatusScreen title="正在准备您的专属座驾" detail="正在加载车型与选配目录…" />
  if (error || !catalog || !selections) {
    return <StatusScreen title="暂时无法进入展厅" detail={error || '目录数据不可用'} action={loadCatalog} />
  }

  return (
    <Configurator
      catalog={catalog}
      publicationVersion={publicationVersion}
      selections={selections}
      setSelections={setSelections}
      activePart={activePart}
      setActivePart={setActivePart}
      activeView={activeView}
      setActiveView={setActiveView}
      reloadCatalog={loadCatalog}
    />
  )
}

interface ConfiguratorProps {
  catalog: Catalog
  publicationVersion: string
  selections: Selections
  setSelections: (value: Selections) => void
  activePart: PartId
  setActivePart: (value: PartId) => void
  activeView: RenderViewId
  setActiveView: (value: RenderViewId) => void
  reloadCatalog: () => void
}

function Configurator({
  catalog,
  publicationVersion,
  selections,
  setSelections,
  activePart,
  setActivePart,
  activeView,
  setActiveView,
  reloadCatalog,
}: ConfiguratorProps) {
  const [resolvedRender, setResolvedRender] = useState<ResolveRenderResponse | null>(null)
  const [renderLoading, setRenderLoading] = useState(true)
  const [renderError, setRenderError] = useState<{ message: string; status?: number } | null>(null)
  const orderedParts = useMemo(
    () => [...catalog.parts].sort((a, b) => a.displayOrder - b.displayOrder),
    [catalog.parts],
  )
  const currentPart = orderedParts.find((part) => part.partId === activePart) ?? orderedParts[0]
  const canonicalKey = createCanonicalKey(selections)
  const totalPrice = calculateTotalPrice(catalog, selections)

  useEffect(() => {
    const controller = new AbortController()
    setResolvedRender(null)
    setRenderLoading(true)
    setRenderError(null)
    resolveRender(
      {
        catalogVersion: catalog.catalogVersion,
        publicationVersion,
        vehicleId: catalog.vehicle.vehicleId,
        selections,
        renderViewId: activeView,
      },
      controller.signal,
    )
      .then((result) => {
        if (!controller.signal.aborted) setResolvedRender(result)
      })
      .catch((reason: unknown) => {
        if (controller.signal.aborted) return
        if (reason instanceof ApiError && reason.status === 409) {
          setRenderError({ status: 409, message: '发布版本已更新，请重新加载目录' })
        } else if (reason instanceof ApiError && reason.status === 404) {
          setRenderError({ status: 404, message: '该配置在当前视角暂无可用图片' })
        } else {
          setRenderError({
            status: reason instanceof ApiError ? reason.status : undefined,
            message: reason instanceof Error ? reason.message : '车辆图片解析失败',
          })
        }
      })
      .finally(() => {
        if (!controller.signal.aborted) setRenderLoading(false)
      })
    return () => controller.abort()
  }, [activeView, catalog.catalogVersion, catalog.vehicle.vehicleId, publicationVersion, selections])

  const selectOption = (option: CatalogOption) => {
    setSelections({ ...selections, [currentPart.partId]: option.optionId })
  }

  return (
    <main className="app-shell">
      <section className="stage" aria-label="车辆展示区">
        <Showroom />
        <header className="brand">
          <span className="brand-mark">Y</span>
          <span>曜影汽车</span>
        </header>
        <div className="vehicle-title">
          <p>全新纯电旗舰</p>
          <h1>{catalog.vehicle.zhName}</h1>
        </div>
        <div className="vehicle-frame">
          {renderLoading && <div className="render-status" role="status">正在解析车辆图片…</div>}
          {renderError && (
            <div className="render-status render-error" role="alert">
              <span>{renderError.message}</span>
              {renderError.status === 409 && <button onClick={reloadCatalog}>重新加载目录</button>}
            </div>
          )}
          {resolvedRender && (
            <PreviewImage
              className="vehicle-image"
              src={resolvedRender.imageUrl}
              alt={`${catalog.vehicle.zhName} ${activeView}`}
            />
          )}
        </div>
        <div className="view-switcher" aria-label="车辆视角">
          {catalog.renderViews.map((view) => (
            <button
              key={view.renderViewId}
              className={activeView === view.renderViewId ? 'active' : ''}
              onClick={() => setActiveView(view.renderViewId)}
              aria-pressed={activeView === view.renderViewId}
            >
              {view.zhName}
            </button>
          ))}
        </div>
        <div className="image-meta">
          <span>图片地址</span>
          <code title={resolvedRender?.imageUrl}>
            {resolvedRender?.imageUrl ?? (renderLoading ? '解析中' : '暂无可用图片')}
          </code>
        </div>
      </section>

      <aside className="config-panel" aria-label="车辆选配">
        <div className="panel-head">
          <div>
            <span className="eyebrow">个性化定制</span>
            <h2>打造你的座驾</h2>
          </div>
          <span className="step">04 项配置</span>
        </div>

        <section className="templates" aria-labelledby="template-title">
          <div className="section-title">
            <h3 id="template-title">推荐方案</h3>
            <span>一键应用</span>
          </div>
          <div className="template-grid">
            {catalog.templates.map((template, index) => (
              <button key={template.templateId} onClick={() => setSelections({ ...template.selections })}>
                <span className={`template-icon template-${index}`} aria-hidden="true" />
                <span><strong>{template.zhName}</strong><small>{index === 0 ? '动感与操控' : '舒适与质感'}</small></span>
                <span aria-hidden="true">→</span>
              </button>
            ))}
          </div>
        </section>

        <nav className="part-tabs" aria-label="选配分区">
          {orderedParts.map((part, index) => (
            <button
              key={part.partId}
              className={activePart === part.partId ? 'active' : ''}
              onClick={() => setActivePart(part.partId)}
              aria-current={activePart === part.partId ? 'page' : undefined}
            >
              <span>0{index + 1}</span>{part.zhName}
            </button>
          ))}
        </nav>

        <section className="options" aria-live="polite">
          <div className="section-title">
            <h3>选择{currentPart.zhName}</h3>
            <span>{currentPart.options.length} 款可选</span>
          </div>
          <div className="option-grid">
            {currentPart.options.map((option) => {
              const selected = selections[currentPart.partId] === option.optionId
              return (
                <button
                  key={option.optionId}
                  className={`option-card ${selected ? 'selected' : ''}`}
                  onClick={() => selectOption(option)}
                  aria-pressed={selected}
                >
                  <PreviewImage className="option-preview" src={option.previewImageUrl} alt={option.zhName} />
                  <span className="option-info">
                    <strong>{option.zhName}</strong>
                    <small>{formatPrice(option.priceDeltaMinor, true)}</small>
                  </span>
                  <span className="check" aria-hidden="true">{selected ? '✓' : ''}</span>
                </button>
              )
            })}
          </div>
        </section>

        <footer className="summary">
          <div className="canonical">
            <span>配置标识 · CANONICAL KEY</span>
            <code>{canonicalKey}</code>
          </div>
          <div className="total">
            <span>车辆总价<small>含基础配置与已选项目</small></span>
            <strong>{formatPrice(totalPrice)}</strong>
          </div>
        </footer>
      </aside>
    </main>
  )
}

function StatusScreen({
  title,
  detail,
  action,
}: {
  title: string
  detail: string
  action?: () => void
}) {
  return (
    <main className="status-screen">
      <Showroom />
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
