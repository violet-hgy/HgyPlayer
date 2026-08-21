---
name: image-read-geometry
description: 读图·几何实测（由 image-read 调度）。
disable-model-invocation: true
---

# Geometry

- 原点左上；先写整图像素、参照系、**scale**（设计宽/截图宽）  
- 必测：整图边界、目标外框、padding、内容宽、border（色+宽，无则「无」）、radius、邻接 gap  
- 整页图加：距内容区右/底/左/顶（锚定边）  
- 描边周缘采样；阴影≠描边  
- 禁止大画布留白当控件宽；未换算不得称设计 px  

工具见 `../gates/SKILL.md`。
