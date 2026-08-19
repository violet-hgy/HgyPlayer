---
name: design-to-qml
description: >-
  将设计（Figma 链接 / MCP，或用户提供的控件切图、截图）落地为 MojhonAssistantQt
  的 Qt Quick / QML。该 skill 现作为总调度 skill：识别任务类型、选择 Figma/图片链路、
  决定是否进入 UI 落地，并按需继续读取 design-intake、design-measure、
  design-asset-guard、design-visual-guard、design-implement-qml、design-verify。
  自动触发信号包括 figma.com、node-id、fileKey、design-to-code、设计稿、按图还原、
  像素对齐，以及新建/改写 qml/default_en_us 页面或 components。
disable-model-invocation: false
---

<!-- markdownlint-disable MD024 MD036 MD040 MD060 -->

# Design → QML（Dispatcher）

> 原 skill 名：`figma-to-qml`。现作为 **总调度 skill** 使用。

## 目标

统一处理 MojhonAssistantQt 的设计落地请求，但不再把所有细则堆进一个文件；由本 skill 负责：

- 识别任务是否属于设计落地
- 识别设计源是 Figma、图片，还是无设计源的视觉 QML 改动
- 决定后续应读取哪些子 skill
- 统一收口到 QML 落地和收尾自检

## 项目背景

- 本项目是**手柄上位机**
- 同类控件会跨多机型、多页面复用
- 默认目标是**可复用组件 + 页面组装**
- 禁止按机型复制整页近似控件

## 自动触发

命中以下任一信号时，先读本 skill：

- Figma / FigJam URL
- `node-id` / `fileKey` / `get_design_context`
- 用户附 PNG / 截图 / 切图
- “按图还原”“像素对齐”“设计稿落地”“对齐 UI”
- 新建 / 修改 `qml/default_en_us/**/*.qml` 或 `components/`

## 调度顺序

### 第 1 步：总是先读

- `.cursor/skills/design-intake/SKILL.md`

### 第 2 步：按任务读取

#### A. 需要读设计 / 测量

满足任一时读取：

- 有 Figma
- 有 PNG / 截图 / 切图
- 用户要求按图对齐 / 复现

读取：

- 进入 `design-measure` 前：先做 Python 前置校验（门控见 `design-measure`）
- 优先读取嵌套路径：
  - `.cursor/skills/design-to-qml/measure/SKILL.md`
  - `.cursor/skills/design-to-qml/asset-guard/SKILL.md`
  - `.cursor/skills/design-to-qml/visual-guard/SKILL.md`
- 若读取失败则回退平铺路径：
  - `.cursor/skills/design-measure/SKILL.md`
  - `.cursor/skills/design-asset-guard/SKILL.md`
  - `.cursor/skills/design-visual-guard/SKILL.md`

#### B. 需要真正改 QML

满足任一时读取：

- 任务目标是代码落地
- 本轮会改 `qml/default_en_us/`
- 本轮会改 `components/`

读取：

- 优先读取嵌套路径：
  - `.cursor/skills/design-to-qml/implement-qml/SKILL.md`
  - `.cursor/skills/design-to-qml/verify/SKILL.md`
- 若读取失败则回退平铺路径：
  - `.cursor/skills/design-implement-qml/SKILL.md`
  - `.cursor/skills/design-verify/SKILL.md`

### 第 3 步：收尾

若落地了 QML 或资源，结束前总是读取：

- `.cursor/skills/design-verify/SKILL.md`

## 典型路由

### Figma -> QML

1. `design-intake`
2. `design-measure`
3. `design-asset-guard`
4. `design-visual-guard`
5. `design-implement-qml`
6. `design-verify`

### 图片 / 切图 -> QML

1. `design-intake`
2. `design-measure`
3. `design-asset-guard`
4. `design-visual-guard`
5. `design-implement-qml`
6. `design-verify`

### 无设计源，但要改视觉 QML

1. `design-intake`
2. `design-implement-qml`
3. `design-verify`

### 只做分析 / 解读 / 差异说明

1. `design-intake`
2. 若有图再读 `design-measure`
3. 不进入 `design-implement-qml`

## 子 skill 职责

### `design-intake`

- 任务识别
- 缺信息门控
- “对话点名未对齐”前置列举

### `design-measure`

- 识图 / 读 Figma
- 像素参数表
- 视觉参数表
- 一维分段表

### `design-asset-guard`

- 缺图硬停
- 透明底要求
- 资源颜色不符时找用户更新
- 资源落盘与 `qrc` 接入

### `design-visual-guard`

- 颜色 / 字号 / 字重 / 圆角 / 描边 / 透明度
- 阴影只记录不落地
- 防止只改几何

### `design-implement-qml`

- 布局、组件复用、资源引用
- `qsTr(...)`、主题、翻译同步
- 只改 `qml/default_en_us/`

### `design-verify`

- QML 落地自检
- 文档 / 翻译 / `qrc` / 组件目录检查
- 收尾输出【差异 / 不确定】

## 总调度硬规则

- 不要见图就直接改 QML；先经过 `design-intake`
- 有图对齐任务，改码前必须已有参数表输出
- 资源颜色明显不对时，必须走 `design-asset-guard`
- 识别到颜色 / 字号 / 圆角差异时，必须走 `design-visual-guard`
- 任何 `qml/default_en_us/` 视觉改动，结束前都要走 `design-verify`

## 规范镜像同步

- 规范源：`.cursor/skills/design-to-qml/SKILL.md`
- 文档镜像：`docs/skills/design-to-qml.md`
- 改规范源后立即同步镜像，内容保持一致
