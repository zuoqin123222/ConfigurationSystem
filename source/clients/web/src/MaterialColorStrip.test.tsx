import { fireEvent, render, screen } from '@testing-library/react'
import { useState } from 'react'
import { describe, expect, it, vi } from 'vitest'
import MaterialColorStrip, { type MaterialStripChoice } from './MaterialColorStrip'
import type { CatalogOption } from './types'

function option(optionId: string, unitPriceMinor: number, isStandard = false): CatalogOption {
  return {
    optionId,
    surfaceId: 'seat-backrest',
    materialFamilyId: 'ultrasuede',
    displayName: '奥司维',
    renderRelevant: true,
    colorCode: null,
    finish: null,
    parameters: {
      color: { mode: 'variant', value: null, required: true },
      material: { materialFamilyId: 'ultrasuede', variantId: null },
    },
    pricing: {
      unitPriceMinor,
      quantity: 1,
      pricingUnit: 'per-seat',
      isStandard,
      status: 'confirmed',
      quotable: false,
    },
    reviewRequired: false,
    thumbnailUrl: null,
    ui: { control: 'material-strip' },
  }
}

function choice(
  optionValue: CatalogOption,
  choiceId: string,
  displayName: string,
  colorHex: string,
  materialVariantId?: string,
): MaterialStripChoice {
  return {
    option: optionValue,
    choiceId,
    displayName,
    colorHex,
    imageUrl: `/sc01/thumbnails/${choiceId}.webp`,
    materialVariantId,
  }
}

const standardOption = option('seat-ultrasuede-black', 0, true)
const mainOption = option('seat-ultrasuede-main', 118000)
const premiumOption = option('seat-ultrasuede-premium', 168000)
const choices: MaterialStripChoice[] = [
  choice(standardOption, 'ultrasuede-black-default', '奥司维（黑）', '#111111'),
  choice(mainOption, 'ultrasuede-black', 'Black UF7', '#151515', 'ultrasuede-black'),
  choice(mainOption, 'ultrasuede-red', 'Red US3', '#8D2429', 'ultrasuede-red'),
  choice(premiumOption, 'ultrasuede-blue', 'Blue UB8', '#263E5C', 'ultrasuede-blue'),
]

function Harness({ onCommit }: { onCommit: (choice: MaterialStripChoice) => void }) {
  const [selected, setSelected] = useState(choices[1])
  return (
    <MaterialColorStrip
      familyName="奥司维"
      choices={choices}
      selectedOptionId={selected.option.optionId}
      selectedVariantId={selected.materialVariantId}
      formatPrice={(item) => `¥${(item.pricing.unitPriceMinor ?? 0) / 100}`}
      resolveImageUrl={(url) => url}
      onImageError={() => undefined}
      onCommit={(choice) => {
        onCommit(choice)
        setSelected(choice)
      }}
    />
  )
}

describe('MaterialColorStrip', () => {
  it('默认免费材质被选中时优先显示默认项摘要', () => {
    render(
      <MaterialColorStrip
        familyName="奥司维"
        choices={choices}
        selectedOptionId="seat-ultrasuede-black"
        formatPrice={(item) => item.pricing.isStandard ? '免费' : '¥1,180'}
        resolveImageUrl={(url) => url}
        onImageError={() => undefined}
        onCommit={() => undefined}
      />,
    )

    expect(screen.getByRole('img', { name: '奥司维（黑）材质实拍' }))
      .toHaveAttribute('src', '/sc01/thumbnails/ultrasuede-black-default.webp')
    expect(screen.getAllByText('免费')).toHaveLength(2)
  })

  it('按价格拆分色彩条并保留传入顺序', () => {
    render(<Harness onCommit={() => undefined} />)

    expect(screen.getAllByRole('slider')).toHaveLength(2)
    expect(screen.getByText('价格组 1')).toBeInTheDocument()
    expect(screen.getByText('价格组 2')).toBeInTheDocument()
    expect(screen.getByRole('img', { name: 'Black UF7材质实拍' }))
      .toHaveAttribute('src', '/sc01/thumbnails/ultrasuede-black.webp')
  })

  it('色彩条数值变化时立即提交并更新实拍预览', () => {
    const onCommit = vi.fn()
    render(<Harness onCommit={onCommit} />)
    const firstStrip = screen.getAllByRole('slider')[0]

    fireEvent.input(firstStrip, { target: { value: '2' } })
    expect(onCommit).toHaveBeenCalledWith(choices[2])
    expect(screen.getByRole('img', { name: 'Red US3材质实拍' })).toBeInTheDocument()
  })
})
