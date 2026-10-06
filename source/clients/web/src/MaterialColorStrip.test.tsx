import { fireEvent, render, screen } from '@testing-library/react'
import { useState } from 'react'
import { describe, expect, it, vi } from 'vitest'
import MaterialColorStrip, { type MaterialStripChoice } from './MaterialColorStrip'
import type { CatalogMaterialVariant, CatalogOption } from './types'

function option(optionId: string, unitPriceMinor: number): CatalogOption {
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
      isStandard: false,
      status: 'confirmed',
      quotable: false,
    },
    reviewRequired: false,
    thumbnailUrl: null,
    ui: { control: 'material-strip' },
  }
}

function variant(
  variantId: string,
  displayName: string,
  sortColorHex: string,
): CatalogMaterialVariant {
  return {
    variantId,
    materialFamilyId: 'ultrasuede',
    displayName,
    colorCode: displayName,
    ui: { sortColorHex },
    thumbnailUrl: `/sc01/thumbnails/${variantId}.webp`,
    reviewRequired: false,
  }
}

const mainOption = option('seat-ultrasuede-main', 118000)
const premiumOption = option('seat-ultrasuede-premium', 168000)
const choices: MaterialStripChoice[] = [
  { option: mainOption, variant: variant('ultrasuede-black', 'Black UF7', '#151515') },
  { option: mainOption, variant: variant('ultrasuede-red', 'Red US3', '#8D2429') },
  { option: premiumOption, variant: variant('ultrasuede-blue', 'Blue UB8', '#263E5C') },
]

function Harness({ onCommit }: { onCommit: (choice: MaterialStripChoice) => void }) {
  const [selected, setSelected] = useState(choices[0])
  return (
    <MaterialColorStrip
      familyName="奥司维"
      choices={choices}
      selectedOptionId={selected.option.optionId}
      selectedVariantId={selected.variant.variantId}
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
        selectedDefault={{
          name: '奥司维（黑）',
          price: '免费',
          imageUrl: '/sc01/thumbnails/ultrasuede-black-default.webp',
        }}
        formatPrice={() => '¥1,180'}
        resolveImageUrl={(url) => url}
        onImageError={() => undefined}
        onCommit={() => undefined}
      />,
    )

    expect(screen.getByRole('img', { name: '奥司维（黑）材质实拍' }))
      .toHaveAttribute('src', '/sc01/thumbnails/ultrasuede-black-default.webp')
    expect(screen.getByText('免费')).toBeInTheDocument()
  })

  it('按价格拆分色彩条并保留传入顺序', () => {
    render(<Harness onCommit={() => undefined} />)

    expect(screen.getAllByRole('slider')).toHaveLength(2)
    expect(screen.getByText('价格组 1')).toBeInTheDocument()
    expect(screen.getByText('价格组 2')).toBeInTheDocument()
    expect(screen.getByRole('img', { name: 'Black UF7材质实拍' }))
      .toHaveAttribute('src', '/sc01/thumbnails/ultrasuede-black.webp')
  })

  it('拖动时只移动指示器，松开后才提交并更新实拍预览', () => {
    const onCommit = vi.fn()
    render(<Harness onCommit={onCommit} />)
    const firstStrip = screen.getAllByRole('slider')[0]

    fireEvent.input(firstStrip, { target: { value: '1' } })
    expect(screen.getByRole('img', { name: 'Black UF7材质实拍' })).toBeInTheDocument()
    expect(screen.queryByRole('img', { name: 'Red US3材质实拍' })).not.toBeInTheDocument()
    expect(onCommit).not.toHaveBeenCalled()

    fireEvent.pointerUp(firstStrip, { pointerId: 1 })
    expect(onCommit).toHaveBeenCalledWith(choices[1])
    expect(screen.getByRole('img', { name: 'Red US3材质实拍' })).toBeInTheDocument()
  })
})
