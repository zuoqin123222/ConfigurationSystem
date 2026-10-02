import type { Catalog } from '../types'

export const catalogFixture: Catalog = {
  schemaVersion: '1.0.0',
  catalogVersion: 'mvp-v1',
  currency: 'CNY',
  vehicle: { vehicleId: 'demo-car', zhName: 'SC01', basePriceMinor: 30_000_000 },
  parts: [
    {
      partId: 'paint', zhName: '车漆', displayOrder: 0,
      options: [
        { optionId: 'paint-red', zhName: '竞速红', priceDeltaMinor: 0, previewImageUrl: '/red.png', uePrimaryAssetId: 'CarMaterialOption:paint-red' },
        { optionId: 'paint-silver', zhName: '星辉银', priceDeltaMinor: 880_000, previewImageUrl: '/silver.png', uePrimaryAssetId: 'CarMaterialOption:paint-silver' },
      ],
    },
    {
      partId: 'wheel', zhName: '轮毂', displayOrder: 1,
      options: [
        { optionId: 'wheel-sport', zhName: '运动轮毂', priceDeltaMinor: 0, previewImageUrl: '/sport.png', uePrimaryAssetId: 'CarMaterialOption:wheel-sport' },
        { optionId: 'wheel-forged', zhName: '锻造轮毂', priceDeltaMinor: 1_200_000, previewImageUrl: '/forged.png', uePrimaryAssetId: 'CarMaterialOption:wheel-forged' },
      ],
    },
    {
      partId: 'interior', zhName: '内饰', displayOrder: 2,
      options: [
        { optionId: 'interior-dark', zhName: '曜石黑', priceDeltaMinor: 0, previewImageUrl: '/dark.png', uePrimaryAssetId: 'CarMaterialOption:interior-dark' },
        { optionId: 'interior-ivory', zhName: '象牙白', priceDeltaMinor: 680_000, previewImageUrl: '/ivory.png', uePrimaryAssetId: 'CarMaterialOption:interior-ivory' },
      ],
    },
    {
      partId: 'frame', zhName: '内部车架', displayOrder: 3,
      options: [
        { optionId: 'frame-black', zhName: '哑光黑', priceDeltaMinor: 0, previewImageUrl: '/black.png', uePrimaryAssetId: 'CarMaterialOption:frame-black' },
        { optionId: 'frame-red', zhName: '性能红', priceDeltaMinor: 360_000, previewImageUrl: '/frame-red.png', uePrimaryAssetId: 'CarMaterialOption:frame-red' },
      ],
    },
  ],
  templates: [
    { templateId: 'sport', zhName: '运动', selections: { paint: 'paint-red', wheel: 'wheel-sport', interior: 'interior-dark', frame: 'frame-red' } },
    { templateId: 'luxury', zhName: '豪华', selections: { paint: 'paint-silver', wheel: 'wheel-forged', interior: 'interior-ivory', frame: 'frame-black' } },
  ],
  interactionCameras: ['default', 'paint', 'wheel', 'interior', 'frame'],
  renderViews: [
    { renderViewId: 'front', zhName: '正前' },
    { renderViewId: 'front-left', zhName: '左前' },
    { renderViewId: 'side', zhName: '侧面' },
    { renderViewId: 'rear-right', zhName: '右后' },
  ],
}
