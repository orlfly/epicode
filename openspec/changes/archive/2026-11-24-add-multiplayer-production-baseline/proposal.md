# Proposal

## Why

当前 Epicode 是单用户、编辑器内的场景编排与渲染工具，场景 JSON 是唯一真相源，HTTP/WebSocket 是本地控制面。新设计文档要求把它扩展为多人协同剧情视频制作平台：权威服务器、多玩家角色表演、物品制作与建造、表情口型同步，以及后续的重演录制与多机位导出。

如果继续把多人状态塞进现有编辑器场景文档，会让运行时复制、权限、重连和回放互相污染。需要在保留现有作者链路的前提下，新增独立的运行时网络平面，并先打通文档建议的 P1–P3 主链路。

## What Changes

- 新增 `EpicodeNetwork` 运行时插件，提供会话生命周期、玩家身份、角色所有权、角色/导演/观察者权限、重连和会话诊断。
- 新增专用服务器目标 `EpicodeVideoServer`，复用并扩展现有 `EpicodeVideo` Game 客户端目标，并增加 `Scripts/Server.sh`、`Scripts/Client.sh` 和双客户端网络冒烟脚本。
- 新增运行时场景装载：服务器从已保存的场景 JSON 启动会话世界，运行时修改只写入会话快照，不回写编辑器场景文件。
- 新增玩家角色：使用 `ACharacter` 和 `CharacterMovementComponent` 的服务器权威移动、客户端预测、服务器校正和远程插值；编辑器中的角色投影保持现状。
- 新增表演状态：动作队列、情绪、表情强度、说话状态和口型驱动源通过服务器校验与关键状态复制；口型与微表情默认本地播放，不逐帧同步。
- 新增物品、库存、制作和建造：物品状态、配方进度、建造预览与确认全部由服务器校验，客户端只发送请求和本地预测。
- 新增多人控制面：`session.*`、`performance.*`、`item.*`、`craft.*`、`build.*` 命令，以及会话快照和会话事件流。
- 新增 Studio 多人面板：主持/加入会话、玩家与角色列表、角色分配、导演模式入口和表演者 HUD 基础控件。
- 保留现有单用户编辑器、场景命令、导入和渲染语义；多人能力作为可独立加载的运行层，不改变现有场景 JSON 的默认格式。

## Capabilities

### New Capabilities

- `multiplayer-session`: 权威服务器/监听服务器拓扑、会话创建加入离开、玩家身份与角色、角色所有权、重连、运行时场景装载、权限校验、网络诊断和会话快照。
- `networked-performance`: 玩家角色生成与占有、移动预测与服务器校正、动作队列、情绪与表情关键状态、口型驱动源、Motion Matching/IK 的降级策略。
- `interactive-world`: 服务器权威的物品、库存、制作和建造，包括交互距离校验、配方进度、建造预览、撤销和世界事件。

### Modified Capabilities

- `engine-foundation`: 增加服务器/客户端构建目标、`EpicodeNetwork` 插件、网络配置、平台矩阵和网络启动诊断。
- `scene-authoring`: 增加“会话场景装载与运行时隔离”要求，明确编辑器场景 JSON 与会话快照的边界。
- `control-api`: 增加会话、表演、物品、制作和建造命令的覆盖范围、角色错误和会话事件。
- `production-ui`: 增加多人会话面板、导演控制台和表演者 HUD 的最低交互要求。

## Impact

- **新增代码**：`engine/Plugins/EpicodeNetwork/`、服务器与客户端目标、网络冒烟脚本、网络相关自动化测试；`EpicodeStudio` 增加多人面板；`EpicodeCore` 增加运行时实体/会话扩展点。
- **新增配置**：默认游戏端口、最大玩家数、会话目录、重连宽限期、角色权限矩阵和在线子系统选择。默认仍只监听本机控制 API，游戏端口仅用于会话。
- **平台**：Linux x86_64 作为专用服务器和首版客户端基线；Windows 客户端作为可选目标，用于 MetaHuman Animator、Audio2Face 和 Live Link Face；DX12 不再是服务器前置条件。
- **数据**：新增 `Saved/Epicode/Sessions/<sessionId>/` 保存会话快照和事件日志；现有 `Saved/Epicode/Scenes/`、`Renders/` 和导入索引保持不变。
- **兼容性**：不修改现有命令名称和场景 JSON 默认字段；已有 Studio、HTTP API、导入和渲染工作流可继续单独使用。
- **本变更范围**：对应设计文档 P1–P3，即网络基线、物品/制作/建造、动作/表情/口型。P4 场景控制、P5 资产注册表、P6 重演/Take Recorder、P7 多机位导出、P8 Replication Graph 与完整权限优化留给后续变更。
- **风险**：Motion Matching 与网络校正、MetaHuman/Live Link 的平台限制、多人录制冲突和带宽压力；设计文档已给出对策，本变更先建立可测试的网络与表演基线。
