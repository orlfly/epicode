# Tasks

## 1. 可行性与外壳 PoC

- [x] 1.1 在 `engine/Plugins` 下创建 `EpicodeEditorShell` Editor 模块骨架，注册 `StartupModule`/`ShutdownModule` 和 `-EpicodeFocusedEditor` 开关，验证模块能在 UE 5.8.4 编辑器目标加载且不影响 `--legacy`
- [x] 1.2 编写视口宿主 PoC：创建自定义 `SWindow`，用 `SEditorViewport` + `FLevelEditorViewportClient` 显示当前编辑世界，验证选择、移动/旋转/缩放 gizmo、撤销和退出流程；记录成功/失败结论与引擎 API 限制
- [x] 1.3 验证隐藏 stock 主窗口、菜单栏、工具栏、主标签和状态栏的 API 路径；如果方案 1 不可行，实现全屏 nomad 页签回退并记录差异
- [x] 1.4 实现 `Scripts/Editor.sh --focused/--legacy` 参数解析，默认聚焦，`--legacy` 保持现有启动；验证无效参数返回退出码 2
- [x] 1.5 编写 PoC 验收日志：窗口截图、视口 gizmo、默认页签隐藏、`--legacy` 回退、退出无残留进程；写入 `Saved/Logs/editor-shell-poc.log`

## 2. 聚焦外壳与菜单

- [ ] 2.1 实现 `SEpicodeEditorShell` 根控件：顶部主菜单、中央视口、左侧库/对象列表、右侧对话、底部状态栏，支持左右面板折叠和宽度调整
- [ ] 2.2 实现主菜单：场景、角色、物品、灯光、摄像、本地库、编辑、视图、对话、帮助；移除环境、渲染、API、Content Browser、蓝图、材质、Sequencer、地形、植被、PIE、插件和项目设置入口
- [ ] 2.3 实现场景菜单：新建、打开、保存、另存为、关闭、最近场景、从本地库导入/删除场景
- [ ] 2.4 实现角色/物品/灯光/摄像菜单：从本地库放置、导入到本地库、从本地库删除、删除选中实体、创建四类灯光、创建相机和设为活动相机
- [ ] 2.5 实现编辑/视图菜单：撤销、重做、选择/移动/旋转/缩放、删除、聚焦选中、框选全部、Lit/Unlit/Wireframe、网格、相机速度
- [ ] 2.6 实现快捷键：Q/W/E/R、F、Delete、Ctrl+Z、Ctrl+Shift+Z/Ctrl+Y、Ctrl+S，并拦截 Unreal 通用快捷键
- [ ] 2.7 实现布局持久化：左右面板宽度、折叠状态、最近场景和视口镜头写入 `Saved/Epicode/Editor/layout.json`，重启恢复
- [ ] 2.8 实现状态栏：当前场景、选择实体、revision、变换模式、库/对话后端状态、保存状态
- [ ] 2.9 实现空状态：无场景、空场景、无选择、库为空、对话后端不可用、视口初始化失败，提供新建/打开/重试/`--legacy` 入口

## 3. 四类实体编辑

- [ ] 3.1 实现最小对象列表：角色/物品/灯光/摄像分类、名称搜索、类型过滤、视口双向选择，不提供层级拖放和重挂接
- [ ] 3.2 实现四类实体检查器：角色骨骼网格/动画/播放/速度/可见性，物品静态网格/材质/物理/可见性，灯光类型/强度/颜色/衰减/阴影，摄像视场角/焦距/光圈/对焦/活动相机
- [ ] 3.3 实现变换编辑：视口 gizmo、数值输入、世界/局部坐标、网格吸附，所有修改通过 `entity.setTransform`
- [ ] 3.4 实现放置：从本地库放置角色/物品，从菜单放置四类灯光和相机，放置后自动选中并进入移动模式
- [ ] 3.5 实现从场景删除：`entity.delete`、灯光/相机删除命令、源资产保留、一次撤销恢复
- [ ] 3.6 实现范围限制：移除/禁用层级重挂接、分组、环境、材质图、蓝图、动画蓝图、Sequencer、地形、植被、物理约束、导航、PIE、渲染队列和通用资产编辑入口
- [ ] 3.7 实现 `Epicode.EditorShell.Viewport`、`Scope`、`Inspector` 自动化测试，覆盖选择、gizmo、属性、删除、撤销和禁止入口

## 4. 本地库

- [ ] 4.1 实现 `FEpicodeLocalLibrary` 索引模型和 `Saved/Epicode/Library/index.json` 读写，支持场景/角色/物品三类条目、schema 版本、损坏备份和重建
- [ ] 4.2 实现场景扫描：读取 `Saved/Epicode/Scenes`，显示名称、sceneId、levelPath、实体数、修改时间和可用状态
- [ ] 4.3 实现角色/物品扫描：通过资产注册表扫描 `/Game/Epicode/Imported/Characters`、`/Game/Epicode/Imported/Props` 和配置根，标记缺失资产
- [ ] 4.4 实现库面板：分类、搜索、标签过滤、刷新、定位、空状态和不可用状态
- [ ] 4.5 实现场景导入：文件对话框、schema 校验、重复 sceneId 覆盖/另存/取消、写入场景库并选中
- [ ] 4.6 实现角色/物品导入：复用 Interchange/OBJ 流程、目标路径、骨骼/静态网格校验、进度、取消、失败清理
- [ ] 4.7 实现库删除：场景走 `scene.delete` 回收站；角色/物品默认只删索引；显式删除资产时检查当前和已保存场景引用
- [ ] 4.8 实现放置：双击/拖放到视口，调用 `character.spawn`/`prop.spawn`，初始位置用视口中心或鼠标交点
- [ ] 4.9 实现路径安全：ImportRoots、`/Game/` 目标、目录穿越、符号链接逃逸、扩展名和大小限制
- [ ] 4.10 实现冲突处理：SHA-1 复用、选项冲突覆盖/副本/取消、名称冲突重命名/取消
- [ ] 4.11 编写 `Epicode.Library.Index`、`Import`、`Delete`、`Place`、`PathSafety` 自动化测试

## 5. 对话与 DeepSeek 后端

- [ ] 5.1 定义 `IConversationBackend`、`FConversationRequest`、`FConversationEvent`、`IConversationStream`，把 UI 与 SDK/网络解耦
- [ ] 5.2 实现 `EpicodeAssistant` 模块和右侧对话面板：消息历史、输入框、发送/停止/重试/清空/新建会话/设置
- [ ] 5.3 确认 `deepseek-harness-sdk` 的 Python 版本、SDK 版本、许可证和安装来源（私有索引/凭据/wheel）；创建 `Scripts/Assistant/requirements.txt` 并固定版本，公开 PyPI 当前查不到该包，未拿到来源前保持待定
- [ ] 5.4 实现 `Scripts/Assistant/DeepSeekHarnessSidecar.py`：隔离 venv、SDK import、JSONL stdio 协议、流式 delta/done/error、cancel、ping、shutdown、空闲退出和 stderr 诊断
- [ ] 5.5 实现 `FDeepSeekHarnessSidecarBackend`：启动/复用 sidecar、`FPlatformProcess` 管道读写、请求 ID 路由、超时、崩溃重启、取消和不可用降级
- [ ] 5.6 实现可选 HTTP 模式：sidecar `/health`、`/v1/chat` SSE、`/v1/cancel`，本地 token 鉴权，默认仍用 stdio
- [ ] 5.7 实现 `FOpenAICompatibleChatBackend` 作为 DeepSeek API 兼容/降级后端，复用同一接口
- [ ] 5.8 实现 `FMockConversationBackend`，支持脚本化流式回复、超时、取消、崩溃和错误，供自动化测试
- [ ] 5.9 实现配置：`bEnabled`、`Backend`、`SidecarCommand`、`SidecarScript`、`SidecarMode`、`Endpoint`、`Model`、`ApiKeyFile`、`MaxContextTokens`、`MaxRepairAttempts`、`RequestTimeoutSeconds`、`SidecarIdleSeconds`
- [ ] 5.10 实现密钥读取和掩码：环境变量/`Saved/Epicode/deepseek.env` 0600，sidecar 不把密钥写入命令行，日志/审计/错误脱敏
- [ ] 5.11 实现上下文构建：场景摘要、当前选择、本地库摘要、命令 schema、约束、最近对话；超限本地裁剪
- [ ] 5.12 实现对话历史持久化：`Saved/Epicode/Chat/<conversationId>.json`，新建/清空/删除/恢复
- [ ] 5.13 实现后端不可用/超时/禁用/sidecar 崩溃的降级：对话面板显示错误，手工编辑、视口、库和保存不受影响
- [ ] 5.14 编写 `Epicode.Assistant.Chat`、`SidecarProtocol`、`BackendFailure`、`ContextBudget`、`Secrets` 自动化测试

## 6. 编辑脚本与 AI 应用闭环

- [ ] 6.1 定义编辑脚本 schema：`schemaVersion`、`summary`、`requiresConfirmation`、`operations`、`clientId`、`method`、`params`
- [ ] 6.2 实现 `script.validate` 命令：版本、数量、方法白名单、参数类型/范围、实体存在性、`$clientId` 引用、资产路径和权限校验
- [ ] 6.3 实现 `script.apply` 命令：单事务执行、`clientId -> entityId` 映射、部分失败回滚、一次撤销、结果摘要
- [ ] 6.4 实现脚本白名单和限制：24 个允许方法、最多 50 operations/100 实体、禁止嵌套事务/导入/渲染/系统/导演/沙盒/控制台/Python/Lua/文件/网络
- [ ] 6.5 实现脚本预览：新建/修改/删除实体、字段差异、资产路径、影响数量、警告、视口高亮
- [ ] 6.6 实现确认和拒绝：删除/批量删除/覆盖实体/修改活动相机二次确认；取消不改场景
- [ ] 6.7 实现生成失败和修复：非法 JSON、schema 错误、校验错误显示；最多 `MaxRepairAttempts` 次自动修复；耗尽后停止
- [ ] 6.8 实现审计：`Saved/Epicode/Script/audit.jsonl` 记录时间、脚本摘要、operation 数、受影响实体、结果、错误码、耗时；写入失败只返回警告
- [ ] 6.9 实现对话面板的“预览/应用/取消”交互，应用后刷新视口、对象列表和检查器
- [ ] 6.10 编写 `Epicode.Script.Validate`、`Apply`、`Undo`、`Reference`、`Safety`、`Audit` 自动化测试

## 7. 兼容、测试与文档

- [ ] 7.1 把 `Scripts/Editor.sh` 默认切到聚焦模式；`--legacy` 保留旧版完整 Editor 和九页签 Studio
- [ ] 7.2 验证 `--legacy` 下现有 `Epicode Studio`、`Epicode Multiplayer`、`Epicode Director Workspace` 仍可打开，聚焦模式不注册这些页签
- [ ] 7.3 运行 `Epicode.EditorShell.Launch`、`Viewport`、`Scope`、`Responsiveness`、`Epicode.Library.*`、`Epicode.Assistant.*`、`Epicode.Script.*` 测试并记录结果
- [ ] 7.4 执行手工验收：启动聚焦模式，放置/移动/改属性/删除四类实体，导入/删除库条目，对话生成脚本、预览、应用、撤销；保存截图和日志
- [ ] 7.5 编写 `Docs/EditorShell.md`：启动、布局、菜单、快捷键、四类实体、范围限制、`--legacy`、故障排查
- [ ] 7.6 编写 `Docs/Assistant.md`：DeepSeek 配置、密钥、上下文、脚本 schema、白名单、预览/应用/撤销、审计、隐私
- [ ] 7.7 更新 `Docs/Studio.md`、`Docs/Usage.md`、`Docs/Architecture.md`、`Docs/Operations.md`、`Docs/Acceptance.md`，标注聚焦模式为默认、旧 Studio 为 legacy
- [ ] 7.8 评估旧 `production-ui` 九页签规格：保留为 legacy 行为，还是后续变更归档；在评审记录中给出结论
- [ ] 7.9 运行 `openspec validate --all --strict` 和项目自动化测试，确认无规格/文档矛盾

## Workflow follow-up

- 先完成 1.1–1.5 PoC，再决定是否继续 2.x；PoC 失败时回到设计 D1 方案 2/3，不直接扩大实现。
- DeepSeek Harness SDK 已确认是 Python 包 `deepseek-harness-sdk`；版本、许可证和安装来源必须在 5.3 中固定，未拿到私有索引/凭据/wheel 前不开始真实后端集成，也不把 SDK 或 venv 提交到仓库。
- 所有 AI 生成的修改必须先经过 `script.validate` 和用户确认，再调用 `script.apply`。
- 评审通过后再归档旧 `production-ui` 需求；本变更不删除旧 Studio 代码。
