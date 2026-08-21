---
name: image-read-output
description: 读图·报告模板（由 image-read 调度）。
disable-model-invocation: true
---

# Output

```text
【识图场景】…

【元数据】源 / 整图像素 / scale / 参照系 / 工具；流水线=读图

【可视结构】…
【像素参数表】| 字段 | 值 | 实测/推导/猜测 |
【视觉参数表】…
【结构专表】或「不适用」
【需确认】
【差异 / 不确定】
【暂时猜测数值占用】或「无」
```

完成：有场景、有外框+border 行、有视觉表（text-extract 除外）、结构专表齐、无未声明猜测、未夹带写码。
