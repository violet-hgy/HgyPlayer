---
name: specialization-qml
description: >-
  【特化包·QML】消费 image-read 像素报告，落到 Qt Quick/QML。
  不负责识图。由项目 SOP 显式调度；勿作为纯识图入口自动触发。
disable-model-invocation: true
---

# Specialization · QML

## 定位

```text
image-read 报告  →  本包写 QML  →  项目 bindings（路径/主题/翻译）
```

**前置**：已有 image-read 输出（或无图任务且用户只改已知几何/文案）。  
**禁止**：在本包内重新测图、跳过报告猜间距。

## 调度顺序

1. 确认宿主已提供 **project-bindings**（目录、`Colors`/`qsTr`、组件映射）  
2. `./implement/SKILL.md` — 落码  
3. `./verify/SKILL.md` — qmllint、尺寸链、布局复校  

## 报告字段 → QML（速查）

| 读图字段 | 典型落点 |
| --- | --- |
| 控件外框 | `width`/`height` 或 Layout min/preferred/max（经 bindings 的 `Metrics.dp`） |
| 距内容区边 | `anchors.*Margin` / `Layout.margins` |
| itemSpacing | 仅 `ListView.spacing` / 列表向 Column spacing |
| 列表外距 | 容器/父 Layout margin，禁止塞进 `ListView.spacing` |
| border / radius / 色 | `border.*` / `radius` / bindings 主题色 |
| discrete-repeat | 一条 delegate + model，禁止按条数复制 |
| dialog 面板外框 | Dialog/Popup 内容宽高，不含遮罩 |
| showcase 显示盒 | 按报告推导宽高与 topMargin；勿盲 centerIn |

细节见 `implement/`、`verify/`。

## 与项目示例

Mojhon 示例 bindings：`../../project-sop/examples/mojhon/project-bindings.md`  
接入真实项目时改为该项目 bindings 路径。
