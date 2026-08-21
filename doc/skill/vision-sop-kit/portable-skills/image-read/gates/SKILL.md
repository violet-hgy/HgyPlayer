---
name: image-read-gates
description: 读图·环境与反幻觉（由 image-read 调度）。
disable-model-invocation: true
---

# Gates

实测优先；缺工具默认停；占位必须声明。

建议：Python3 + Pillow（绝对路径；避 WindowsApps 占位符）。

```text
OK|<tool>|<abs-or-id>|<detail>
FAIL|<tool>|<reason>
```

宿主可先跑自有 env（如 `env-detect` profile `measure`），再进本包；结果仍须符合上式语义。

```text
【暂时猜测数值占用】
- 字段：…
- 猜测值：…
- 无法获取精确数值的原因：…
- 待补齐后重测：…
```

含占位不得写「已像素对齐」。
