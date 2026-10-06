import { useCallback, useEffect, useRef, useState } from 'react'
import { fetchCatalog } from './api'
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
  getUePresentationState,
  getUeRenderModeError,
  type UeControlCommand,
  type UeCameraIndex,
  type UePresentationState,
} from './ueBridge'

const SCENE_PRESETS = [
  { value: 'studio', label: '影棚' },
  { value: 'outdoor', label: '外景' },
] as const
const RENDER_MODES = [
  { value: 'realtime', label: '实时' },
  { value: 'path-tracing', label: 'Path Tracing' },
] as const
const DISPLAY_MODES = [
  { value: false, label: '窗口' },
  { value: true, label: '全屏' },
] as const
type ControlMenu = 'camera' | 'animation' | 'scene' | 'render' | 'display'

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
  const [openMenu, setOpenMenu] = useState<ControlMenu | null>(null)
  const [cameras, setCameras] = useState<CatalogInteractionCamera[]>([])
  const [animations, setAnimations] = useState<CatalogAnimation[]>([])
  const [cameraId, setCameraId] = useState<CatalogCameraId | null>(null)
  const [cameraIndex, setCameraIndex] = useState<UeCameraIndex | null>(null)
  const [pendingCameraSelection, setPendingCameraSelection] = useState<PendingCameraSelection | null>(null)
  const [animationEnabled, setAnimationEnabled] = useState(false)
  const [animationId, setAnimationId] = useState<string | null>(null)
  const [lightPreset, setLightPreset] = useState<'studio' | 'outdoor'>('studio')
  const [renderMode, setRenderMode] = useState<'realtime' | 'path-tracing'>('realtime')
  const [renderProgress, setRenderProgress] = useState(0)
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
    setFullscreen(state.fullscreen)
  }, [])

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
        const availability = await Promise.all(animationCandidates.map(
          (animation) => canPlayUeAnimation(
            getUeBridge(true),
            animation.animationId,
          ),
        ))
        if (active) {
          setAnimations(animationCandidates.filter((_, index) => availability[index]))
        }
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
  }, [applyState, ueEnabled])

  useEffect(() => {
    if (!ueEnabled || renderMode !== 'path-tracing') return
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
  }, [applyState, renderMode, ueEnabled])

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
    if (accepted) {
      setError('')
    } else if (command.type === 'render') {
      setError(
        await getUeRenderModeError(bridge)
          || '当前设备无法切换到 Path Tracing',
      )
    } else {
      setError('UE 控制桥不可用或命令被拒绝')
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
    setError(accepted ? '' : 'UE 控制桥不可用或命令被拒绝')
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
      setOpenMenu(null)
      const updatedState = await getUePresentationState(bridge)
      if (updatedState) applyState(updatedState)
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
    void run(
      { type: 'render', mode },
      () => {
        setRenderMode(mode)
        setOpenMenu(null)
      },
    )
  }

  const selectDisplayMode = (enabled: boolean) => {
    void run(
      { type: 'fullscreen', enabled },
      () => {
        setFullscreen(enabled)
        setOpenMenu(null)
      },
    )
  }

  const cycleCamera = () => {
    if (cameras.length === 0) return
    const selectedId = pendingCameraSelection?.cameraId ?? cameraId
    const currentIndex = cameras.findIndex((camera) => camera.cameraId === selectedId)
    void selectCamera(cameras[(currentIndex + 1 + cameras.length) % cameras.length])
  }

  const cycleAnimation = () => {
    if (animations.length === 0) return
    const currentIndex = animations.findIndex((animation) => animation.animationId === animationId)
    void selectAnimation(animations[(currentIndex + 1 + animations.length) % animations.length].animationId)
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
          onMouseEnter={() => setOpenMenu('animation')}
          onMouseLeave={() => setOpenMenu(null)}
        >
          <button
            aria-expanded={openMenu === 'animation'}
            aria-pressed={animationEnabled}
            onClick={cycleAnimation}
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
        <div
          className="toolbar-item"
          onMouseEnter={() => setOpenMenu('render')}
          onMouseLeave={() => setOpenMenu(null)}
        >
          <button
            className="path-tracing-toggle"
            aria-expanded={openMenu === 'render'}
            aria-pressed={renderMode === 'path-tracing'}
            aria-label="渲染"
            onClick={() => selectRenderMode(
              renderMode === 'realtime' ? 'path-tracing' : 'realtime',
            )}
          >
            {renderMode === 'path-tracing'
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
          {openMenu === 'render' && (
            <div className="control-popover compact-popover" role="menu" aria-label="渲染模式">
              {RENDER_MODES.map((mode) => (
                <button
                  key={mode.value}
                  role="menuitemradio"
                  aria-checked={renderMode === mode.value}
                  onClick={() => selectRenderMode(mode.value)}
                >
                  {mode.label}
                </button>
              ))}
            </div>
          )}
        </div>
        <div
          className="toolbar-item"
          onMouseEnter={() => setOpenMenu('display')}
          onMouseLeave={() => setOpenMenu(null)}
        >
          <button
            aria-expanded={openMenu === 'display'}
            aria-pressed={fullscreen}
            onClick={() => selectDisplayMode(!fullscreen)}
          >
            <span aria-hidden="true">□</span>
            全屏
          </button>
          {openMenu === 'display' && (
            <div className="control-popover compact-popover" role="menu" aria-label="显示模式">
              {DISPLAY_MODES.map((mode) => (
                <button
                  key={String(mode.value)}
                  role="menuitemradio"
                  aria-checked={fullscreen === mode.value}
                  onClick={() => selectDisplayMode(mode.value)}
                >
                  {mode.label}
                </button>
              ))}
            </div>
          )}
        </div>
      </nav>
      {error && <p className="bridge-error" role="alert">{error}</p>}
    </main>
  )
}
