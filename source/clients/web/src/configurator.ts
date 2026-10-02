import { PART_ORDER, type Catalog, type Selections } from './types'

export function createInitialSelections(catalog: Catalog): Selections {
  return Object.fromEntries(
    PART_ORDER.map((partId) => {
      const part = catalog.parts.find((item) => item.partId === partId)
      if (!part?.options[0]) {
        throw new Error(`分区 ${partId} 没有可用选项`)
      }
      return [partId, part.options[0].optionId]
    }),
  ) as Selections
}

export function createCanonicalKey(selections: Selections): string {
  return PART_ORDER.map((partId) => selections[partId]).join('__')
}

export function calculateTotalPrice(catalog: Catalog, selections: Selections): number {
  return catalog.parts.reduce((total, part) => {
    const selected = part.options.find(
      (option) => option.optionId === selections[part.partId],
    )
    return total + (selected?.priceDeltaMinor ?? 0)
  }, catalog.vehicle.basePriceMinor)
}

export function formatPrice(minor: number, showSign = false): string {
  if (showSign && minor === 0) return '已包含'
  const amount = new Intl.NumberFormat('zh-CN', {
    style: 'currency',
    currency: 'CNY',
    maximumFractionDigits: 0,
  }).format(minor / 100)
  return showSign ? `+${amount}` : amount
}
