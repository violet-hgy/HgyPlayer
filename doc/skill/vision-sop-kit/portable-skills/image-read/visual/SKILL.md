---
name: image-read-visual
description: 读图·视觉实测（由 image-read 调度）。
disable-model-invocation: true
---

# Visual

禁止只出几何。覆盖：背景/前景/透明度、字号/字重、圆角、描边色宽、图标色（能稳定读到时）。

未稳定读到 → 写明，禁止猜。阴影只记备注。  
勿：外阴影当边框；墨迹高直接当字号不注明；不采样写死色值。
