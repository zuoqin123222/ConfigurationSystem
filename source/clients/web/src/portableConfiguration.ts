import qrcode from 'qrcode-generator'
import { compressToUint8Array, decompressFromUint8Array } from 'lz-string'
import type { CatalogV2, Customizations, Selections } from './types'

export const PORTABLE_CONFIGURATION_PREFIX = 'SC01CFG2.'
export const LEGACY_PORTABLE_CONFIGURATION_PREFIX = 'SC01CFG1.'
// QR Version 40-H 的 Byte 模式容量约 1.27 KiB。新格式使用目录索引压缩，
// 将上限收紧到 1200 字符，确保所有新选配码都能使用高纠错二维码。
const MAX_PORTABLE_CONFIGURATION_LENGTH = 1200
const MAX_LEGACY_PORTABLE_CONFIGURATION_LENGTH = 2900
const CHECKSUM_HEX_LENGTH = 8

interface PortableConfiguration {
  format: 'sc01-config'
  version: 1
  catalogVersion: string
  vehicleId: string
  selections: Selections
  customizations: Customizations
}

type CompactCustomization =
  | [surfaceIndex: number, type: 1, variantIndex: number]
  | [
      surfaceIndex: number,
      type: 2,
      colorHex: string,
      metallic: number,
      roughness: number,
      clearCoat: number,
      orangePeel: number,
      flakeIntensity: number,
    ]

type CompactPortableConfiguration = [
  version: 2,
  catalogVersion: string,
  vehicleId: string,
  selectionIndexes: number[],
  customizations: CompactCustomization[],
]

function encodeBase64Url(bytes: Uint8Array): string {
  let binary = ''
  for (const byte of bytes) binary += String.fromCharCode(byte)
  return btoa(binary)
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/g, '')
}

function decodeBase64Url(value: string): Uint8Array {
  if (!/^[A-Za-z0-9_-]+$/.test(value)) throw new Error('配置字符串已损坏')
  const normalized = value.replace(/-/g, '+').replace(/_/g, '/')
  const padded = normalized.padEnd(Math.ceil(normalized.length / 4) * 4, '=')
  const binary = atob(padded)
  return Uint8Array.from(binary, (character) => character.charCodeAt(0))
}

function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff
  for (const byte of bytes) {
    crc ^= byte
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0)
    }
  }
  return (crc ^ 0xffffffff) >>> 0
}

function checksum(bytes: Uint8Array): string {
  return crc32(bytes).toString(16).padStart(CHECKSUM_HEX_LENGTH, '0').toUpperCase()
}

function compareId(left: string, right: string): number {
  return left < right ? -1 : left > right ? 1 : 0
}

function optionsForSurface(catalog: CatalogV2, surfaceId: string) {
  return catalog.options
    .filter((option) => option.surfaceId === surfaceId)
    .sort((left, right) => compareId(left.optionId, right.optionId))
}

function orderedVariants(catalog: CatalogV2) {
  return catalog.materialVariants
    .slice()
    .sort((left, right) => compareId(left.variantId, right.variantId))
}

function orderedSelections(catalog: CatalogV2, selections: Selections): Selections {
  return Object.fromEntries(
    catalog.selectionOrder.flatMap((surfaceId) =>
      selections[surfaceId] ? [[surfaceId, selections[surfaceId]]] : []),
  )
}

function orderedCustomizations(
  catalog: CatalogV2,
  customizations: Customizations,
): Customizations {
  return Object.fromEntries(
    catalog.selectionOrder.flatMap((surfaceId) => {
      const customization = customizations[surfaceId]
      if (!customization) return []
      return [[surfaceId, 'materialVariantId' in customization
        ? { materialVariantId: customization.materialVariantId }
        : {
            colorHex: customization.colorHex,
            metallic: customization.metallic,
            roughness: customization.roughness,
            clearCoat: customization.clearCoat,
            orangePeel: customization.orangePeel,
            flakeIntensity: customization.flakeIntensity,
          }]]
    }),
  )
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return !!value && typeof value === 'object' && !Array.isArray(value)
}

function hasExactFields(value: Record<string, unknown>, fields: string[]): boolean {
  const keys = Object.keys(value).sort()
  return keys.length === fields.length
    && keys.every((key, index) => key === fields.slice().sort()[index])
}

function validatePayload(
  payload: Partial<PortableConfiguration>,
  catalog: CatalogV2,
): payload is PortableConfiguration {
  if (
    payload.format !== 'sc01-config'
    || payload.version !== 1
    || payload.catalogVersion !== catalog.catalogVersion
    || payload.vehicleId !== catalog.vehicle.vehicleId
    || !isRecord(payload.selections)
    || !isRecord(payload.customizations)
  ) {
    return false
  }
  const surfaceIds = new Set(catalog.selectionOrder)
  for (const [surfaceId, optionId] of Object.entries(payload.selections)) {
    const option = typeof optionId === 'string'
      ? catalog.options.find((item) =>
          item.surfaceId === surfaceId && item.optionId === optionId)
      : undefined
    if (
      !surfaceIds.has(surfaceId)
      || !option
      || option.availability?.status === 'disabled'
      || !Object.entries(option.requiresSelections ?? {}).every(
        ([requiredSurfaceId, requiredOptionId]) =>
          payload.selections?.[requiredSurfaceId] === requiredOptionId)
    ) {
      return false
    }
  }
  for (const [surfaceId, customization] of Object.entries(payload.customizations)) {
    if (!surfaceIds.has(surfaceId) || !isRecord(customization)) return false
    const selectedOptionId = payload.selections[surfaceId]
    const selectedOption = catalog.options.find((option) =>
      option.surfaceId === surfaceId && option.optionId === selectedOptionId)
    if (!selectedOption) return false
    if ('materialVariantId' in customization) {
      const variant = catalog.materialVariants.find((item) =>
        item.variantId === customization.materialVariantId)
      if (
        !hasExactFields(customization, ['materialVariantId'])
        || typeof customization.materialVariantId !== 'string'
        || !variant
        || variant.materialFamilyId !== selectedOption.materialFamilyId
        || (
          selectedOption.ui?.control !== 'material-variant'
          && selectedOption.parameters.color?.mode !== 'variant'
        )
      ) {
        return false
      }
      continue
    }
    const paintFields = [
      'clearCoat',
      'colorHex',
      'flakeIntensity',
      'metallic',
      'orangePeel',
      'roughness',
    ]
    if (
      !hasExactFields(customization, paintFields)
      || selectedOption.parameters.color?.mode !== 'custom'
      || typeof customization.colorHex !== 'string'
      || !/^#[0-9A-F]{6}$/i.test(customization.colorHex)
      || paintFields.filter((field) => field !== 'colorHex').some((field) => {
        const number = customization[field]
        return typeof number !== 'number'
          || !Number.isFinite(number)
          || number < 0
          || number > 1
      })
    ) {
      return false
    }
  }
  return true
}

function compactPayload(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations,
): CompactPortableConfiguration {
  const selectionIndexes = catalog.selectionOrder.map((surfaceId) => {
    const optionId = selections[surfaceId]
    if (!optionId) return 0
    const optionIndex = optionsForSurface(catalog, surfaceId)
      .findIndex((option) => option.optionId === optionId)
    if (optionIndex < 0) throw new Error('当前配置包含未知选项')
    return optionIndex + 1
  })
  const variants = orderedVariants(catalog)
  const compactCustomizations = catalog.selectionOrder.flatMap<CompactCustomization>(
    (surfaceId, surfaceIndex) => {
      const customization = customizations[surfaceId]
      if (!customization) return []
      if ('materialVariantId' in customization) {
        const variantIndex = variants.findIndex(
          (variant) => variant.variantId === customization.materialVariantId,
        )
        if (variantIndex < 0) throw new Error('当前配置包含未知材质颜色')
        return [[surfaceIndex, 1, variantIndex]]
      }
      return [[
        surfaceIndex,
        2,
        customization.colorHex.slice(1).toUpperCase(),
        customization.metallic,
        customization.roughness,
        customization.clearCoat,
        customization.orangePeel,
        customization.flakeIntensity,
      ]]
    },
  )
  return [
    2,
    catalog.catalogVersion,
    catalog.vehicle.vehicleId,
    selectionIndexes,
    compactCustomizations,
  ]
}

function expandCompactPayload(
  value: unknown,
  catalog: CatalogV2,
): Partial<PortableConfiguration> | null {
  if (!Array.isArray(value) || value.length !== 5) return null
  const [version, catalogVersion, vehicleId, selectionIndexes, compactCustomizations] = value
  if (
    version !== 2
    || catalogVersion !== catalog.catalogVersion
    || vehicleId !== catalog.vehicle.vehicleId
    || !Array.isArray(selectionIndexes)
    || selectionIndexes.length !== catalog.selectionOrder.length
    || !Array.isArray(compactCustomizations)
  ) {
    return null
  }

  const selections: Selections = {}
  for (const [surfaceIndex, surfaceId] of catalog.selectionOrder.entries()) {
    const optionIndex = selectionIndexes[surfaceIndex]
    if (!Number.isInteger(optionIndex) || optionIndex < 0) return null
    if (optionIndex === 0) continue
    const option = optionsForSurface(catalog, surfaceId)[optionIndex - 1]
    if (!option) return null
    selections[surfaceId] = option.optionId
  }

  const variants = orderedVariants(catalog)
  const customizations: Customizations = {}
  const customizedSurfaces = new Set<number>()
  for (const entry of compactCustomizations) {
    if (!Array.isArray(entry)) return null
    const surfaceIndex = entry[0]
    if (
      !Number.isInteger(surfaceIndex)
      || surfaceIndex < 0
      || surfaceIndex >= catalog.selectionOrder.length
      || customizedSurfaces.has(surfaceIndex)
    ) {
      return null
    }
    customizedSurfaces.add(surfaceIndex)
    const surfaceId = catalog.selectionOrder[surfaceIndex]
    if (entry[1] === 1 && entry.length === 3) {
      const variantIndex = entry[2]
      if (!Number.isInteger(variantIndex) || variantIndex < 0) return null
      const variant = variants[variantIndex]
      if (!variant) return null
      customizations[surfaceId] = { materialVariantId: variant.variantId }
      continue
    }
    if (
      entry[1] === 2
      && entry.length === 8
      && typeof entry[2] === 'string'
      && /^[0-9A-F]{6}$/.test(entry[2])
      && entry.slice(3).every((item) => typeof item === 'number' && Number.isFinite(item))
    ) {
      customizations[surfaceId] = {
        colorHex: `#${entry[2]}`,
        metallic: entry[3],
        roughness: entry[4],
        clearCoat: entry[5],
        orangePeel: entry[6],
        flakeIntensity: entry[7],
      }
      continue
    }
    return null
  }

  return {
    format: 'sc01-config',
    version: 1,
    catalogVersion,
    vehicleId,
    selections,
    customizations,
  }
}

function encodeEnvelope(prefix: string, payload: unknown, maximumLength: number): string {
  const compressed = compressToUint8Array(JSON.stringify(payload))
  const result = `${prefix}${encodeBase64Url(compressed)}.${checksum(compressed)}`
  if (result.length > maximumLength) {
    throw new Error('当前配置过大，无法生成二维码选配码')
  }
  return result
}

function decodeEnvelope(input: string, prefix: string): unknown {
  const encoded = input.slice(prefix.length)
  const separator = encoded.lastIndexOf('.')
  if (separator <= 0) throw new Error('missing checksum')
  const compressed = decodeBase64Url(encoded.slice(0, separator))
  const expectedChecksum = encoded.slice(separator + 1).toUpperCase()
  if (
    !new RegExp(`^[0-9A-F]{${CHECKSUM_HEX_LENGTH}}$`).test(expectedChecksum)
    || checksum(compressed) !== expectedChecksum
  ) {
    throw new Error('checksum mismatch')
  }
  const json = decompressFromUint8Array(compressed)
  if (!json) throw new Error('decompression failed')
  return JSON.parse(json) as unknown
}

export function createPortableConfiguration(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations,
): string {
  return encodeEnvelope(
    PORTABLE_CONFIGURATION_PREFIX,
    compactPayload(
      catalog,
      orderedSelections(catalog, selections),
      orderedCustomizations(catalog, customizations),
    ),
    MAX_PORTABLE_CONFIGURATION_LENGTH,
  )
}

export function createLegacyPortableConfiguration(
  catalog: CatalogV2,
  selections: Selections,
  customizations: Customizations,
): string {
  const payload: PortableConfiguration = {
    format: 'sc01-config',
    version: 1,
    catalogVersion: catalog.catalogVersion,
    vehicleId: catalog.vehicle.vehicleId,
    selections: orderedSelections(catalog, selections),
    customizations: orderedCustomizations(catalog, customizations),
  }
  return encodeEnvelope(
    LEGACY_PORTABLE_CONFIGURATION_PREFIX,
    payload,
    MAX_LEGACY_PORTABLE_CONFIGURATION_LENGTH,
  )
}

export function parsePortableConfiguration(
  value: string,
  catalog: CatalogV2,
): { selections: Selections; customizations: Customizations } {
  const input = value.trim()
  const prefix = input.startsWith(PORTABLE_CONFIGURATION_PREFIX)
    ? PORTABLE_CONFIGURATION_PREFIX
    : input.startsWith(LEGACY_PORTABLE_CONFIGURATION_PREFIX)
      ? LEGACY_PORTABLE_CONFIGURATION_PREFIX
      : null
  if (!prefix) {
    throw new Error('不是受支持的 SC01 配置字符串')
  }
  const maximumLength = prefix === PORTABLE_CONFIGURATION_PREFIX
    ? MAX_PORTABLE_CONFIGURATION_LENGTH
    : MAX_LEGACY_PORTABLE_CONFIGURATION_LENGTH
  if (input.length > maximumLength) {
    throw new Error('配置字符串超过长度限制')
  }
  let payload: Partial<PortableConfiguration>
  try {
    const decoded = decodeEnvelope(input, prefix)
    payload = prefix === PORTABLE_CONFIGURATION_PREFIX
      ? expandCompactPayload(decoded, catalog) ?? {}
      : decoded as Partial<PortableConfiguration>
  } catch {
    throw new Error('配置字符串已损坏')
  }
  if (!validatePayload(payload, catalog)) {
    throw new Error('配置字符串与当前目录版本不兼容')
  }
  return {
    selections: payload.selections,
    customizations: payload.customizations,
  }
}

export function createPortableConfigurationQr(value: string): string {
  const legacy = value.startsWith(LEGACY_PORTABLE_CONFIGURATION_PREFIX)
  if (!value.startsWith(PORTABLE_CONFIGURATION_PREFIX) && !legacy) {
    throw new Error('不是受支持的 SC01 配置字符串')
  }
  const maximumLength = legacy
    ? MAX_LEGACY_PORTABLE_CONFIGURATION_LENGTH
    : MAX_PORTABLE_CONFIGURATION_LENGTH
  if (value.length > maximumLength) {
    throw new Error('配置字符串过大，无法生成二维码')
  }
  try {
    const qr = qrcode(0, legacy ? 'L' : 'H')
    qr.addData(value, 'Byte')
    qr.make()
    return qr.createDataURL(4, 16)
  } catch {
    throw new Error('配置字符串过大，无法生成二维码')
  }
}
