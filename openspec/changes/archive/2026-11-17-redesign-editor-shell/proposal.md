# Proposal

## Why

当前 Editor 入口 `Scripts/Editor.sh` 启动完整 Unreal Editor，用户面对的是关卡编辑器、Content Browser、Details、Blueprint、Sequencer、插件和项目设置等通用功能。Epicode Studio 只是其中的可选页签，主菜单和布局仍然围绕 Unreal 的通用工作流，而不是围绕 Epicode 的场景、角色、物品、灯光和摄像编辑。

这与当前产品目标不一致：操作者只需要保留 UE 的场景渲染视口，并完成四类实体的放置、位置调整、属性调整和从场景删除；场景、角色、物品的导入和删除应主要来自本地库，而不是完整的 Content Browser 和资产编辑器。同时需要一个对话窗口，通过 DeepSeek Harness SDK 把自然语言转换为可校验、可预览、可撤销的编辑脚本。

本变更重新规划 Editor 的默认界面和交互边界，不实现新的渲染器，也不修改 `/usr/local/unreal`。

## What Changes

- **聚焦编辑器外壳**：新增默认的 Epicode 聚焦模式，保留 UE 场景渲染视口和选择/变换 gizmo，隐藏或禁用 Unreal Editor 的通用菜单、工具栏、主页签、Content Browser、Details、Blueprint、Sequencer、插件和项目设置等入口。旧版完整 Editor 只通过显式 `--legacy` 保留，用于维护和排障。
- **只保留四类交互编辑**：角色、物品、灯光、摄像的放置、选择、移动/旋转/缩放、属性调整和从场景删除。移除层级编辑、环境编辑、材质图、蓝图、动画编辑、地形/植被、PIE、渲染队列和通用资产编辑。
- **按 Epicode Studio 能力组织主菜单**：场景、角色、物品、灯光、摄像、本地库、编辑、视图、对话、帮助；不再以 Unreal 的 File/Edit/Window/Tools 通用菜单为主。
- **本地库**：提供场景、角色、物品的本地库浏览、搜索、导入、删除和放置。场景来自 `Saved/Epicode/Scenes`，角色和物品来自配置的本地内容根；删除场景走回收目录，删除库条目默认不删除资产文件。
- **对话窗口**：右侧可折叠对话面板，支持流式回复、取消、重试、清空和会话历史；引入 DeepSeek Harness SDK，通过适配层把自然语言转换为 Epicode 编辑脚本。
- **编辑脚本闭环**：模型只输出受版本约束的 JSON 编辑脚本，不执行任意 Python/Lua/控制台命令。脚本经过 schema、方法白名单、资产路径、实体引用和数量限制校验，先预览差异，再由用户确认后以单次事务应用，可一次撤销，并写入审计日志。
- **兼容与迁移**：`Editor.sh` 默认启动聚焦模式；`Editor.sh --legacy` 启动旧版完整 Editor。旧 Studio 九页签作为兼容模式保留，不再作为默认工作区；后续变更再决定是否归档旧界面规格。

## Capabilities

### New Capabilities

- `focused-editor`: 聚焦编辑器外壳、场景视口、四类实体编辑范围、Epicode 主菜单、布局、快捷键、旧版兼容入口和界面响应性。
- `local-library`: 本地场景/角色/物品库的索引、浏览、搜索、导入、删除、放置、冲突处理和路径安全。
- `conversational-editing`: 对话窗口、DeepSeek Harness SDK 适配、上下文构建、编辑脚本生成、校验、预览、确认、应用、撤销、审计和隐私边界。

### Modified Capabilities

- `production-ui`: 默认工作区从完整 Unreal Editor 内的九页签 Studio 改为聚焦编辑器外壳；旧工作区只在 `--legacy` 模式保留。
- `scene-authoring`: 增加编辑脚本执行语义，包括 `script.validate`、`script.apply`、引用解析、单事务原子性、撤销和审计。

## Impact

- **新增代码**：`EpicodeEditorShell`（Editor 模块，聚焦外壳、菜单、视口宿主、状态栏）、`EpicodeAssistant`（对话、DeepSeek 适配、上下文和脚本生成）、`EpicodeCore` 的编辑脚本执行器、`EpicodeImport` 的本地库索引与导入删除服务。
- **修改代码**：`EpicodeStudio` 的页签注册和默认入口、`Scripts/Editor.sh` 的 `--focused/--legacy` 参数、`DefaultEpicode.ini` 的库根、对话后端和脚本限制配置。
- **引擎约束**：不修改 `/usr/local/unreal`。Unreal Editor 的底层通用功能无法在不 fork 引擎的情况下真正删除，本变更通过自定义顶层窗口、隐藏默认主窗口、关闭默认页签和限制快捷键实现“界面级移除”；维护入口保留在 `--legacy`。
- **外部依赖**：DeepSeek Harness SDK 是 Python 包 `deepseek-harness-sdk`。Unreal 不直接导入 Python，改为由 `Scripts/Assistant/DeepSeekHarnessSidecar.py` 在隔离 venv 中加载 SDK，通过 stdio JSONL（默认）或本地 HTTP 与 Unreal 通信；SDK 版本在实现前固定，公开 PyPI 当前查不到该包，需要提供安装索引、凭据或 wheel。密钥只从环境变量或 `Saved/Epicode/` 下的 0600 文件读取。
- **测试**：新增 `Epicode.EditorShell.*`、`Epicode.Library.*`、`Epicode.Assistant.*`、`Epicode.Script.*`，覆盖外壳启动、默认页签隐藏、四类实体编辑、库导入删除、脚本校验/预览/应用/撤销、对话后端失败和 100 ms 响应预算。
- **文档**：新增 `Docs/EditorShell.md`、`Docs/Assistant.md`，更新 `Docs/Studio.md`、`Docs/Usage.md`、`Docs/Architecture.md`、`Docs/Operations.md` 和 `Docs/Acceptance.md`。
- **非目标**：不 fork Unreal Editor；不实现新的渲染管线；不实现蓝图/材质/Sequencer/地形/植被/PIE/渲染队列/完整 Content Browser；不做云端资产库；不做模型微调；不允许模型执行任意代码；不在本变更中实现多用户协同对话。
