---
name: design-intake
description: >-
  MojhonAssistantQt 设计落地任务识别：是否写码、点名未对齐、项目缺信息门控。
  有图场景与像素测量由 portable image-read 负责。供 design-to-qml 调度。
disable-model-invocation: false
---

# Design Intake

## 作用

**任务识别、写码门控、点名未对齐**；不负责通用像素测量（见 `image-read`）。

落点目录 / 具名组件：`.cursor/skills/design-to-qml/project-bindings.md`。

## 入口判断

- Figma / FigJam / `node-id` / `fileKey`
- 附图、按图还原、像素对齐、设计稿落地
- 新建 / 修改 bindings 中的 QML 树

## 任务分类

### 1. 进入写码（UI 落地）

- 明确要求按图改 QML / 对齐页面 / 实现设计
- 有设计源且目标是代码落地
- 无设计源但本轮要改 bindings 允许树内视觉

### 2. 不进入写码

- 只要解读 / 差异 / 量尺寸 / 评审
- 只查 Figma 节点、不改代码

## 源路由

- 有图 → `design-to-qml` 调度 **image-read**，再按是否写码继续  
- Figma + 图 → Figma 为设计源，图补像素（仍先 image-read）

## 识图场景

有图时：**场景码与测量由 image-read 产出**，本文件不重复测像素。

本文件只做：是否写码、项目缺信息、对话点名列举。  
可将场景码映射到 bindings（如 `dialog-sheet` → `MessageBox`）。

## 缺信息门控（项目写码侧）

停问用户：

- 写码落点组件映射不唯一  
- `showcase-align` 多机型多图无法唯一  
- `dialog-sheet` 无法判断 MessageBox / 已有 Popup / 新建  
- image-read【需确认】或 asset 尺寸以谁为准未定  
- 数据态与目标图变体不一致；现有结构无法同时满足几何  
- Figma 缺 `node-id` 且无法定位  

有图时优先结合现有码关系解题，勿因「参照系是谁」空转提问。

图标：优先仓库已有图；无图策略见 design-measure 项目增量。

## 对话点名未对齐

### 纯文本点名（无图）

只改点名项；已确认 UI 锁定。有明确 px 时写出尺寸链并锁死外框/内容宽。

### 有图对齐

1. 先列【对话列举的未对齐项】与【对齐过程发现的未对齐项】  
2. 先改点名项，发现额外问题先列再改  
3. 几何与视觉都要对齐，禁止只改点名忽略图上差异  

## 调度顺序（交还 design-to-qml）

1. 有图 → **image-read**  
2. design-measure（项目增量）  
3. 若写码 → implement → verify  
4. measure-only / 不写码 → 停在读图（+ 可选 measure 增量）
