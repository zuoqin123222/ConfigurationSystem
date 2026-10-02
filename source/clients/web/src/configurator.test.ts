import { describe, expect, it } from 'vitest'
import {
  calculateTotalPrice,
  createCanonicalKey,
  createInitialSelections,
  formatPrice,
} from './configurator'
import { catalogFixture } from './test/catalogFixture'

describe('选配核心逻辑', () => {
  it('按固定分区顺序生成初始配置和 canonical key', () => {
    const selections = createInitialSelections({
      ...catalogFixture,
      parts: [...catalogFixture.parts].reverse(),
    })

    expect(selections).toEqual({
      paint: 'paint-red',
      wheel: 'wheel-sport',
      interior: 'interior-dark',
      frame: 'frame-black',
    })
    expect(createCanonicalKey(selections)).toBe(
      'paint-red__wheel-sport__interior-dark__frame-black',
    )
  })

  it('分区没有选项时拒绝创建不完整配置', () => {
    const invalidCatalog = {
      ...catalogFixture,
      parts: catalogFixture.parts.map((part) =>
        part.partId === 'wheel' ? { ...part, options: [] } : part,
      ),
    }

    expect(() => createInitialSelections(invalidCatalog)).toThrow('分区 wheel 没有可用选项')
  })

  it('计算基础价与四项差价之和', () => {
    const total = calculateTotalPrice(catalogFixture, {
      paint: 'paint-silver',
      wheel: 'wheel-forged',
      interior: 'interior-ivory',
      frame: 'frame-red',
    })

    expect(total).toBe(33_120_000)
    expect(formatPrice(total)).toBe('¥331,200')
    expect(formatPrice(0, true)).toBe('已包含')
  })
})
