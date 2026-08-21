# MojhonAssistantQt — Design→QML 项目硬绑定

> **本文件只放本仓库落点约定。** 测法、场景分流、通用 QML 布局写法见各流程 skill；换项目时优先改本文件，勿把路径/组件名散写回 measure/intake。

供 `design-to-qml` 调度：凡进入 **implement / verify / asset 落盘 / 改 QML**，必须先读本文件。

## 产品

- 名称：MojhonAssistantQt（手柄上位机）
- 目标：可复用组件 + 页面组装；**禁止按机型复制整页近似控件**
- UI 主题（`Colors` / `Theme`）≠ 手柄产品外观色

## 落码目录

| 项 | 约定 |
| --- | --- |
| 唯一可改 QML 树 | `qml/default_en_us/` |
| 通用组件 | `qml/default_en_us/components/` |
| 成对文件 | 有 Form 时成对改 `Xxx.qml` + `XxxForm.ui.qml` |
| 资源落盘 | `res/`（沿用现有图片目录结构） |
| 资源清单 | `res/resources.qrc` → 运行时 `qrc:/...` |

## 尺寸 / 字体 / 主题

| 项 | 约定 |
| --- | --- |
| 设计 px → 代码 | `Metrics.dp(设计 px)` |
| 字体 | 优先 `Fonts.*`，否则 `Microsoft YaHei UI` |
| 颜色 | `Colors.*`；禁止为对齐默认写死 hex（产品固定色除外） |
| 可换肤图 | `Theme.name` / `ThemeImage` / `ImagePaths`；勿写死 `images/default/` 可换肤路径 |
| 用法参考 | `qml/default_en_us/main/Main.qml` |

## 翻译

| 项 | 约定 |
| --- | --- |
| 用户可见文案 | 一律 `qsTr(...)` |
| 源字符串 | **英文**（用户给中文须先译英再写入） |
| 翻译文件 | `res/i18n/app_zh-CN.ts`、`res/i18n/app_ja_JP.ts` |
| 编译 | 改 ts 后必须 `lrelease` 出对应 `.qm`；`.qm` 须进 `resources.qrc` |
| 细则 | `.cursor/rules/qml-i18n-english-source.mdc` |

### 本机工具路径（可按环境改，勿抄死到其它机器却不声明）

```powershell
$lrelease = "C:\Qt\6.8.3\msvc2022_64\bin\lrelease.exe"
& $lrelease "D:\QtProjects\mojhonassistantqt\res\i18n\app_zh-CN.ts" "D:\QtProjects\mojhonassistantqt\res\i18n\app_ja_JP.ts"

& "C:/Qt/6.8.3/msvc2022_64/bin/qmllint.exe" "D:/QtProjects/mojhonassistantqt/qml/.../YourPage.qml"
```

环境门控仍走 `env-detect`（profile 含 i18n / measure）。

## 具名组件映射（场景 → 优先落点）

| 识图场景 / 意图 | 优先组件 / 符号 | 备注 |
| --- | --- | --- |
| `showcase-align` | `GamepadShowcase` 等产品主图 | 透明 pad + 整页墨迹；见 measure |
| `dialog-sheet` | `MessageBox` / 页面内已有 `Popup` | 默认变体勿回退；注意类曾用外框 **375** vs 默认 **391** |
| `overlay-badge` | 页面内浮层（如首页竞技模式胶囊） | 只改该浮层 margin/border |
| `delegate-row` | `ListView` / `Repeater` + delegate | 配置列表、宏列表等离散重复项 |
| `asset-swap` | 现有 `Image` / `ThemeImage` 源 | 比例同尺寸不同 → 问用户以谁为准 |
| 设置行 | 现有设置页行组件 / Form 行 | 可与 `delegate-row` 叠用 |

落点不唯一 → intake **缺信息门控**，停问用户。

## 本产品场景别名（帮助识图，非新场景码）

| 口语 / 图特征 | 场景码 |
| --- | --- |
| 手柄+底座主图位置/大小 | `showcase-align` |
| 配置列表、宏列表（不相连重复卡） | `delegate-row` |
| 确认/注意/导入结果弹窗 | `dialog-sheet` |
| 右下角竞技模式等胶囊 | `overlay-badge` |

## 文档与目录

| 项 | 路径 |
| --- | --- |
| 组件目录（改通用组件时） | `docs/qml-components-catalog.md` |
| 代码导航 | `.cursor/skills/mojhon-code-nav/SKILL.md` |
| 本绑定文档镜像 | `docs/skills/design-to-qml-project-bindings.md` |

## 同步

- 规范源：`.cursor/skills/design-to-qml/project-bindings.md`
- 改源后立即同步到 `docs/skills/design-to-qml-project-bindings.md`
