---
name: design-implement-qml
description: 将已测得的设计参数落到 MojhonAssistantQt 的 QML：页面、组件、资源、qsTr、主题和布局实现。供 design-to-qml 调度时读取。目录层级不影响规则，按 dispatcher 读取嵌套或平铺路径均可。
disable-model-invocation: false
---

# Design Implement QML

## 作用

只负责**把已确认参数写进 QML / 资源 / 翻译**，不再重新发明测量规则。

## 项目约束

- 只改 `qml/default_en_us/`
- 优先复用 `qml/default_en_us/components/`
- 视觉页优先 Layout，不要满页绝对坐标
- 色彩优先 `Colors.*`
- 图片优先 `ThemeImage` / `ImagePaths`
- 文案一律 `qsTr(...)`
- 英文做 source；同步 `app_zh-CN.ts` / `app_ja_JP.ts`

## 实现顺序

1. 先用现有码结构求解
2. 优先微调几何参数，不先推翻结构
3. 再落视觉参数：颜色、圆角、描边、字号
4. 最后接资源、翻译、文档

## 适合放在这里的改动

- `spacing` / `padding` / `margin`
- `preferredWidth` / `preferredHeight`
- `Layout.*` 对齐
- `color` / `radius` / `border`
- 新增局部资源引用
- 文案和翻译上下文修正

## 禁止

- 借对齐顺手重构
- 为了省事复制整页近似控件
- 只落几何，漏掉已确认的可视参数
