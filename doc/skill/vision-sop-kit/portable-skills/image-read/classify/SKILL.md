---
name: image-read-classify
description: 读图·场景分流（由 image-read 调度）。
disable-model-invocation: true
---

# Classify

输出：

```text
【识图场景】<场景码> — <一句话依据>
```

用户点名优先。

| 场景码 | 特征 | 测量重点 |
| --- | --- | --- |
| `showcase-align` | 大块渲染常含透明边 | 透明 pad + 墨迹框 |
| `overlay-badge` | 角落浮层无遮罩 | 外框、border、页边距、内 padding |
| `dialog-sheet` | 遮罩 + 面板 | 遮罩与面板分列 |
| `asset-compare` | 新旧图比尺寸 | 固有尺寸、宽高比、显示框 |
| `control-full` | 孤立单控件切图 | 外框、padding、border |
| `delegate-row` | ≥2 同构近等距离散项 | itemSpacing + 首项外距 |
| `page-layout` | 整页多区块 | 框 + gap + 距内容区边 |
| `visual-tweak` | 主要差色/字/圆角 | 视觉表 |
| `measure-only` | 只要报告 | 完整测量 |
| `text-extract` | 只要文案 | 文案列表 |

判定：透明大主体→showcase；孤立单块→control-full；≥2 同构等距→delegate-row（勿等说「列表」）；遮罩+面板→dialog-sheet；角落胶囊→overlay-badge。

仅当测量目标无法唯一框定时停问（与写码落点无关）。
