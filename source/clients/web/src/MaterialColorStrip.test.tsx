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
      choices={choices}
      selectedOptionId={selected.option.optionId}
      selectedVariantId={selected.materialVariantId}
      onCommit={(choice) => {
        onCommit(choice)
        setSelected(choice)
      }}
    />
  )
}

describe('MaterialColorStrip', () => {
  it('默认免费材质被选中时色彩条指向默认项且不重复摘要', () => {
    render(
      <MaterialColorStrip
        choices={choices}
        selectedOptionId="seat-ultrasuede-black"
        onCommit={() => undefined}
      />,
    )

    expect(screen.getAllByRole('slider')[0]).toHaveAttribute(
      'aria-valuetext',
      '奥司维（黑）',
    )
    expect(screen.queryByRole('img')).not.toBeInTheDocument()
    expect(screen.queryByText('免费')).not.toBeInTheDocument()
    expect(document.querySelector<HTMLElement>('.material-strip-track')?.style
      .getPropertyValue('--material-strip-position-ratio')).toBe('0')
  })

  it('首尾选择使用 0 到 1 的轨道内位置比例', () => {
    const { rerender } = render(
      <MaterialColorStrip
        choices={choices}
        selectedOptionId="seat-ultrasuede-black"
        onCommit={() => undefined}
      />,
    )
    expect(document.querySelector<HTMLElement>('.material-strip-track')?.style
      .getPropertyValue('--material-strip-position-ratio')).toBe('0')

    rerender(
      <MaterialColorStrip
        choices={choices}
        selectedOptionId="seat-ultrasuede-main"
        selectedVariantId="ultrasuede-red"
        onCommit={() => undefined}
      />,
    )
    expect(document.querySelector<HTMLElement>('.material-strip-track')?.style
      .getPropertyValue('--material-strip-position-ratio')).toBe('1')
  })

  it('按价格拆分无前置文字的全宽色彩条并保留传入顺序', () => {
    render(<Harness onCommit={() => undefined} />)

    expect(screen.getAllByRole('slider')).toHaveLength(2)
    expect(screen.queryByText('价格组 1')).not.toBeInTheDocument()
    expect(screen.queryByText('价格组 2')).not.toBeInTheDocument()
    expect(document.querySelector('.material-strip-label')).toBeNull()
    expect(screen.getAllByRole('slider')[0]).toHaveAttribute('aria-valuetext', 'Black UF7')
    expect(screen.getAllByRole('slider')[1]).toHaveAttribute('aria-valuetext', 'Blue UB8')
    expect(screen.queryByRole('img')).not.toBeInTheDocument()
  })

  it('色彩条数值变化时立即提交并更新当前色值', () => {
    const onCommit = vi.fn()
    render(<Harness onCommit={onCommit} />)
    const firstStrip = screen.getAllByRole('slider')[0]

    fireEvent.input(firstStrip, { target: { value: '2' } })
    expect(onCommit).toHaveBeenCalledWith(choices[2])
    expect(firstStrip).toHaveAttribute('aria-valuetext', 'Red US3')
  })
})
