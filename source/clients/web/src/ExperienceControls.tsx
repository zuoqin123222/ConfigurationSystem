import { useCallback, useEffect, useRef, useState } from 'react'
import { fetchCatalog } from './api'
import type { CatalogAnimation, CatalogCameraId, CatalogInteractionCamera } from './types'
import {
  executeUeControl,
  focusUeAnimation,
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

interface PendingCameraSelection {
  cameraId: CatalogCameraId
  cameraIndex: UeCameraIndex | null
}

function cameraSelectionMatchesState(
  selection: PendingCameraSelection,
  state: UePresentationState,
): boolean {
  return state.cameraId === selection.cameraId
    || (selection.cameraIndex !== null && state.cameraIndex === selection.cameraIndex)
}

export default function ExperienceControls({ ueEnabled = false }: ExperienceControlsProps) {
  const [cameraMenuOpen, setCameraMenuOpen] = useState(false)
  const [animationMenuOpen, setAnimationMenuOpen] = useState(false)
  const [qualityMenuOpen, setQualityMenuOpen] = useState(false)
  const [cameras, setCameras] = useState<CatalogInteractionCamera[]>([])
  const [animations, setAnimations] = useState<CatalogAnimation[]>([])
  const [cameraId, setCameraId] = useState<CatalogCameraId | null>(null)
  const [cameraIndex, setCameraIndex] = useState<UeCameraIndex | null>(null)
  const [pendingCameraSelection, setPendingCameraSelection] = useState<PendingCameraSelection | null>(null)
  const [animationEnabled, setAnimationEnabled] = useState(false)
  const [animationId, setAnimationId] = useState<string | null>(null)
  const [lightPreset, setLightPreset] = useState<'studio' | 'outdoor'>('studio')
  const [renderMode, setRenderMode] = useState<'realtime' | 'path-tracing'>('realtime')
  const [quality, setQuality] = useState<UeQualityLevel>('high')
  const [fullscreen, setFullscreen] = useState(false)
  const [toolbarIdle, setToolbarIdle] = useState(false)
  const [error, setError] = useState('')
  const idleTimer = useRef<number | null>(null)
  const cameraRequestIdRef = useRef(0)

  useEffect(() => {
    document.documentElement.classList.add('controls-document')
    document.body.classList.add('controls-document')
    return () => {
      cameraRequestIdRef.current += 1
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
    setPendingCameraSelection((current) => (
      current && cameraSelectionMatchesState(current, state)
        ? null
        : current
    ))
    setAnimationEnabled(state.animationEnabled)
    setAnimationId(state.animationId ?? null)
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
        setAnimations(catalog.animations ?? [])
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

  const run = async (
    createCommand: UeControlCommand | ((state: UePresentationState) => UeControlCommand),
    onSuccess?: (state: UePresentationState) => void,
  ) => {
    if (!ueEnabled) return false
    const bridge = getUeBridge(true)
    const currentState = await getUePresentationState(bridge)
    if (!currentState) {
      setError(bridge
        ? '无法读取 UE 展示状态'
        : 'UE 控制桥不可用或命令被拒绝')
      return false
    }
    applyState(currentState)
    const command = typeof createCommand === 'function'
      ? createCommand(currentState)
      : createCommand
    const accepted = await executeUeControl(bridge, command)
    setError(accepted ? '' : 'UE 控制桥不可用或命令被拒绝')
    if (accepted) {
      onSuccess?.(currentState)
      const updatedState = await getUePresentationState(bridge)
      if (updatedState) applyState(updatedState)
    }
    return accepted
  }

  const selectCamera = async (camera: CatalogInteractionCamera) => {
    if (!ueEnabled) return
    const bridge = getUeBridge(true)
    const requestId = cameraRequestIdRef.current + 1
    cameraRequestIdRef.current = requestId
    const targetSelection: PendingCameraSelection = {
      cameraId: camera.cameraId,
      cameraIndex: camera.legacyIndex ?? null,
    }
    setPendingCameraSelection(targetSelection)
    setError('')
    const accepted = await executeUeControl(bridge, {
      type: 'camera',
      cameraId: camera.cameraId,
      legacyIndex: camera.legacyIndex,
    })
    if (cameraRequestIdRef.current !== requestId) return
    setError(accepted ? '' : 'UE 控制桥不可用或命令被拒绝')
    if (!accepted) {
      setPendingCameraSelection(null)
      return
    }
    setCameraMenuOpen(false)
    for (let attempt = 0; attempt < 5; attempt += 1) {
      const updatedState = await getUePresentationState(bridge)
      if (cameraRequestIdRef.current !== requestId) return
      if (updatedState) {
        applyState(updatedState)
        if (cameraSelectionMatchesState(targetSelection, updatedState)) return
      }
      if (attempt < 4) {
        await new Promise<void>((resolve) => {
          window.setTimeout(resolve, 120)
        })
      }
    }
    if (cameraRequestIdRef.current === requestId) {
      setPendingCameraSelection(null)
      setError('UE 镜头切换未确认')
    }
  }

  const selectAnimation = async (nextAnimationId: string) => {
    if (!ueEnabled) return
    const bridge = getUeBridge(true)
    const currentState = await getUePresentationState(bridge)
    if (!currentState) {
      setError(bridge
        ? '无法读取 UE 展示状态'
        : 'UE 控制桥不可用或命令被拒绝')
      return
    }
    applyState(currentState)
    const currentAnimationId = currentState.animationId ?? null
    const nextId = currentAnimationId === nextAnimationId ? null : nextAnimationId
    const accepted = await focusUeAnimation(
      bridge,
      currentAnimationId,
      nextId,
    )
    setError(accepted ? '' : 'UE 控制桥不可用或命令被拒绝')
    if (accepted) {
      setAnimationId(nextId)
      setAnimationEnabled(nextId !== null)
      setAnimationMenuOpen(false)
      const updatedState = await getUePresentationState(bridge)
      if (updatedState) applyState(updatedState)
    }
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
                  aria-checked={(pendingCameraSelection?.cameraId ?? cameraId) !== null
                    ? (pendingCameraSelection?.cameraId ?? cameraId) === camera.cameraId
                    : (pendingCameraSelection?.cameraIndex ?? cameraIndex) !== null
                      && (pendingCameraSelection?.cameraIndex ?? cameraIndex) === camera.legacyIndex}
                  onClick={() => void selectCamera(camera)}
                >
                  <img src={camera.iconUrl} alt="" />
                  {camera.displayName}
                </button>
              ))}
            </div>
          )}
        </div>
        <div className="toolbar-item">
          <button
            aria-expanded={animationMenuOpen}
            aria-pressed={animationEnabled}
            onClick={() => setAnimationMenuOpen((open) => !open)}
          >
            <span aria-hidden="true">▷</span>
            动画
          </button>
          {animationMenuOpen && (
            <div className="control-popover animation-popover" role="menu" aria-label="动画列表">
              {animations.map((animation) => (
                <button
                  key={animation.animationId}
                  role="menuitemradio"
                  aria-checked={animationId === animation.animationId}
                  onClick={() => void selectAnimation(animation.animationId)}
                >
                  {animation.displayName}
                </button>
              ))}
            </div>
          )}
        </div>
        <button
          aria-pressed={lightPreset === 'outdoor'}
          onClick={() => {
            void run(
              (state) => ({
                type: 'light',
                preset: state.lightPreset === 'studio' ? 'outdoor' : 'studio',
              }),
              (state) => setLightPreset(
                state.lightPreset === 'studio' ? 'outdoor' : 'studio',
              ),
            )
          }}
        >
          <span aria-hidden="true">☼</span>
          灯光
        </button>
        <button
          className="path-tracing-toggle"
          aria-pressed={renderMode === 'path-tracing'}
          aria-label="Path Tracing"
          onClick={() => {
            void run(
              (state) => ({
                type: 'render',
                mode: state.renderMode === 'realtime' ? 'path-tracing' : 'realtime',
              }),
              (state) => setRenderMode(
                state.renderMode === 'realtime' ? 'path-tracing' : 'realtime',
              ),
            )
          }}
        >
          <span aria-hidden="true">◇</span>
          Path Tracing
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
        <button onClick={() => {
          cameraRequestIdRef.current += 1
          setPendingCameraSelection(null)
          void run(
            { type: 'reset' },
            () => {
              void getUePresentationState(getUeBridge(true)).then((state) => {
                if (state) applyState(state)
              })
            },
          )
        }}>
          <span aria-hidden="true">↺</span>
          复位
        </button>
        <button
          aria-pressed={fullscreen}
          onClick={() => void run(
            (state) => ({ type: 'fullscreen', enabled: !state.fullscreen }),
            (state) => setFullscreen(!state.fullscreen),
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
