# Epicode 聚焦工作台

`Editor.sh` 默认启动 Epicode 聚焦工作台。它用一个自绘 `SWindow` 承载 `SLevelEditor` 视口，隐藏 stock Unreal 主窗口、主标签、工具栏、Content Browser 和通用编辑入口，只保留场景布景需要的四类实体、本地库、检查器、对话和脚本应用闭环。

旧完整 Editor 保留为维护通道：

```bash
cd engine
./Scripts/Editor.sh --legacy
```

## 1. 启动模式

| 命令 | 行为 |
| --- | --- |
| `./Scripts/Editor.sh` | 聚焦工作台（默认） |
| `./Scripts/Editor.sh --focused` | 显式聚焦工作台 |
| `./Scripts/Editor.sh --legacy` | 完整 Editor + 旧 Studio/Multiplayer/Director 页签 |
| `./Scripts/Editor.sh --help` | 参数帮助 |
| `./Scripts/Editor.sh --bad` | 退出码 2 |

聚焦模式通过 `-EpicodeFocusedEditor` 标记启用。`EpicodeStudio` 在该标记下不注册 `EpicodeStudio`、`EpicodeMultiplayer`、`EpicodeDirectorWorkspace` 三个 nomad 页签；`--legacy` 下三者仍然注册并可从 Window 菜单打开。

## 2. 布局

```text
┌──────────────────────────────────────────────────────────────┐
│ 场景 角色 物品 灯光 摄像 本地库 编辑 视图 对话 帮助          │
├──────────────┬────────────────────────────┬──────────────────┤
│ 对象列表     │                            │ 检查器           │
│ 本地库       │        SLevelViewport      │ 对话 / 脚本卡片  │
│ 场景库       │                            │                  │
├──────────────┴────────────────────────────┴──────────────────┤
│ 场景 · 选择 · 变换模式 · 库/对话状态 · revision · 保存状态  │
└──────────────────────────────────────────────────────────────┘
```

- 左右面板可折叠，宽度可拖动；布局、最近场景和视口镜头保存到 `Saved/Epicode/Editor/layout.json`。
- 视口支持选择、移动/旋转/缩放 gizmo、数值变换、聚焦选中、Lit/Unlit/Wireframe、网格和相机速度。
- 无场景、空场景、无选择、库为空、对话不可用、视口初始化失败都有空状态；视口失败可点击“重试”，仍失败时使用 `--legacy`。

## 3. 菜单

聚焦模式只注册 10 个主菜单：

| 菜单 | 内容 |
| --- | --- |
| 场景 | 新建、打开、保存、另存为、关闭、最近场景、从 `scene.json` 导入、删除场景 |
| 角色 | 从本地库放置、导入到本地库、从本地库移除、删除选中实体 |
| 物品 | 从本地库放置、导入到本地库、从本地库移除、删除选中实体 |
| 灯光 | 创建方向光、点光、聚光、矩形光；从本地库放置 |
| 摄像 | 创建相机、设为活动相机、删除相机 |
| 本地库 | 场景库、资源库、刷新、定位、恢复隐藏资源、删除文件 |
| 编辑 | 撤销、重做、选择/移动/旋转/缩放、删除、聚焦选中、框选全部 |
| 视图 | Lit/Unlit/Wireframe、网格、相机速度、左右面板 |
| 对话 | 新建、清空、删除当前会话、设置、重发、停止 |
| 帮助 | 快捷键、启动模式说明、`--legacy` 提示 |

没有环境、渲染、API、Content Browser、蓝图、材质、Sequencer、地形、植被、PIE、插件和项目设置入口。

## 4. 快捷键

| 键 | 动作 |
| --- | --- |
| `Q` / `W` / `E` / `R` | 选择 / 移动 / 旋转 / 缩放 |
| `F` | 聚焦选中 |
| `Delete` | `entity.delete` |
| `Ctrl+Z` | 撤销 |
| `Ctrl+Shift+Z` / `Ctrl+Y` | 重做 |
| `Ctrl+S` | `scene.save` |
| `Ctrl+Enter` | 发送对话消息 |
| `Shift+Enter` | 对话输入换行 |

快捷键由聚焦视口的子命令列表拦截，`Delete`/`Ctrl+S`/撤销不会绕过 Epicode 文档和命令注册表。

## 5. 四类实体

| 类型 | 创建/放置 | 检查器 |
| --- | --- | --- |
| 角色 | 本地库角色放置、FBX/GLTF/GLB 导入 | 骨骼网格、动画、播放/暂停、速度、可见性 |
| 物品 | 本地库物品放置、FBX/OBJ/GLTF/GLB 导入 | 静态网格、材质、物理、可见性 |
| 灯光 | 方向/点/聚光/矩形光 | 类型、强度、颜色、衰减、锥角、阴影、可见性 |
| 摄像 | 创建、设为活动相机 | FOV、焦距、光圈、对焦距离、活动相机 |

所有变换通过 `entity.setTransform`；检查器支持世界/局部坐标和位置 10 单位、旋转 15 度吸附。删除实体保留源资产，删除后一次撤销恢复。

## 6. 本地库

- 索引：`Saved/Epicode/Library/index.json`，场景/角色/物品三类条目。
- 场景扫描：`Saved/Epicode/Scenes`，显示名称、短 sceneId、实体数、修改时间和可用状态。
- 资产扫描：`/Game/Epicode/Imported/Characters`、`/Game/Epicode/Imported/Props` 和配置根；缺失资产标记 `available=false` 并禁用放置。
- 场景导入只支持 `scene.json`；重复 sceneId 可选择覆盖、副本或取消。
- 资产导入通过 `import.submit` 异步任务；左栏显示文件、阶段、百分比、进度条和取消按钮。Interchange 本体仍在游戏线程同步执行，取消只对尚未开始的任务立即生效。
- 删除角色/物品默认只移除索引；“删除文件”先检查当前和已保存场景引用，无引用时删除 `.uasset` 和索引。
- 资源行可拖到视口，落点取鼠标射线与 `Z=0` 的交点；不在视口内时退回相机前方。

## 7. 对话与脚本

- 对话历史：`Saved/Epicode/Chat/<conversationId>.json`，启动恢复最近会话。
- 后端：`mock`（离线）、`deepseek_sidecar`（隔离 venv + DeepSeek Harness SDK）、`openai_compat`（DeepSeek HTTP/SSE）。
- API Key：`DEEPSEEK_API_KEY` 环境变量或 `Saved/Epicode/deepseek.env`，UI 只显示掩码。
- 回复中的 JSON 脚本自动经过 `script.validate`，助手消息下显示脚本卡片：操作数、影响实体、摘要、错误、警告、动作/字段/资产预览、破坏性标记和“定位”。
- “应用”在单个事务中执行；破坏性操作弹 Yes/No；失败回滚，成功刷新对象列表/检查器/视口并选中首个受影响实体。一次 `Ctrl+Z` 撤销整个脚本。
- “取消”清除待应用脚本，不修改场景。
- 非法 JSON 或校验失败自动修复，最多 `AssistantMaxRepairAttempts` 次；耗尽后停止。
- 审计：`Saved/Epicode/Script/audit.jsonl`，单行 JSON，不记录完整脚本和密钥。

详见 [Assistant 与编辑脚本](Assistant.md)。

## 8. 范围限制

聚焦模式有意不提供：

- 层级重挂接、分组、环境、材质图、蓝图/动画蓝图、Sequencer、地形、植被、物理约束和导航编辑。
- Content Browser、PIE、渲染队列和通用资产编辑入口。
- 任意控制台、Python、Lua、文件或网络命令作为脚本 operation。

需要这些功能时使用 `--legacy`。`--legacy` 与聚焦模式共享同一套命令注册表和场景文档，不是另一套数据格式。

## 9. 故障排查

| 现象 | 处理 |
| --- | --- |
| 视口初始化失败 | 点击空状态“重试”；仍失败用 `--legacy`，并查看 `Saved/Logs/editor-shell-workbench.log` |
| 对话不可用 | 检查 `AssistantBackend`、`DEEPSEEK_API_KEY`/`deepseek.env` 和网络；sidecar 先运行 `Scripts/Assistant/SetupSidecar.sh`；`mock` 可离线验证 UI |
| 脚本被拒绝 | 查看卡片错误；确认方法在白名单、资产在 `/Game/` 且存在、`clientId` 只引用更早操作 |
| 脚本需要确认 | 删除、`camera.activate` 等破坏性操作必须确认；取消不会修改场景 |
| 应用失败但场景未变 | 事务已回滚；查看 `Saved/Epicode/Script/audit.jsonl` 的 `errorCode` |
| 撤销没有恢复 | 确认聚焦模式已安装事务 sink；`--legacy` 使用编辑器原生事务 |
| 本地库场景/资产缺失 | 在本地库点“刷新”；检查 `Saved/Epicode/Scenes` 和项目 `Content` 目录 |
| 测试后出现 `* Test` 场景 | 自动化测试夹具；删除对应 `Saved/Epicode/Scenes/<id>` 后刷新索引 |

## 10. 验证

```bash
cd engine
./Scripts/Build.sh
./Scripts/Test.sh
openspec validate --all --strict
```

- 全量自动化：`Epicode.*` 186+ 项，0 失败，日志 `Saved/Logs/test.log`。
- 聚焦/legacy 页签：`Epicode.EditorShell.LegacyTabs` 在两种启动参数下验证页签注册与模式一致。
- 工作台证据：`Saved/Logs/editor-shell-workbench.log`、`Saved/Screenshots/EditorShell*.png`。
- 脚本证据：`Saved/Logs/script-test.log`、`assistant-script-flow.log`。
