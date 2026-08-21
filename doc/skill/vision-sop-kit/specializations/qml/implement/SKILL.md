---
name: design-implement-qml
description: >-
  将已测得的设计参数落到 Qt Quick / QML：通用布局与尺寸链、组件变体复用、列表外内距落码。
  本仓库路径、主题、翻译、具名组件见 design-to-qml/project-bindings.md（必读）。
  供 design-to-qml 调度；目录层级不影响规则。
disable-model-invocation: false
---

# Design Implement QML

## 作用

只负责**把已确认参数写进 QML**，不再重新发明测量规则。

**分层：**

| 层 | 内容 | 文件 |
| --- | --- | --- |
| 项目硬绑定 | 目录、主题、翻译、具名组件、工具路径 | 宿主提供的 `project-bindings.md`（Kit 示例见 `project-sop/examples/`） |
| 本文件 | **QML 通用落码写法**（Layout、尺寸链、变体、列表 spacing、改动隔离） | 下文 |

未读 project-bindings 不得宣称项目落地完成。

## 实现顺序

1. 读 project-bindings，确认落点目录与组件映射
2. 先用现有码结构求解
3. **判定是否可复用**（见下）；可复用时走变体/参数，不直接沿用旧外框当新稿外框
4. 优先微调几何参数，不先推翻结构
5. 再落视觉参数（见下「视觉落码」；色值命名空间见 bindings）
6. 最后接资源、翻译（流程见 bindings）、文档
7. 改过宽高 → 收尾前走同特化包 `../verify/SKILL.md`【布局复校】

## 视觉落码

参数表已有视觉项时必须写入 QML，**禁止只改 spacing/margin 而保留默认色/圆角**：

- 背景 / 前景 / 透明度、字号 / 字重、圆角、描边色宽、图标色与朝向
- 色走 bindings 主题 API；禁止用默认控件色冒充设计色；禁止统一圆角抹平切图差异
- **阴影**：只记录，不实现
- 弹窗变体：主按钮文案须覆盖 Form 默认 `OK`/`Cancel`（若设计不是这两字）
- 未稳定读到的视觉项：落码时勿猜，保留【差异 / 不确定】

## QML 通用写法（与栈相关、与仓库路径无关）

### 1. 页面骨架：Layout，忌满页绝对坐标

- 主结构用 `RowLayout` / `ColumnLayout` / `GridLayout` + `Layout.*`
- 页面根内容：`anchors.fill` 或 `Layout.fillWidth` / `fillHeight`
- 禁止把设计稿坐标整页写成死 `x` / `y` / 固定窗宽高（锁定的是**局部控件外框**，不是整窗）

### 2. 尺寸链：外框 − padding = 内容宽

用户或参数表给定外框 / margin 时：

```text
内容宽 = 外框 − 左 padding/margin − 右 padding/margin
```

落码要点：

- 外框用 `preferredWidth` + `minimumWidth` + `maximumWidth` **同值锁死**
- 禁止再加 `Layout.fillWidth: true` 把锁定外框撑破
- 内容宽写常量或不会被父级吞掉的绑定；禁止裸 `availableWidth` 吃掉 margin
- 子项 `minimumWidth` ≤ 内容宽

### 3. 窗口拉伸 vs 锁定块

| 角色 | 写法 |
| --- | --- |
| 可伸缩区（吸收多余空间） | `Layout.fillWidth: true` / `fillHeight: true` |
| 设计锁定块（如固定宽列表列） | `fillWidth: false` + min/preferred/max 同值 |
| 需要垂直居中的子块 | 外层 host `fillHeight` + 内层 `anchors.verticalCenter`，勿只靠无最小高度的 `fillHeight: false` |

### 4. 边距落点：`anchors.*Margin` vs `Layout.*` vs `padding`

| 参数表含义 | 典型 API |
| --- | --- |
| 相对父/内容区边 | `anchors.left/right/top/bottomMargin` 或 `Layout.margins` |
| 控件内沿 → 内容 | `padding` / `leftPadding`… 或内层 Layout 的 margin |
| 描边 | `border.width` / `border.color`（与参数表成对） |

禁止：只写外框宽高，漏已测 border 或整页相对边距。

### 5. 图片框：`PreserveAspectFit` 与透明边

含大面积透明的 PNG：控件 `width×height` 是**整图**盒子，墨迹会小于盒子。

- 显示尺寸按参数表推导值落，勿盲 `anchors.fill` 或未测透明边就 `centerIn`
- `fillMode: Image.PreserveAspectFit`（或项目等价）时，scale 由短边约束；推导式见 `design-measure` Showcase 节
- 换图尺寸冲突见 bindings + `asset-swap` 门控

### 6. 列表：外距 ≠ `ListView.spacing`

| 参数表字段 | 写入位置 |
| --- | --- |
| 列表距上/下/左/右邻 | 列表**容器或父 Layout** 的 margin / 与非列表邻居的 spacing |
| 列表容器 padding | 外包层 padding，或 header/footer/margins |
| **itemSpacing** | 仅 `ListView.spacing` 或列表向 `ColumnLayout.spacing` |
| 行内 padding / 行内 gap | **delegate 内部** |

硬规则：列表离标题远 → 动外距；两行更疏 → 动 `itemSpacing`；未分栏的单一 `spacing` → 退回 measure。

离散重复项 → 一条 `delegate` + model，禁止按可见条数复制 N 份。

### 7. ScrollView 高度

高度若绑内层 `ColumnLayout.implicitHeight`：常为 0。必须有 `Math.max(..., 设计最小高度)`（或等价）兜底，否则首帧列表「消失」。

### 8. 弹层

- `Popup` / `Dialog`：`modal`、关闭策略；遮罩用 `Overlay.modal`（或项目等价）
- 面板宽高绑**面板外框**，不含遮罩像素
- 优先变体复用现有弹窗组件（具名映射见 bindings）

### 9. 组件复用与变体（通用策略）

复用看**结构同类**（如都是标题+正文+双按钮），不看「高度是否碰巧接近」。

1. **默认变体不回退**：旧调用处的 width/height/padding/spacing 保持改前值
2. **新变体**绑参数表「控件外框（落码锁定）」，禁止绑 PNG 整图边界
3. 用 `layoutStyle` / `variant` / 只读常量区分；新稿差异只进新分支
4. 变体外框变了 → 重算内容宽再写入

本仓库示例（数值属 bindings）：`MessageBox` 默认 391 vs 注意类 375。

### 10. 改动隔离与猜测

- **纯文本点名（无图）**：只改点名项；已确认 UI 锁定
- **有图**：按参数表，不得用隔离跳过图上差异
- 参数表含猜测/占位：回复保留【暂时猜测数值占用】；收尾写「含占位，待重测」

### 11. 落码后布局复校

凡改过 `width` / `height` / `implicit*` / `preferred*` / min/max：单控件外框对齐后必须再跑 verify【布局复校】（父级、邻接 gap、列表外内距）。

## 项目侧检查清单（细节以 bindings 为准）

落地本仓库时确认：

- [ ] 只改 bindings 允许的 QML 树；成对 Form
- [ ] 色 / 图 / 字走主题 API；文案 `qsTr` + ts/`lrelease`
- [ ] 资源进 `res/` + `qrc`
- [ ] 具名场景落到映射表中的组件，而非新建近似套件

## 适合放在这里的改动

- `spacing` / `padding` / `margin`
- `preferredWidth` / `preferredHeight` / min / max
- `Layout.*` / `anchors.*`
- `color` / `radius` / `border`（色值来源见 bindings）
- 资源引用、文案与翻译上下文

## 禁止

- 未读 project-bindings 就按「通用模板」写入错误目录或硬编码主题
- 借对齐顺手重构；按机型复制整页近似控件
- 只落几何，漏已确认的可视参数（见「视觉落码」）
- 用 `ListView.spacing` 吸收列表外距，或用页边距冒充项间距
- 把猜测数值当实测且不声明
- 改过宽高却跳过布局复校
- 用默认控件色/统一圆角冒充设计；把阴影当描边落地
