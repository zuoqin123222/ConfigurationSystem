# SC01 v2 字段决策

## 版本边界

v1 的 `catalog.schema.json`、`published-configurations.schema.json`、`catalog.mvp.json` 和 16 个发布组合保持原样，继续服务 `demo-car` 自动化链路。SC01 使用 `schemaVersion=2.0.0` 和独立文件名，不把多层产品结构或未知价格塞回固定四分区 v1。

v2 当前为 `lifecycle=draft`。草案通过验证只表示结构、自洽性、来源和安全策略合格，不表示商品、价格或可交付性已获业务批准。

## 层级与引用

v2 采用规范化引用：

```text
vehicle
└─ category
   └─ component
      └─ surface
         └─ option → materialFamily
```

`categoryId`、`componentId`、`surfaceId`、`materialFamilyId` 和 `optionId` 均为小写 kebab-case。显示名称不参与引用或身份计算。`selectionOrder` 是配置身份的唯一字段顺序，当前原子样本只包含三个必选 surface。

## 价格安全

原始清单展示了金额和“标配”，但缺少含税、工时、有效期、基础车型价及当前审批状态。为避免把资料摘录误当正式报价：

- `vehicle.basePriceMinor` 和所有 `option.pricing.unitPriceMinor` 固定为 `null`。
- `priceStatus` 固定为 `unconfirmed`，`quotable` 固定为 `false`。
- price-result 的基础价、单价、小计和总价均为 `null`。
- price-result 固定 `quoteAllowed=false`，并包含 `PRICE_UNCONFIRMED`。
- 验证器拒绝任意非空金额或开放报价的 SC01 草案。

`pricingUnit` 支持 `per-vehicle`、`per-piece`、`per-pair`、`per-seat`、
`per-set` 和 `percentage`，用于覆盖整车、单件、左右件、座椅、成套和比例计价。
物理数量没有在当前三项原子样本中得到明确的 `×N` 标记，因此 `quantity` 为
`null`。两者都不参与金额计算；本阶段没有价格求和函数，`buildPriceResult`
只生成阻断结果。

## 稳定配置身份

配置身份不使用 JSON 对象遍历顺序。验证器严格按 catalog 的 `selectionOrder` 生成 UTF-8 文本，行分隔符固定为 LF：

```text
schemaVersion=2.0.0
catalogVersion=<catalogVersion>
vehicleId=<vehicleId>
<surfaceId-1>=<optionId-1>
...
```

`configurationId` 为 `cfg-` 加上述字节 SHA-256 的前 24 个小写十六进制字符。截断值用于稳定标识和缓存键，不作为安全签名。

`renderKey` 不复用完整配置摘要。验证器按 `selectionOrder` 只保留当前
`renderRelevant=true` 的所选项生成 `canonicalRenderInput`，再计算独立摘要：

```text
<vehicleId>__<catalogVersion>__render-<SHA-256(canonicalRenderInput) 前 24 位>
```

这样材料备注、服务项或当前发布尚不呈现的细节改变时，`configurationId` 会变化，
但可以复用同一组 Render。`renderViewId` 仍在配置身份之外；它选择同一渲染投影
的视角。算法和跨属性顺序向量冻结在
`contracts/fixtures/sc01.identity-golden.v2.json`。

## 校验职责

`tools/validate-sc01-v2.mjs` 负责跨文件语义：

- 层级引用、ID 唯一性和必选 surface 覆盖。
- option 必须属于所选 surface，拒绝缺选、跨面选项和未知选项。
- 重新计算并比对 `configurationId` 与 `renderKey`。
- 强制所有未知金额为 `null` 并禁止报价。
- 验证有效/无效 fixture 和黄金向量。

三个 JSON Schema 负责冻结传输形状；聚合入口 `node tools/validate-contracts.mjs` 同时执行 v1 与 v2，任何一侧失败都返回非零退出码。
