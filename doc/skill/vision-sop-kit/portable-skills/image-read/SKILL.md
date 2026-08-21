---
name: image-read
description: >-
  【读图·可移植】PNG/截图/切图像素级识图与结构化报告。禁止沉默猜测。
  不写代码、不绑定 GUI 语言或业务仓库。由宿主 SOP（如 design-to-qml）显式调度；
  勿作为 Cursor 自动技能根目录挂载。
disable-model-invocation: true
---

# 读图 Image-Read（复合 · 总调度）

## 定位

```text
读图 (本包)  →  结构化像素报告
写码         →  specializations/qml（或其它语言特化）+ 项目 bindings
```

**只读图、只出数。** 不实现 UI，不改 Figma，不落资源清单。

本包是 **Vision SOP Kit 的主能力**；QML 等语言处理见同级 `specializations/`。
## 触发（由宿主调度，勿自触发）

宿主在「有图要量 / 只要识图报告」时 Read 本文件，再按下列顺序读子 skill。

## 嵌套结构

```text
image-read/
├── SKILL.md
├── classify/
├── geometry/
├── visual/
├── structure/
├── gates/
└── output/
```

## 调度顺序（强制）

1. `./gates/SKILL.md`
2. `./classify/SKILL.md` → 输出【识图场景】
3. `./geometry/SKILL.md`
4. `./structure/SKILL.md`（场景需要时）
5. `./visual/SKILL.md`
6. `./output/SKILL.md` → 完整报告

结束后把报告交给宿主写码 SOP；本包职责终止。

## 硬规则

- 禁止未读图写参数表；禁止「大约」当实测  
- 猜测必须【暂时猜测数值占用】  
- 有图必须先有【识图场景】  
- 阴影只记不当描边  
- 设计 px + 注明 scale；用户口头 px 优先  
- **禁止**本包内写码  

## 迁移

整目录复制到其它 Agent 即可；宿主改「Read 路径」指向本 `SKILL.md`。
