---
name: project-design-sop
description: >-
  【胶水模板】项目设计落地 SOP：调度 portable image-read（读图）与
  specializations/qml（写码），再叠加本项目 project-bindings。
  复制后改名（如 design-to-qml）并改路径。勿把通用识图写进本文件。
disable-model-invocation: false
---

# 项目设计 SOP（薄胶水）

## 架构

```text
本 SOP（项目）
  ├─ project-bindings          本仓库目录/主题/翻译/组件
  ├─ intake                    是否写码、点名、项目缺信息
  ├─ ../portable-skills/image-read     【读图】
  └─ ../specializations/qml            【QML 特化】
```

相对本文件的默认邻居布局见仓库根 `README.md`。迁入真实项目后，把路径改成该仓库绝对/相对路径。

## 调度

### 0. bindings（改码 / 落资源前）

读本项目的 `project-bindings.md`（示例：`examples/mojhon/project-bindings.md`）。

### 1. intake

读本项目 intake（示例：`examples/mojhon/intake/SKILL.md`）。

### 2. 读图（有图时强制）

Read 并执行：`portable-skills/image-read/SKILL.md`  
（gates → classify → geometry → structure → visual → output）

产出完整报告后再写码。无图仅改代码则跳过。

### 3. 写码（QML 特化）

Read：`specializations/qml/SKILL.md` → implement → verify。  
消费读图报告 + bindings；**禁止重新发明测法**。

### 4. 只识图

intake（可不写码）→ image-read → **停止**（不进 qml 特化）。

## 硬规则

- 见图不直接改 QML；有图先读图报告  
- 通用列表/弹层/透明图规则只在 image-read  
- 主题/路径/翻译只在 bindings  
- Layout/尺寸链/ListView.spacing 映射只在 qml 特化  
