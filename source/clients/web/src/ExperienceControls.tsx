import { useCallback, useEffect, useRef, useState } from 'react'
import { fetchCatalog } from './api'
import type { CatalogCameraId, CatalogInteractionCamera } from './types'
import {
  executeUeControl,
  getUeBridge,
  getUePresentationState,
  type UeControlCommand,
  type UeCameraIndex,
  type UePresentationState,
  type UeQualityLevel,
} from './ueBridge'

const QUALITY_LEVELS: Array<{ value: UeQualityLevel; label: string }> = [
  { value: 'low', label: '低' },
  { value: 'medium', label: '中' },
  { value: 'high', label: '高' },
  { value: 'epic', label: '极高' },
]

interface ExperienceControlsProps {
  ueEnabled?: boolean
}

export default function ExperienceControls({ ueEnabled = false }: ExperienceControlsProps) {
  const [cameraMenuOpen, setCameraMenuOpen] = useState(false)
  const [qualityMenuOpen, setQualityMenuOpen] = useState(false)
  const [cameras, setCameras] = useState<CatalogInteractionCamera[]>([])
  const [cameraId, setCameraId] = useState<CatalogCameraId | null>(null)
  const [cameraIndex, setCameraIndex] = useState<UeCameraIndex | null>(null)
  const [animationEnabled, setAnimationEnabled] = useState(false)
  const [lightPreset, setLightPreset] = useState<'studio' | 'outdoor'>('studio')
  const [renderMode, setRenderMode] = useState<'realtime' | 'path-tracing'>('realtime')
  const [quality, setQuality] = useState<UeQualityLevel>('high')
  const [fullscreen, setFullscreen] = useState(false)
  const [toolbarIdle, setToolbarIdle] = useState(false)
  const [error, setError] = useState('')
  const idleTimer = useRef<number | null>(null)

  useEffect(() => {
    document.documentElement.classList.add('controls-document')
    document.body.classList.add('controls-document')
    return () => {
      document.documentElement.classList.remove('controls-document')
      document.body.classList.remove('controls-document')
    }
  }, [])

  const keepToolbarAwake = useCallback(() => {
    setToolbarIdle(false)
    if (idleTimer.current !== null) window.clearTimeout(idleTimer.current)
    if (fullscreen) {
      idleTimer.current = window.setTimeout(() => setToolbarIdle(true), 1800)
    }
  }, [fullscreen])

  useEffect(() => {
    keepToolbarAwake()
    return () => {
      if (idleTimer.current !== null) window.clearTimeout(idleTimer.current)
    }
  }, [keepToolbarAwake])

  const applyState = (state: UePresentationState) => {
    setCameraId(state.cameraId ?? null)
    setCameraIndex(state.cameraIndex ?? null)
    setAnimationEnabled(state.animationEnabled)
    setLightPreset(state.lightPreset)
    setRenderMode(state.renderMode)
    setQuality(state.quality)
    setFullscreen(state.fullscreen)
  }

  useEffect(() => {
    if (!ueEnabled) return
    let active = true
    const controller = new AbortController()
    void fetchCatalog(controller.signal)
      .then((catalog) => {
        if (!active) return
        setCameras((catalog.interactionCameras ?? [])
          .slice()
          .sort((left, right) => left.order - right.order))
      })
      .catch(() => {
        if (active && !controller.signal.aborted) setError('无法读取镜头目录')
      })
    void getUePresentationState(getUeBridge(true)).then((state) => {
      if (!active) return
      if (state) {
        applyState(state)
        setError('')
      } else {
        setError('无法读取 UE 展示状态')
      }
    })
    return () => {
      active = false
      controller.abort()
    }
  }, [ueEnabled])

  const run = async (command: UeControlCommand, onSuccess?: () => void) => {
    if (!ueEnabled) return false
    const accepted = await executeUeControl(getUeBridge(true), command)
    setError(accepted ? '' : 'UE 控制桥不可用或命令被拒绝')
    if (accepted) onSuccess?.()
    return accepted
  }

  return (
    <main
      className={`controls-view ${fullscreen ? 'fullscreen' : ''} ${toolbarIdle ? 'toolbar-idle' : ''}`}
      onPointerMove={keepToolbarAwake}
    >
      <nav className="experience-toolbar" aria-label="体验控制">
        <div className="toolbar-item">
          <button
            aria-expanded={cameraMenuOpen}
            onClick={() => setCameraMenuOpen((open) => !open)}
          >
            <span aria-hidden="true">◉</span>
            镜头
          </button>
          {cameraMenuOpen && (
            <div className="control-popover camera-popover" role="menu" aria-label="镜头预设">
              {cameras.map((camera) => (
                <button
                  key={String(camera.cameraId)}
                  role="menuitemradio"
                  aria-checked={cameraId !== null
                    ? cameraId === camera.cameraId
                    : cameraIndex !== null && cameraIndex === camera.legacyIndex}
                  onClick={() => void run(
                    {
                      type: 'camera',
                      cameraId: camera.cameraId,
                      legacyIndex: camera.legacyIndex,
                    },
                    () => {
                      setCameraId(camera.cameraId)
                      setCameraIndex(camera.legacyIndex ?? null)
                      setCameraMenuOpen(false)
                    },
                  )}
                >
                  <img src={camera.iconUrl} alt="" />
                  {camera.displayName}
                </button>
              ))}
            </div>
          )}
        </div>
        <button
          aria-pressed={animationEnabled}
          onClick={() => void run(
            { type: 'animation', enabled: !animationEnabled },
            () => setAnimationEnabled((enabled) => !enabled),
          )}
        >
          <span aria-hidden="true">▷</span>
          动画
        </button>
        <button
          aria-pressed={lightPreset === 'outdoor'}
          onClick={() => {
            const nextPreset = lightPreset === 'studio' ? 'outdoor' : 'studio'
            void run(
              { type: 'light', preset: nextPreset },
              () => setLightPreset(nextPreset),
            )
          }}
        >
          <span aria-hidden="true">☼</span>
          灯光
        </button>
        <button
          aria-pressed={renderMode === 'path-tracing'}
          onClick={() => {
            const nextMode = renderMode === 'realtime' ? 'path-tracing' : 'realtime'
            void run(
              { type: 'render', mode: nextMode },
              () => setRenderMode(nextMode),
            )
          }}
        >
          <span aria-hidden="true">◇</span>
          渲染
        </button>
        <div className="toolbar-item">
          <button
            aria-expanded={qualityMenuOpen}
            onClick={() => setQualityMenuOpen((open) => !open)}
          >
            <span aria-hidden="true">◐</span>
            画质
          </button>
          {qualityMenuOpen && (
            <div className="control-popover quality-popover" role="menu" aria-label="画质设置">
              {QUALITY_LEVELS.map((level) => (
                <button
                  key={level.value}
                  role="menuitemradio"
                  aria-checked={quality === level.value}
                  onClick={() => void run(
                    { type: 'quality', quality: level.value },
                    () => {
                      setQuality(level.value)
                      setQualityMenuOpen(false)
                    },
                  )}
                >
                  {level.label}
                </button>
              ))}
            </div>
          )}
        </div>
        <button onClick={() => void run(
          { type: 'reset' },
          () => {
            void getUePresentationState(getUeBridge(true)).then((state) => {
              if (state) applyState(state)
            })
          },
        )}>
          <span aria-hidden="true">↺</span>
          复位
        </button>
        <button
          aria-pressed={fullscreen}
          onClick={() => void run(
            { type: 'fullscreen', enabled: !fullscreen },
            () => setFullscreen((enabled) => !enabled),
          )}
        >
          <span aria-hidden="true">□</span>
          全屏
        </button>
      </nav>
      {error && <p className="bridge-error" role="alert">{error}</p>}
    </main>
  )
}
