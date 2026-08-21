---
name: image-read-structure
description: 读图·结构（列表/弹层/浮层/透明资源）（由 image-read 调度）。
disable-model-invocation: true
---

# Structure

## discrete-repeat（`delegate-row`）

≥2 同构近等距不相连项 → `【列表模式】discrete-repeat — N`。  
测：第1/2项 → **itemSpacing** → 首项外距 → 模板行内。外距与内距分栏，禁止一个笼统 spacing。

## dialog-sheet

遮罩 vs 面板分列；面板外框不含遮罩。

## overlay-badge

外框、border、距内容区边、内 padding。

## showcase-align

资源透明 pad + 画面墨迹；报告推导显示盒 = 资源×scale_fit（不绑定框架 API）。

## asset-compare

固有尺寸/宽高比/显示框；比例同边长不同 → 【需确认】，不擅自选定。
