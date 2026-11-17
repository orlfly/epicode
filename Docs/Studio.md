# Epicode Studio

## 打开

Studio 只属于旧版完整 Editor。使用 `./Scripts/Editor.sh --legacy` 启动后，通过编辑器菜单 `Window > Epicode Studio`，或 Level Editor 工具栏的 `Epicode Studio` 按钮打开。面板可停靠到任意位置。

默认的 `./Scripts/Editor.sh` 启动聚焦工作台，不注册也不显示九页签 Studio、Multiplayer 或 Director 页签。聚焦工作台提供 Epicode 菜单、实时视口、对象列表/本地库、对话/脚本卡片和状态栏；布局与最近场景保存在 `Saved/Epicode/Editor/layout.json`。详见 [聚焦工作台](EditorShell.md)。

`--legacy` 与聚焦模式共享同一套命令注册表、场景文档和事务 sink，不是另一套数据格式。`Epicode.EditorShell.LegacyTabs` 在两种启动模式下分别验证页签存在/不存在。

## 结构

顶部为九个页签，底部为日志窗格：

| 页签 | 功能 |
| --- | --- |
| Scenes | 新建、保存、另存、关闭、打开、删除场景 |
| Outliner | 场景实体列表、名称/类型/ID 过滤、层级缩进、多选/批量删除、展开折叠、拖放重挂接、复制、显示/隐藏；检查器可重命名、重挂接、编辑变换及按类型编辑动画/材质/物理/灯光/相机 |
| Characters | 资产选择器绑定骨骼网格、动画、按名称/网格过滤、播放/暂停/删除 |
| Props | 资产选择器绑定静态网格/材质、设置材质槽、按名称/网格过滤、删除 |
| Lights | 选择类型、强度创建灯光，按名称/类型过滤、删除；颜色等字段通过 API 设置 |
| Environment | 四个预设、时间/雾/云/曝光/泛光数值、应用 |
| Import | 浏览多选或拖放文件、路径/类型、自动放置/覆盖选项、提交导入、查看任务/失败详情、取消、在内容浏览器定位产物 |
| Render | 提交三帧 PNG、查看作业状态/进度、取消排队或运行中作业 |
| API | 运行 `system.health`、显示渲染后端、命令数、场景和 HTTP/WS 地址 |

## 行为

所有按钮最终调用 `FEpicodeCommandRegistry::Execute`，`Source="studio"`。因此 Studio 与 HTTP/WebSocket 使用同一套校验、事件和错误码。面板不直接改 Actor 或 JSON。

列表在 `Tick` 中比较场景文档 revision；只有 revision 变化时才重建，避免每帧刷新导致选择丢失。

未打开场景时，Characters/Props/Lights 列表显示提示；场景、导入和渲染列表为空时也会显示空状态说明，而不是空白区域。

Outliner 会按 `parentId` 计算缩进展示层级，支持复选框多选和 `Delete selected` 批量删除、带子实体的 `+/-` 展开折叠、拖动行到另一行重挂接，以及 `Duplicate` 复制。检查器中的变换字段使用 `x, y, z` 三个逗号分隔数值；角色显示动画、道具显示材质/物理、灯光显示类型/强度、相机显示视场角。输入框获得焦点时外部刷新不会覆盖未提交内容，非法输入只写日志、不调用命令。

Studio 写操作与导入接入编辑器事务，可直接使用编辑器撤销/重做；撤销后场景文档和 Actor 投影会一起恢复。

`FEpicodeStudioViewModel` 保存当前页签、已刷新 revision、单选实体和多选集合；面板按 revision 变化刷新，Outliner 选择通过视图模型同步。

## 基本流程

1. `Scenes > New` 输入名称，创建场景。
2. `Characters/Props/Lights` 生成实体。
3. `Environment` 选择预设或输入数值后 `Apply`。
4. `Scenes > Save` 写入场景库。
5. `Import` 点击 `Browse...` 多选资产或输入绝对路径，选择 `auto/character/prop`，设置自动放置/覆盖后提交，轮询任务。

## 多人面板

`Window > Epicode Multiplayer` 打开独立多人面板，也可从同一 Window 菜单调用。面板分为连接、状态、玩家/角色、表演者 HUD 和导演控制台五块。

- 连接：服务器地址输入框为后续跨进程连接预留，当前不发起远程连接；`Create` 调用本进程的 `session.create`，`Join` 调用 `session.join`，`End` 调用 `session.end`，`Target` + `Kick` 调用 `session.kick`。
- 状态：显示 `No active session`、`Live`、`Reconnecting` 或 `Session ended`，并在断线时显示宽限期重连提示。
- 玩家/角色：列出 `playerId [role]`、连接状态和 `characterId`，以及角色到玩家的映射。
- 表演者 HUD：显示当前动作、动作队列长度、情绪、表情强度、说话状态和口型源；观察者显示空状态。
- 导演控制台：列出玩家、相机和灯光入口；所有者可管理会话，观察者只读。

面板只通过命令注册表读写会话，不直接修改 Actor 或场景 JSON。`FEpicodeMultiplayerViewModel` 负责解析快照并按角色计算按钮可用性，`Epicode.Studio.MultiplayerPanel`、`MultiplayerRoles`、`PerformerHud`、`DirectorConsole` 和 `SessionStates` 覆盖面板构造、权限、HUD 和状态显示。

## Sandbox / Director Workspace

`SEpicodeDirectorWorkspace` 把制作包、沙盒、导演和重演信息集中到五个页签：

| 页签 | 内容 |
| --- | --- |
| Overview | 制作包标识、沙盒运行状态、实体/事件/生成/cue 预算和校验警告 |
| Timeline | 事件序号、类型、参与者和节拍；顶部过滤框按类型、参与者、摘要或节拍过滤 |
| Beats | 当前节拍、当前目标和叙事提示 |
| Cues | 已触发的 cue 和导演消息 |
| Controls | 当前角色、Take 版本、live/ghost/resim 模式、分支标识和 100 ms 响应预算 |

工作区由 `FEpicodeStudioDirectorModel` 驱动，模型不依赖 Slate，可单独测试。`SetSandboxSnapshot` 接受 `sandbox.state` 结果，`SetDirectorSnapshot` 接受 `director.status` 结果；`SetRole` 使用会话角色，owner/director 可进入 ghost/resim 和导演控制，performer/observer 只读。时间轴缩放限制在 0.25–8.0，Take 版本不得超过 `latestTakeVersion`。

失败时页签顶部显示 `LastError`，例如 `SANDBOX_SNAPSHOT_INVALID`、`TAKE_NOT_FOUND` 或 `VIEWER_READ_ONLY`，不阻塞 live 会话。面板不直接修改世界，写操作仍由 `production.*`、`sandbox.*`、`director.*`、`replay.*` 和 `take.*` 命令完成。`Epicode.Studio.SandboxPanel`、`DirectorWorkspace`、`BeatGraph`、`DirectorControls`、`ReplayControls`、`DirectorResponsiveness` 和 `Docs` 覆盖模型、页面数据、权限、版本和响应预算。

## 首版限制

- Outliner 检查器覆盖名称、父级、可见性和变换，以及角色动画、道具材质/物理、灯光类型/强度、相机视场角；环境参数仍在 Environment 页签，碰撞配置和完整后期参数仍通过 API。
- 支持文件对话框多选、拖放到导入列表和导入结果定位；尚不支持拖放资产到视口自动绑定。
- Render 页签只提供固定三帧 PNG 提交和取消；完整帧范围/格式/相机选择仍通过 `render.*` API 或命令行。
- 列表为简单轮询，不订阅 WebSocket 事件。
- 多人面板每 1 秒轮询本进程 `session.snapshot`，尚未接入 WebSocket 推送或专用服务器会话桥接；角色分配和角色修改通过命令面板或 API 完成，面板当前只展示和踢人。
- 场景打开依赖编辑器关卡加载，切换时界面可能短暂阻塞。
