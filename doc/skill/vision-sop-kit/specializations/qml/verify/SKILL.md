---
name: design-verify
description: 对 MojhonAssistantQt 设计落地结果做收尾自检：QML 视觉检查、主题与翻译检查、组件目录和规则同步检查。供 design-to-qml 调度时读取。目录层级不影响规则，按 dispatcher 读取嵌套或平铺路径均可。
disable-model-invocation: false
---

# Design Verify（QML 特化 · 收尾）

项目路径、主题、翻译、工具命令以宿主 **project-bindings** 为准；本文件写检查动作。

隶属 `specializations/qml`；前置应为 image-read 报告（有图时）+ implement 落码。

## 必查项

- 是否已读 project-bindings 并对照其清单
- 是否已输出【差异 / 不确定】
- 是否完成 QML 落地自检
- 是否遗漏 bindings 中的主题 / 图片 API
- 是否遗漏 `qsTr(...)` 及 ts/`lrelease`（路径见 bindings）
- 变体弹窗按钮文案是否与设计一致（勿沿用 Form 默认 `OK`/`Cancel` 翻译）
- 是否需要更新 bindings 中的组件目录文档
- 是否有资源未进 bindings 中的 `qrc`
- **视觉**：参数表中的色/字号/字重/圆角/描边是否已落码（禁止只改几何）；阴影是否仅记录未实现
- **资源**：依赖图是否存在且色/透明底合格；【资源对照】是否已写；子图标几何（尺寸/边距）是否落码；`asset-swap` 尺寸冲突是否已获用户确认；无法用基础元素且无现成图时，首轮对齐后是否已输出【缺图待补充】（禁止静默缺图或近似顶替）

## 尺寸链自检（禁止留给用户二次视觉确认）

用户或参数表已经给出外框 / margin / 内容宽时，收尾前必须对照已写 QML 做算术核对，通过后再回复：

- 外框宽是否被 `maximumWidth`（或等价）锁住，而不是只有 `preferredWidth` + `fillWidth`
- 内容宽是否等于 `外框 - 左右 margin`，子项是否不可能大于外框
- 页面骨架是否已 `anchors.fill` / `Layout.fillWidth` / `Layout.fillHeight`；是否仍写死整页 `width` / `height`
- 若尺寸链在代码里已经矛盾（例如内容宽绑定会超过外框），视为未完成，当场改掉

禁止把“跑起来看一眼才知道错”的可算术误差交给用户。

## 落地后执行自检（必做）

改完 bindings 允许的 QML 树后，**回复用户前**必须完成下列可执行检查；任一项失败则继续改，不得收尾。

### 1. QML 语法 / 编译自检

对本轮修改过的 QML 文件运行 `qmllint`（命令见 **project-bindings**），**不得出现 error**。

- error（含 `Expected token '}'`）→ 立即修语法，优先检查新增 `Item` / `ScrollView` 包装层是否少 `}`
- warning 可记录，但不替代尺寸链 / 布局自检

### 2. 括号与包装层自检

若本轮新增或调整了容器包装（如 `itemSettingListHost` 套 `itemSettingList`）：

- 逐层核对：`ScrollView` → 内层 `Item` → 外层 `Item` → `RowLayout` → `ColumnLayout` → 根节点，每层都有闭合 `}`
- 改完后再读文件末尾 30 行，确认闭合层级与缩进一致

### 3. 尺寸链算术自检（有外框 / margin 时必做）

收尾前输出简短【尺寸链自检】（可写在回复里，也可只在思考中完成但不得跳过核对）：

| 项 | 期望 | 现码是否一致 |
| --- | --- | --- |
| 外框宽 | 用户给定（如 410） | `min/preferred/max` 是否同为该值且无 `fillWidth` 撑破 |
| 左右 margin | 用户给定（如 30） | `padding` / `leftPadding` / `rightPadding` |
| 内容宽 | 外框 - 左 - 右（如 350） | 不得用裸 `availableWidth` 吞 margin |
| 子项最大宽 | ≤ 内容宽 | 无更大 `minimumWidth` / 写死 `width` |

**单控件切图复用**：若参数表有「控件外框（落码锁定）」，须核对 QML 默认变体仍用旧外框、**新变体**外框等于锁定值；不得把 PNG 整图边界写进 `dialogWidth` / `implicitWidth`。不等则不得收尾。

### 4. 窗口拉伸 + 锁定块自检

- 页面骨架：`anchors.fill` + 主 `RowLayout`/`ColumnLayout` 的 `Layout.fillWidth` / `fillHeight`
- 锁定块（如列表 410）：`fillWidth: false` + `min/preferred/max` 同值
- 可伸缩区（如左侧主图区）：`Layout.fillWidth: true` 吸收多余宽度
- 需要与主图垂直居中时：用外层 host `fillHeight` + 内层 `anchors.verticalCenter`，**不要**只靠 `Layout.fillHeight: false` 且无最小高度

### 5. ScrollView / 列表高度陷阱自检

高度若绑定 `ColumnLayout.implicitHeight` 且该列在 `ScrollView` 内：

- `implicitHeight` 常为 0 → 必须有 `Math.max(..., 设计最小高度)` 兜底
- 设计最小高度优先用已锁定规格推导（如主图 397 + 上下 margin 30 = 457），禁止只绑 `implicitHeight`
- 绑定后自问：首帧高度是否为 0？为 0 则列表会“消失”

### 6. 改动隔离自检（仅纯文本点名）

- diff 是否只覆盖用户点名项
- 是否误改已确认的外框宽 / margin / 内容宽公式 / 滚动条位置
- 有连带改动 → 先改回再收尾

### 7. 控件完成后布局复校（改过宽高则强制）

本轮若曾调整目标控件（或 delegate）的 **`width` / `height` / `implicit*` / `preferred*` / `minimum*` / `maximum*` / 外框锁定值**，在单控件「看起来对齐」之后**不得直接收尾**，必须再做一轮**布局复校**。原因：局部改尺寸常把父 Layout、邻接 gap、列表外/内距、ScrollView 高度链带歪，而单看该控件外框发现不了。

#### 何时触发

满足任一即触发（有图对齐与纯文本改尺寸均适用）：

- 参数表或落码改写了控件外框宽高
- 复用变体为新稿增大/减小了外框
- 列表项、配置行、宏卡片改了行高/卡宽
- 为塞进外框而改了子项宽高或 padding（间接触发父级重排）

#### 复校清单（对照参数表 + 现码，必要时回看设计图邻接）

输出简短【布局复校】（可附在收尾回复）：

| 复校项 | 查什么 | 异常时 |
| --- | --- | --- |
| 父级约束 | 父 `RowLayout`/`ColumnLayout`/`ScrollView` 是否被撑破、裁切、出现非预期滚动条 | 改回父级 margin/spacing 或子项约束，勿只缩小目标糊弄 |
| 邻接 gap | 与上/下/左/右**非目标**控件的空隙是否仍等于参数表外距 / 邻接 gap | 复校外距；禁止用改目标 height 冒充 gap |
| 列表外/内距 | 若在列表内：`itemSpacing`、列表距上邻、行内距是否仍分栏正确 | 按 `design-measure` 分栏改回，勿混进一个 spacing |
| 同列齐宽 | 同列表/同组其它行是否应同宽；改一项是否导致参差 | delegate 统一外框，禁止只改可见那一行的魔法数 |
| 拉伸吸收 | 窗口变宽时多余空间是否仍由可伸缩区吸收；锁定块是否被 `fillWidth` 撑破 | 见第 4 节 |
| 高度链 | 改高后 ScrollView/列表首帧高度是否为 0 或裁切标题区 | 见第 5 节 |
| 未点名兄弟 | 纯文本点名时：邻接项是否被连带挤位（复校发现则列入未对齐项，点名范围外先问再改；有图则按图表修） | 有图：修；无图点名：先列再确认 |

#### 硬规则

1. **先完成目标控件几何，再复校布局**；复校发现问题视为**未完成**，当场修，不得写「控件已对齐，布局请再看一眼」
2. 复校以**参数表外距 / itemSpacing / 邻接 gap** 为准；若改宽高后这些式子不再成立，改布局关系或回退错误尺寸，禁止沉默
3. 只改了颜色/文案、**未改任何宽高类属性** → 可跳过本节（在收尾注明「未改尺寸，跳过布局复校」即可）

## 对齐任务额外检查

- 用户点名项是否全部处理
- 对齐过程中新增发现项是否已先列再改
- 若有无法稳定读到的视觉参数，是否明确告知
- 若识别到阴影，是否仅记录未落地
- 纯文本点名（无图）时：diff 是否只覆盖点名项；是否误改已确认的宽高 / padding / 内容宽计算；有连带改动则先改回
- 有图对齐时：不要用“只改点名项”跳过图上已测出的几何或可视差异

## 输出要求

收尾回复至少包含：

- 已对齐的关键项
- 仍不确定 / 未稳定读到的项
- 若有额外修正，明确告知用户

## 禁止

- 改完不检查翻译、不 `lrelease` 编译 `.qm`
- 改组件却不更新组件目录
- 发现不确定项却沉默收尾
- 把已能算术核对的尺寸误差留给用户二次视觉确认
- 未跑 QML 语法自检（`qmllint` 或等价编译）就宣告完成
- ScrollView 高度只绑 `implicitHeight` 且无最小高度兜底
- 改过控件宽高却不做【布局复校】（父级/邻接 gap/列表外内距/拉伸）就宣告完成
- 把「单控件外框已对、周围布局已坏」留给用户二次发现
