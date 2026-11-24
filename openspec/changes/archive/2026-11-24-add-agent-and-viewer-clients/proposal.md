# Proposal

## Why

当前 Epicode 只有一种运行时客户端：以表演者身份加入、占有角色并接收复制的 Game 客户端。它既不适合无界面的智能体控制，也不适合只负责观察和拍摄的查看端。智能体需要结构化的命令、租约和结果反馈，查看端需要相机和完整世界表现，但不能控制角色。

把这两种需求继续塞进同一个表演者客户端，会导致权限边界模糊、角色被观看端误占、Agent 缺少可靠的控制闭环，查看端也拿不到完整世界表现。现在需要把运行时客户端明确拆成“API 控制客户端”和“世界查看客户端”。

## What Changes

- 新增 Agent 参与者模型：Agent 通过 HTTP/WebSocket 加入会话、申请角色租约、续租、释放，并以结构化命令控制角色。
- 新增 Agent 控制闭环：`character.moveTo`、`character.lookAt`、`character.stop`、`character.state` 等命令，同步结果、异步事件和状态版本。
- 新增服务器侧 Agent 控制器：不依赖 UE 客户端，由服务器导航和驱动角色，角色位置仍由服务器权威并复制给查看端。
- 新增 Agent 命令反馈契约：请求标识、幂等键、状态版本、接受/拒绝原因、完成/受阻事件和可查询观察结果。
- 新增世界查看客户端：以只读 Viewer 参与者加入，生成 SpectatorPawn，只控制相机，不占有角色、不修改世界。
- 新增查看端相机模式：自由飞行、跟随角色、固定相机和场景预置相机，支持干净画面与轻量信息叠加。
- 新增查看端世界表现：加载运行时关卡，复制角色、表演、道具、建造物、物品、灯光和环境，保证查看端看到服务器世界而不是空场景。
- 新增只读强制：Viewer 角色在服务器侧被拒绝角色控制、世界交互和会话管理，不依赖客户端隐藏按钮。
- 新增查看端槽位与加入令牌：Viewer 不占表演者角色，不因角色耗尽被拒绝，并需要服务器认可的查看端身份。
- 扩展场景作者化：标记可被 Agent 控制的角色、Agent 出生点、查看端可用相机和默认相机，并校验运行时关卡与导航数据。
- 新增 `Scripts/Viewer.sh`、Agent 冒烟脚本和查看端冒烟脚本；保留 `Client.sh` 作为表演者调试客户端。
- 更新控制 API、场景、运行脚本和验收文档。

## Capabilities

### New Capabilities

- `agent-control`: 无 GUI 的 API 控制客户端，包括参与者身份、角色租约、命令闭环、服务器侧移动、观察结果、错误与审计。
- `world-viewing`: 只读世界查看客户端，包括 Viewer 参与者、SpectatorPawn、相机模式、世界表现复制、只读强制和查看端槽位。

### Modified Capabilities

- `control-api`: 增加 Agent 会话、租约、角色移动与观察方法，增加请求关联、状态版本、事件过滤和只读拒绝语义。
- `scene-authoring`: 增加 Agent 可控角色、Agent 出生点、查看端相机和运行时关卡/导航数据的作者化与校验。
- `engine-foundation`: 增加查看端和 Agent 冒烟运行脚本，明确 Editor、Headless、Server、Viewer 和 Agent 的启动边界。

## Impact

- **新增代码**：`EpicodeNetwork` 增加 Agent 参与者、角色租约、服务器侧控制器、查看端控制器、SpectatorPawn、运行时场景表现和复制；`EpicodeControlAPI` 增加 Agent 命令与反馈；`EpicodeCore` 增加场景运行时元数据。
- **修改代码**：`EpicodeNetworkPlayerController` 拆分表演者与查看者路径；`EpicodeNetworkGameMode` 按参与者类型生成控制器；`EpicodeNetworkSceneLoader` 从角色摘要扩展为运行时场景表现。
- **新增脚本**：`Scripts/Viewer.sh`、`Scripts/AgentSmoke.sh`、`Scripts/ViewerSmoke.sh`，并扩展 `Scripts/env.sh` 的日志和端口约定。
- **配置**：增加 `MaxViewers`、`ViewerJoinToken`、`AgentLeaseSeconds`、`AgentCommandTimeoutSeconds`、`ViewerDefaultCamera`、`bAllowRemoteViewers` 等配置。
- **数据**：场景 JSON 增加可选的运行时元数据；会话快照增加参与者类型、租约、查看端和世界表现摘要；不改变现有场景 JSON 的必填字段。
- **兼容性**：现有 `Client.sh`、表演者 Game 客户端、Studio 本进程会话和既有控制 API 命令保持可用。新增字段和命令向后兼容。
- **非目标**：不实现多租户 Agent 密钥体系、像素流、NDI/Spout 输出、Replication Graph 调优、完整 UGC 编辑器、Windows 客户端、独立 Server 目标构建和打包 Game 客户端崩溃修复。当前查看端使用 `UnrealEditor -game` 回退路径。
- **风险**：查看端世界表现需要把场景实体投影为可复制 Actor，工作量大于网络基线；Agent 移动依赖导航数据；只读角色若只在 UI 层实现会被绕过；查看端和表演者共用 GameMode 时容易误占角色。
