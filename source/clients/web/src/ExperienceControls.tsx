import { useCallback, useEffect, useRef, useState } from 'react'
import { fetchCatalog } from './api'
import { runCefBridgeProbe } from './cefBridgeProbe'
import {
  bundledCatalog,
  usesBundledCatalog,
} from './bundledCatalog'
import type { CatalogAnimation, CatalogCameraId, CatalogInteractionCamera } from './types'
import {
  canPlayUeAnimation,
  executeUeControl,
  focusUeAnimation,
  getUeBridge,
  getUeControlFailureKind,
  getUeControlFailureMessage,
  getUePresentationState,
  getUeRenderModeError,
  hasUeAnimationBridgeMethod,
  type ReflectedUeBridge,
  type UeControlCommand,
  type UeCameraIndex,
  type UePresentationState,
  type UeRenderAvailability,
} from './ueBridge'

const SCENE_PRESETS = [
  { value: 'studio', label: '影棚' },
  { value: 'outdoor', label: '外景' },
] as const
type ControlMenu = 'camera' | 'animation' | 'scene'

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

function presentationStateError(bridge: ReflectedUeBridge | null): string {
  if (!bridge) return 'UE 控制桥不可用'
  if (typeof bridge.getpresentationstatejson !== 'function') {
    return 'UE 控制桥缺少所需方法'
  }
  return '无法读取 UE 展示状态'
}

export default function ExperienceControls({ ueEnabled = false }: ExperienceControlsProps) {
  const [openMenu, setOpenMenu] = useState<ControlMenu | null>(null)
  const [cameras, setCameras] = useState<CatalogInteractionCamera[]>([])
  const [animations, setAnimations] = useState<CatalogAnimation[]>([])
  const [cameraId, setCameraId] = useState<CatalogCameraId | null>(null)
  const [cameraIndex, setCameraIndex] = useState<UeCameraIndex | null>(null)
  const [pendingCameraSelection, setPendingCameraSelection] = useState<PendingCameraSelection | null>(null)
  const [animationEnabled, setAnimationEnabled] = useState(false)
  const [animationId, setAnimationId] = useState<string | null>(null)
  const [animationBridgeReady, setAnimationBridgeReady] = useState(false)
  const [lightPreset, setLightPreset] = useState<'studio' | 'outdoor'>('studio')
  const [renderMode, setRenderMode] = useState<'realtime' | 'path-tracing'>('realtime')
  const [renderProgress, setRenderProgress] = useState(0)
  const [renderAvailability, setRenderAvailability] =
    useState<UeRenderAvailability>(ueEnabled ? 'preparing' : 'ready')
  const [fullscreen, setFullscreen] = useState(false)
  const [toolbarIdle, setToolbarIdle] = useState(false)
  const [error, setError] = useState('')
  const idleTimer = useRef<number | null>(null)
  const cameraRequestIdRef = useRef(0)
  const animationRequestIdRef = useRef(0)
  const animationActionRequestIdRef = useRef(0)
  const animationCandidatesRef = useRef<CatalogAnimation[]>([])
  const cefBridgeProbeStartedRef = useRef(false)

  useEffect(() => {
    document.documentElement.classList.add('controls-document')
    document.body.classList.add('controls-document')
    return () => {
      cameraRequestIdRef.current += 1
      animationRequestIdRef.current += 1
      animationActionRequestIdRef.current += 1
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

  const applyState = useCallback((state: UePresentationState) => {
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
    setRenderProgress(state.renderMode === 'path-tracing'
      ? Math.min(1, Math.max(0, state.renderProgress ?? 0))
      : 0)
    setRenderAvailability(state.renderAvailability ?? 'ready')
    setFullscreen(state.fullscreen)
  }, [])

  const refreshAnimationAvailability = useCallback(async (
    candidates = animationCandidatesRef.current,
  ) => {
    if (!ueEnabled || candidates.length === 0) return
    const requestId = animationRequestIdRef.current + 1
    animationRequestIdRef.current = requestId
    const bridge = getUeBridge(true)
    if (!hasUeAnimationBridgeMethod(bridge)) {
      if (animationRequestIdRef.current === requestId) {
        setAnimationBridgeReady(false)
        setAnimations(candidates)
      }
      return
    }
    setAnimationBridgeReady(true)
    const supported: CatalogAnimation[] = []
    for (const animation of candidates) {
      let available = false
      for (let attempt = 0; attempt < 3 && !available; attempt += 1) {
        available = await canPlayUeAnimation(bridge, animation.animationId)
        if (!available && attempt < 2) {
          await new Promise<void>((resolve) => {
            window.setTimeout(resolve, 120)
          })
        }
      }
      if (available) supported.push(animation)
    }
    if (animationRequestIdRef.current !== requestId) return
    // bridge 已就绪时，能力结果逐项生效；一个执行器不支持不能隐藏其他项。
    setAnimations(supported)
  }, [ueEnabled])

  useEffect(() => {
    if (!ueEnabled) return
    let active = true
    const controller = new AbortController()
    const catalogRequest = usesBundledCatalog()
      ? Promise.resolve(bundledCatalog)
      : fetchCatalog(controller.signal)
    void catalogRequest
      .then(async (catalog) => {
        if (!active) return
        setCameras((catalog.interactionCameras ?? [])
          .slice()
          .sort((left, right) => left.order - right.order))
        const animationCandidates = catalog.animations ?? []
        animationCandidatesRef.current = animationCandidates
        setAnimations(animationCandidates)
        await refreshAnimationAvailability(animationCandidates)
      })
      .catch(() => {
        if (active && !controller.signal.aborted) setError('无法读取镜头目录')
      })
    return () => {
      active = false
      controller.abort()
    }
  }, [applyState, refreshAnimationAvailability, ueEnabled])

  useEffect(() => {
    if (!ueEnabled) return
    let active = true
    let timer: number | null = null
    const probeBridge = async () => {
      const bridge = getUeBridge(true)
      const state = await getUePresentationState(bridge)
      if (!active) return
      if (state) {
        applyState(state)
        if (hasUeAnimationBridgeMethod(bridge)) {
          setAnimationBridgeReady(true)
          setError('')
          await refreshAnimationAvailability()
          return
        }
        setAnimationBridgeReady(false)
        setError((current) => current || 'UE 控制桥缺少所需方法')
      } else {
        setAnimationBridgeReady(false)
        setError((current) => current || presentationStateError(bridge))
      }
      timer = window.setTimeout(() => void probeBridge(), 150)
    }
    void probeBridge()
    return () => {
      active = false
      if (timer !== null) window.clearTimeout(timer)
    }
  }, [applyState, refreshAnimationAvailability, ueEnabled])

  useEffect(() => {
    if (!ueEnabled
      || !animationBridgeReady
      || animations.length === 0
      || cefBridgeProbeStartedRef.current
      || new URLSearchParams(window.location.search).get('cefBridgeProbe') !== '1') {
      return
    }
    const bridge = getUeBridge(true)
    if (!bridge) return
    cefBridgeProbeStartedRef.current = true
    void runCefBridgeProbe(bridge, animations)
  }, [animationBridgeReady, animations, ueEnabled])

  useEffect(() => {
    if (!ueEnabled
      || (renderMode !== 'path-tracing' && renderAvailability !== 'preparing')) return
    let active = true
    const updateProgress = async () => {
      const state = await getUePresentationState(getUeBridge(true))
      if (active && state) applyState(state)
    }
    void updateProgress()
    const timer = window.setInterval(() => {
      void updateProgress()
    }, 150)
    return () => {
      active = false
      window.clearInterval(timer)
    }
  }, [applyState, renderAvailability, renderMode, ueEnabled])

  const run = async (
    createCommand: UeControlCommand | ((state: UePresentationState) => UeControlCommand),
    onSuccess?: (state: UePresentationState) => void,
  ) => {
    if (!ueEnabled) return false
    const bridge = getUeBridge(true)
    const currentState = await getUePresentationState(bridge)
    if (!currentState) {
      setError(presentationStateError(bridge))
      return false
    }
    applyState(currentState)
    const command = typeof createCommand === 'function'
      ? createCommand(currentState)
      : createCommand
    const accepted = await executeUeControl(bridge, command)
    if (accepted) {
      setError('')
    } else if (command.type === 'render') {
      setError(
        await getUeRenderModeError(bridge)
          || '当前设备无法切换到 Path Tracing',
      )
    } else {
      setError(getUeControlFailureMessage(getUeControlFailureKind(bridge, command)))
    }
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
    setError(accepted
      ? ''
      : getUeControlFailureMessage(getUeControlFailureKind(bridge, {
        type: 'camera',
        cameraId: camera.cameraId,
        legacyIndex: camera.legacyIndex,
      })))
    if (!accepted) {
      setPendingCameraSelection(null)
      return
    }
    setOpenMenu(null)
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

  const selectAnimation = async (
    nextAnimationId: string,
    toggleCurrent = true,
  ) => {
    if (!ueEnabled) return
    const bridge = getUeBridge(true)
    const requestId = animationActionRequestIdRef.current + 1
    animationActionRequestIdRef.current = requestId
    const currentState = await getUePresentationState(bridge)
    if (animationActionRequestIdRef.current !== requestId) return
    if (!currentState) {
      setError(presentationStateError(bridge))
      return
    }
    applyState(currentState)
    const currentAnimationId = currentState.animationId ?? null
    const nextId = toggleCurrent && currentAnimationId === nextAnimationId
      ? null
      : nextAnimationId
    const accepted = await focusUeAnimation(
      bridge,
      currentAnimationId,
      nextId,
    )
    if (animationActionRequestIdRef.current !== requestId) return
    setError(accepted
      ? ''
      : getUeControlFailureMessage(
        !bridge
          ? 'bridge-unavailable'
          : hasUeAnimationBridgeMethod(bridge)
            ? 'command-rejected'
            : 'method-unavailable',
      ))
    if (accepted) {
      setAnimationId(nextId)
      setAnimationEnabled(nextId !== null)
      setOpenMenu(null)
      const updatedState = await getUePresentationState(bridge)
      if (animationActionRequestIdRef.current === requestId && updatedState) {
        applyState(updatedState)
      }
    }
  }

  const selectScene = (preset: 'studio' | 'outdoor') => {
    void run(
      { type: 'light', preset },
      () => {
        setLightPreset(preset)
        setOpenMenu(null)
      },
    )
  }

  const selectRenderMode = (mode: 'realtime' | 'path-tracing') => {
    if (mode === 'path-tracing' && renderAvailability === 'preparing') return
    void run(
      { type: 'render', mode },
      () => setRenderMode(mode),
    )
  }

  const selectDisplayMode = (enabled: boolean) => {
    void run(
      { type: 'fullscreen', enabled },
      () => setFullscreen(enabled),
    )
  }

  const cycleCamera = () => {
    if (cameras.length === 0) return
    const selectedId = pendingCameraSelection?.cameraId ?? cameraId
    const currentIndex = cameras.findIndex((camera) => camera.cameraId === selectedId)
    void selectCamera(cameras[(currentIndex + 1 + cameras.length) % cameras.length])
  }

  const playPrimaryAnimation = () => {
    const wheelAnimation = animations.find(
      (animation) => animation.animationId === 'wheel-spin',
    )
    if (!wheelAnimation) return
    void selectAnimation(wheelAnimation.animationId, false)
  }

  return (
    <main
      className={`controls-view ${fullscreen ? 'fullscreen' : ''} ${toolbarIdle ? 'toolbar-idle' : ''}`}
      onPointerMove={keepToolbarAwake}
    >
      <nav className="experience-toolbar" aria-label="体验控制">
        <div
          className="toolbar-item"
          onMouseEnter={() => setOpenMenu('camera')}
          onMouseLeave={() => setOpenMenu(null)}
        >
          <button
            aria-expanded={openMenu === 'camera'}
            onClick={cycleCamera}
          >
            <span aria-hidden="true">◉</span>
            镜头
          </button>
          {openMenu === 'camera' && (
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
                  {camera.displayName}
                </button>
              ))}
            </div>
          )}
        </div>
        {animations.length > 0 && <div
          className="toolbar-item"
          onMouseEnter={() => {
            setOpenMenu('animation')
            void refreshAnimationAvailability()
          }}
          onMouseLeave={() => setOpenMenu(null)}
        >
          <button
            aria-expanded={openMenu === 'animation'}
            aria-pressed={animationEnabled}
            disabled={!animationBridgeReady}
            title={!animationBridgeReady ? 'UE 动画控制尚未就绪' : undefined}
            onClick={playPrimaryAnimation}
          >
            <span aria-hidden="true">▷</span>
            动画
          </button>
          {openMenu === 'animation' && (
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
        </div>}
        <div
          className="toolbar-item"
          onMouseEnter={() => setOpenMenu('scene')}
          onMouseLeave={() => setOpenMenu(null)}
        >
          <button
            aria-expanded={openMenu === 'scene'}
            aria-pressed={lightPreset === 'outdoor'}
            onClick={() => selectScene(lightPreset === 'studio' ? 'outdoor' : 'studio')}
          >
            <span aria-hidden="true">☼</span>
            场景
          </button>
          {openMenu === 'scene' && (
            <div className="control-popover compact-popover" role="menu" aria-label="场景预设">
              {SCENE_PRESETS.map((preset) => (
                <button
                  key={preset.value}
                  role="menuitemradio"
                  aria-checked={lightPreset === preset.value}
                  onClick={() => selectScene(preset.value)}
                >
                  {preset.label}
                </button>
              ))}
            </div>
          )}
        </div>
        <button
          className="path-tracing-toggle"
          aria-pressed={renderMode === 'path-tracing'}
          aria-busy={renderAvailability === 'preparing'}
          aria-label="渲染"
          disabled={renderAvailability === 'preparing'}
          title={renderAvailability === 'preparing'
            ? 'Path Tracing 正在准备'
            : undefined}
          onClick={() => selectRenderMode(
            renderMode === 'realtime' ? 'path-tracing' : 'realtime',
          )}
        >
          {renderAvailability === 'preparing'
            ? (
              <svg className="render-warmup-loader" viewBox="0 0 24 24" aria-hidden="true">
                <polygon points="12,1 15,5 12,9 9,5" />
                <polygon points="23,12 19,15 15,12 19,9" />
                <polygon points="12,23 9,19 12,15 15,19" />
                <polygon points="1,12 5,9 9,12 5,15" />
              </svg>
            )
            : renderMode === 'path-tracing'
            ? (
              <svg className="render-progress" viewBox="0 0 24 24" aria-hidden="true">
                <circle className="render-progress-track" cx="12" cy="12" r="9" pathLength="100" />
                <circle
                  className="render-progress-value"
                  cx="12"
                  cy="12"
                  r="9"
                  pathLength="100"
                  style={{ strokeDashoffset: 100 - renderProgress * 100 }}
                />
              </svg>
            )
            : <span aria-hidden="true">◇</span>}
          渲染
        </button>
        <button
          aria-pressed={fullscreen}
          onClick={() => selectDisplayMode(!fullscreen)}
        >
          <span aria-hidden="true">□</span>
          全屏
        </button>
      </nav>
      {error && <p className="bridge-error" role="alert">{error}</p>}
    </main>
  )
}
