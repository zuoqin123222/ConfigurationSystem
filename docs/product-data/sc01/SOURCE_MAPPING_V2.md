# SC01 v2 来源映射

## 适用范围

`contracts/fixtures/sc01.catalog.draft.v2.json` 只收录两份原始 PDF 中能从版面直接确认的名称、层级、标配标记、计价单位和数量。它是契约原子阶段的草案样本，不是完整商品目录，也不能用于报价或订单。

来源文件由 `source-manifest.json` 锁定哈希：

| documentId | 文件 | 用途 |
|---|---|---|
| `sc01-configuration-list` | `source/SC01-定制选配清单.pdf` | 分类、部件、表面、材料选项及表内标记 |
| `sc01-color-card` | `source/SC01-选配色卡.pdf` | 材料与色卡参考；本阶段不生成可售颜色选项 |

## 草案字段映射

| v2 字段或 ID | 来源页与定位 | 录入值 | 处理 |
|---|---|---|---|
| `vehicle.vehicleId` | 两份文件标题均含 SC01 | `sc01` | 稳定 ID |
| `categoryId=exterior` | 清单第 1 页“外观” | 外观 | 直接录入 |
| `componentId=body` | 清单第 1 页“车漆颜色 / 全车身覆盖件” | 车身 | 用稳定英文 ID 表达层级 |
| `surfaceId=exterior-body-cover` | 清单第 1 页“全车身覆盖件” | 全车身覆盖件 | 直接录入 |
| `body-cover-red` | 清单第 1 页“红色、黄色 / 标配” | 红色 | 拆为可独立选择的稳定 ID；色号未知 |
| `body-cover-yellow` | 清单第 1 页“红色、黄色 / 标配” | 黄色 | 拆为可独立选择的稳定 ID；色号未知 |
| `componentId=wheel` | 清单第 1 页“轮毂” | 轮毂 | 直接录入 |
| `surfaceId=wheel-material` | 清单第 1 页“轮毂材质” | 轮毂材质 | 直接录入 |
| `wheel-aluminum-alloy` | 清单第 1 页“铝合金 / 标配” | 铝合金 | 标配标记可确认 |
| `wheel-magnesium-alloy` | 清单第 1 页“镁合金” | 镁合金 | 非标配选项；金额不录入 |
| `categoryId=steering-wheel` | 清单第 1 页“方向盘” | 方向盘 | 在 v2 中同时作为分类和部件，各自 ID 稳定 |
| `surfaceId=steering-wheel-skin` | 清单第 1 页“表皮” | 表皮 | 直接录入 |
| `steering-skin-ultrasuede-black` | 清单第 1 页“Ultrasuede（黑）/ 标配” | Ultrasuede（黑） | 标配标记可确认 |
| `steering-skin-ultrasuede-custom` | 清单第 1 页“Ultrasuede（色彩拓展）” | Ultrasuede（色彩拓展） | 不展开具体色号 |
| `steering-skin-alcantara` | 清单第 1 页“Alcantara” | Alcantara | 直接录入 |
| `steering-skin-leather` | 清单第 1 页“牛皮” | 牛皮 | 直接录入 |

## 未录入内容

- 清单中的数字金额没有业务确认其币种口径、含税/工时范围、有效期和是否仍有效，因此没有复制到 v2 fixture。
- 基础车型价格没有来源，`basePriceMinor` 固定为 `null`。
- 色卡 PDF 的材料名称可用于人工索引，但具体色号与选配面的适用关系尚未确认，本阶段不生成颜色 option。
- 清单第 1–4 页其余部件会在逐项复核后扩充；当前三面样本只用于冻结层级、身份和禁止报价语义。
- 互斥、依赖、套餐、用户选择百分比和交付周期没有可确认规则，本阶段不建模。

## 可追溯要求

catalog 中每个分类、部件、表面、材料族和选项都带至少一个 `sourceRefs`。`documentId` 必须能映射到本文件中的来源，页码按 PDF 页码计数，`locator` 使用可在页面上人工复核的表头路径。没有来源定位的业务值不得进入 SC01 v2 草案。

## 定制项目图片

- Web 部件参考图直接提取自 `source/SC01-定制选配清单.pdf` 第 1–4 页“定制项目”列中的内嵌原图，不经过页面截图、AI 重绘或插值放大。
- 共提取 29 张，原始宽度均为 1280px；以 WebP 质量 95 保存到 `source/clients/web/public/sc01/interior-parts/`。
- 文件名使用对应的 `surfaceId`，Web 通过 `interiorPartImages.ts` 做显式映射。
- `steering-wheel-addon`（方向盘“加粗”）在原清单中没有独立图片，因此不复用方向盘其他图片，界面显示纯黑参考区域。
