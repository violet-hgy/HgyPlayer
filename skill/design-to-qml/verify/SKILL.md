---
name: design-verify
description: 对 MojhonAssistantQt 设计落地结果做收尾自检：QML 视觉检查、主题与翻译检查、组件目录和规则同步检查。供 design-to-qml 调度时读取。目录层级不影响规则，按 dispatcher 读取嵌套或平铺路径均可。
disable-model-invocation: false
---

# Design Verify

## 作用

负责**落地后的收尾检查**，避免“改完能跑，但不完整”。

## 必查项

- 是否已输出【差异 / 不确定】
- 是否完成 QML 落地自检
- 是否遗漏 `Colors.*` / `ImagePaths` / `ThemeImage`
- 是否遗漏 `qsTr(...)`
- 是否同步 `app_zh-CN.ts` / `app_ja_JP.ts`
- 是否需要更新 `docs/qml-components-catalog.md`
- 是否有资源未进 `qrc`

## 对齐任务额外检查

- 用户点名项是否全部处理
- 对齐过程中新增发现项是否已先列再改
- 若有无法稳定读到的视觉参数，是否明确告知
- 若识别到阴影，是否仅记录未落地

## 输出要求

收尾回复至少包含：

- 已对齐的关键项
- 仍不确定 / 未稳定读到的项
- 若有额外修正，明确告知用户

## 禁止

- 改完不检查翻译
- 改组件却不更新组件目录
- 发现不确定项却沉默收尾
