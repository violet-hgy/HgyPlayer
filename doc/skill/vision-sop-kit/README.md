# Vision SOP Kit（桌面导出 · 方案 A）

识图为主、QML 为特化包、项目 SOP 做胶水。整目录可拷到其它 Agent。

```text
vision-sop-kit/
├── README.md                          ← 本文件
├── portable-skills/
│   └── image-read/                    ← 【主】读图复合包（语言无关）
├── specializations/
│   └── qml/                           ← 【特化】消费读图报告 → 写 QML
│       ├── SKILL.md
│       ├── implement/
│       └── verify/
└── project-sop/
    ├── SKILL.md                       ← 【胶水】薄 SOP 模板
    └── examples/mojhon/               ← 本仓库示例 bindings + intake
        ├── project-bindings.md
        └── intake/SKILL.md
```

## 流水线

```text
只识图     →  portable-skills/image-read → 结构化报告（可结束）

落 QML     →  project-sop
               → image-read（有图时）
               → specializations/qml
               → examples/.../project-bindings
```

## 安装建议

| 包 | 建议放置 | 说明 |
| --- | --- | --- |
| `image-read` | 仓库 `.cursor/portable-skills/`（勿进自动 skills） | 由 SOP 显式 Read |
| `specializations/qml` | `.cursor/portable-skills/specializations/qml` 或 skills 下且 `disable-model-invocation: true` | 仅 SOP 调度 |
| `project-sop` | 改名后进 `.cursor/skills/`（如 `design-to-qml`） | 项目自动触发入口 |

## 刻意不做

- 不在本包内保留与 image-read 重复的全量 `design-measure`
- QML 特化不负责量像素；项目 bindings 不写通用测法
