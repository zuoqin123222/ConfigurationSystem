const BASE = '/sc01/interior-parts'

/** 清单“定制项目”列中的原始 1280px 部件参考图。 */
export const INTERIOR_PART_IMAGES: Readonly<Record<string, string>> = {
  'steering-wheel-skin': `${BASE}/steering-wheel-skin.webp`,
  'steering-center-mark': `${BASE}/steering-center-mark.webp`,
  'seat-backrest': `${BASE}/seat-backrest.webp`,
  'seat-bolster': `${BASE}/seat-bolster.webp`,
  'seat-shell-back': `${BASE}/seat-shell-back.webp`,
  'seat-headrest-mark': `${BASE}/seat-headrest-mark.webp`,
  'door-upper': `${BASE}/door-upper.webp`,
  'door-middle': `${BASE}/door-middle.webp`,
  'door-armrest': `${BASE}/door-armrest.webp`,
  'door-armrest-skin': `${BASE}/door-armrest-skin.webp`,
  'ip-wings': `${BASE}/ip-wings.webp`,
  'ip-middle': `${BASE}/ip-middle.webp`,
  'ip-instrument-cover': `${BASE}/ip-instrument-cover.webp`,
  'ip-upper-trim': `${BASE}/ip-upper-trim.webp`,
  'ip-lower-trim': `${BASE}/ip-lower-trim.webp`,
  'ip-center-mark': `${BASE}/ip-center-mark.webp`,
  'storage-soft-bag': `${BASE}/storage-soft-bag.webp`,
  'console-armrest-cover': `${BASE}/console-armrest-cover.webp`,
  'console-armrest-side': `${BASE}/console-armrest-side.webp`,
  handbrake: `${BASE}/handbrake.webp`,
  'roof-surface': `${BASE}/roof-surface.webp`,
  'a-pillar-surface': `${BASE}/a-pillar-surface.webp`,
  'interior-painted-parts': `${BASE}/interior-painted-parts.webp`,
  'door-sill': `${BASE}/door-sill.webp`,
  'embroidered-logo': `${BASE}/embroidered-logo.webp`,
  'headrest-embroidery': `${BASE}/headrest-embroidery.webp`,
  'door-panel-embroidery': `${BASE}/door-panel-embroidery.webp`,
  'center-panel-trim': `${BASE}/center-panel-trim.webp`,
  nameplate: `${BASE}/nameplate-stainless-preview.webp`,
  'shift-knob': `${BASE}/shift-knob.webp`,
  'brake-handle': `${BASE}/brake-handle.webp`,
  pedal: `${BASE}/pedal.webp`,
}

export const BLACK_REFERENCE_SURFACES = new Set<string>()
