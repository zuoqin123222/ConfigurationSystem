import {
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
  type KeyboardEvent,
  type PointerEvent,
  type SyntheticEvent,
} from 'react'
import type { CatalogMaterialVariant, CatalogOption } from './types'

export interface MaterialStripChoice {
  option: CatalogOption
  variant: CatalogMaterialVariant
}

interface MaterialColorStripProps {
  familyName: string
  choices: MaterialStripChoice[]
  selectedOptionId?: string
  selectedVariantId?: string
  selectedDefault?: {
    name: string
    price: string
    imageUrl: string
  }
  formatPrice: (option: CatalogOption) => string
  resolveImageUrl: (url: string) => string | undefined
  onCommit: (choice: MaterialStripChoice) => void
  onImageError: (event: SyntheticEvent<HTMLImageElement>) => void
}

function priceGroupKey(option: CatalogOption): string {
  const { unitPriceMinor, quantity, pricingUnit, status } = option.pricing
  return `${unitPriceMinor ?? 'unknown'}:${quantity ?? 'unknown'}:${pricingUnit}:${status}`
}

function choiceColor(choice: MaterialStripChoice): string {
  return choice.variant.ui?.sortColorHex ?? '#777a74'
}

function stripGradient(choices: MaterialStripChoice[]): string {
  if (choices.length === 0) return '#777a74'
  const stops = choices.flatMap((choice, index) => {
    const start = (index / choices.length) * 100
    const end = ((index + 1) / choices.length) * 100
    const color = choiceColor(choice)
    return [`${color} ${start}%`, `${color} ${end}%`]
  })
  return `linear-gradient(90deg, ${stops.join(', ')})`
}

export default function MaterialColorStrip({
  familyName,
  choices,
  selectedOptionId,
  selectedVariantId,
  selectedDefault,
  formatPrice,
  resolveImageUrl,
  onCommit,
  onImageError,
}: MaterialColorStripProps) {
  const groups = useMemo(() => {
    const result: Array<{ key: string; option: CatalogOption; choices: MaterialStripChoice[] }> = []
    for (const choice of choices) {
      const key = priceGroupKey(choice.option)
      const existing = result.find((group) => group.key === key)
      if (existing) existing.choices.push(choice)
      else result.push({ key, option: choice.option, choices: [choice] })
    }
    return result
  }, [choices])

  const committedChoice = choices.find(({ option, variant }) =>
    option.optionId === selectedOptionId && variant.variantId === selectedVariantId)
  const fallbackChoice = committedChoice ?? choices[0]
  const summary = selectedDefault ?? (fallbackChoice
    ? {
        name: fallbackChoice.variant.displayName,
        price: formatPrice(fallbackChoice.option),
        imageUrl: fallbackChoice.variant.thumbnailUrl,
      }
    : null)

  return (
    <div className="material-strip-layout">
      {summary && (
        <div className="material-strip-summary" aria-live="polite">
          <div className="material-strip-copy">
            <strong>{summary.name}</strong>
            <span>{familyName}</span>
            <small>{summary.price}</small>
          </div>
          <img
            src={resolveImageUrl(summary.imageUrl)}
            alt={`${summary.name}材质实拍`}
            onError={onImageError}
          />
        </div>
      )}
      <div className="material-strip-groups">
        {groups.map((group, groupIndex) => {
          const selectedIndex = group.choices.findIndex(({ option, variant }) =>
            option.optionId === selectedOptionId && variant.variantId === selectedVariantId)
          return (
            <MaterialStripRange
              key={group.key}
              label={groups.length > 1 ? `价格组 ${groupIndex + 1}` : '色彩'}
              choices={group.choices}
              selectedIndex={selectedIndex}
              price={formatPrice(group.option)}
              onCommit={onCommit}
            />
          )
        })}
      </div>
    </div>
  )
}

interface MaterialStripRangeProps {
  label: string
  choices: MaterialStripChoice[]
  selectedIndex: number
  price: string
  onCommit: (choice: MaterialStripChoice) => void
}

function MaterialStripRange({
  label,
  choices,
  selectedIndex,
  price,
  onCommit,
}: MaterialStripRangeProps) {
  const initialIndex = selectedIndex >= 0 ? selectedIndex : 0
  const [draftIndex, setDraftIndex] = useState(initialIndex)
  const draftIndexRef = useRef(initialIndex)

  useEffect(() => {
    const nextIndex = selectedIndex >= 0 ? selectedIndex : 0
    draftIndexRef.current = nextIndex
    setDraftIndex(nextIndex)
  }, [selectedIndex])

  const setDraft = (value: string) => {
    const nextIndex = Math.max(0, Math.min(choices.length - 1, Number(value)))
    draftIndexRef.current = nextIndex
    setDraftIndex(nextIndex)
  }
  const commit = () => {
    const choice = choices[draftIndexRef.current]
    if (choice) onCommit(choice)
  }
  const handlePointerUp = (event: PointerEvent<HTMLInputElement>) => {
    event.currentTarget.releasePointerCapture?.(event.pointerId)
    commit()
  }
  const handleKeyUp = (event: KeyboardEvent<HTMLInputElement>) => {
    if (['ArrowLeft', 'ArrowRight', 'Home', 'End', 'PageUp', 'PageDown'].includes(event.key)) {
      commit()
    }
  }
  const position = choices.length <= 1 ? 50 : (draftIndex / (choices.length - 1)) * 100
  const draftChoice = choices[draftIndex]

  return (
    <div className={`material-strip-control ${selectedIndex >= 0 ? 'selected' : ''}`}>
      <div className="material-strip-label">
        <span>{label}</span>
        <small>{price}</small>
      </div>
      <div
        className="material-strip-track"
        style={{
          '--material-strip-gradient': stripGradient(choices),
          '--material-strip-position': `${position}%`,
          '--material-strip-color': choiceColor(draftChoice),
        } as CSSProperties}
      >
        <input
          type="range"
          min={0}
          max={Math.max(0, choices.length - 1)}
          step={1}
          value={draftIndex}
          aria-label={`${label}，拖动选择材质颜色`}
          aria-valuetext={draftChoice?.variant.displayName}
          onInput={(event) => setDraft(event.currentTarget.value)}
          onChange={(event) => setDraft(event.currentTarget.value)}
          onPointerUp={handlePointerUp}
          onPointerCancel={commit}
          onKeyUp={handleKeyUp}
          onBlur={commit}
        />
        <span className="material-strip-indicator" aria-hidden="true" />
      </div>
      <div className="material-strip-draft-name">{draftChoice?.variant.displayName}</div>
    </div>
  )
}
