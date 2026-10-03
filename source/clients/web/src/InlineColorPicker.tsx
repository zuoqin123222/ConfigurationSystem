import { useMemo, useRef, type CSSProperties, type PointerEvent as ReactPointerEvent } from 'react'

interface HsvColor {
  h: number
  s: number
  v: number
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.min(maximum, Math.max(minimum, value))
}

function hexToHsv(hex: string): HsvColor {
  const normalized = hex.replace('#', '').padEnd(6, '0').slice(0, 6)
  const red = Number.parseInt(normalized.slice(0, 2), 16) / 255
  const green = Number.parseInt(normalized.slice(2, 4), 16) / 255
  const blue = Number.parseInt(normalized.slice(4, 6), 16) / 255
  const maximum = Math.max(red, green, blue)
  const minimum = Math.min(red, green, blue)
  const delta = maximum - minimum
  let hue = 0

  if (delta > 0) {
    if (maximum === red) hue = 60 * (((green - blue) / delta) % 6)
    else if (maximum === green) hue = 60 * ((blue - red) / delta + 2)
    else hue = 60 * ((red - green) / delta + 4)
  }

  return {
    h: hue < 0 ? hue + 360 : hue,
    s: maximum === 0 ? 0 : delta / maximum,
    v: maximum,
  }
}

function hsvToHex({ h, s, v }: HsvColor): string {
  const chroma = v * s
  const section = h / 60
  const secondary = chroma * (1 - Math.abs((section % 2) - 1))
  const offset = v - chroma
  let red = 0
  let green = 0
  let blue = 0

  if (section < 1) [red, green, blue] = [chroma, secondary, 0]
  else if (section < 2) [red, green, blue] = [secondary, chroma, 0]
  else if (section < 3) [red, green, blue] = [0, chroma, secondary]
  else if (section < 4) [red, green, blue] = [0, secondary, chroma]
  else if (section < 5) [red, green, blue] = [secondary, 0, chroma]
  else [red, green, blue] = [chroma, 0, secondary]

  return `#${[red, green, blue]
    .map((channel) => Math.round((channel + offset) * 255).toString(16).padStart(2, '0'))
    .join('')
    .toUpperCase()}`
}

export default function InlineColorPicker({
  value,
  onChange,
}: {
  value: string
  onChange: (value: string) => void
}) {
  const planeRef = useRef<HTMLDivElement>(null)
  const hsv = useMemo(() => hexToHsv(value), [value])

  const updatePlane = (event: ReactPointerEvent<HTMLDivElement>) => {
    const plane = planeRef.current
    if (!plane) return
    const rect = plane.getBoundingClientRect()
    const saturation = clamp((event.clientX - rect.left) / rect.width, 0, 1)
    const brightness = 1 - clamp((event.clientY - rect.top) / rect.height, 0, 1)
    onChange(hsvToHex({ h: hsv.h, s: saturation, v: brightness }))
  }

  const handlePointerDown = (event: ReactPointerEvent<HTMLDivElement>) => {
    event.currentTarget.setPointerCapture(event.pointerId)
    updatePlane(event)
  }

  const handlePointerMove = (event: ReactPointerEvent<HTMLDivElement>) => {
    if (event.currentTarget.hasPointerCapture(event.pointerId)) updatePlane(event)
  }

  return (
    <div className="inline-color-picker">
      <div
        ref={planeRef}
        className="color-picker-plane"
        role="slider"
        tabIndex={0}
        aria-label="车漆颜色饱和度和亮度"
        aria-valuetext={`${Math.round(hsv.s * 100)}% 饱和度，${Math.round(hsv.v * 100)}% 亮度`}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        style={{ '--picker-hue': `hsl(${hsv.h} 100% 50%)` } as CSSProperties}
      >
        <span
          className="color-picker-cursor"
          style={{
            left: `${hsv.s * 100}%`,
            top: `${(1 - hsv.v) * 100}%`,
            background: value,
          }}
        />
      </div>
      <label className="color-picker-hue">
        <span>色相</span>
        <input
          aria-label="车漆色相"
          type="range"
          min="0"
          max="360"
          step="1"
          value={Math.round(hsv.h)}
          onChange={(event) => onChange(hsvToHex({
            ...hsv,
            h: Number(event.target.value),
          }))}
        />
      </label>
    </div>
  )
}
