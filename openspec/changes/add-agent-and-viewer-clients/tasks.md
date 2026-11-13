# Tasks

## 1. 场景运行时元数据与配置

- [x] 1.1 在 `FEpicodeSceneDocument` 增加可选 `runtime` 对象，包含 Agent 可控角色、Agent 出生点、动作白名单、Viewer 相机列表和默认相机，并把场景 schema 升到 2；验证：`Epicode.Scene.RuntimeMetadata` 覆盖序列化往返、默认值、未知子字段保留和 schema 版本。
- [x] 1.2 实现 schema 1 到 2 的迁移，旧场景加载时写入空 `runtime`，保存后仍可被旧字段读取；验证：`Epicode.Scene.RuntimeMigration` 使用现有场景夹具完成加载、保存、重新加载，实体和活动相机不变。
- [x] 1.3 在 `EpicodeNetworkSettings` 增加 `MaxViewers`、`ViewerTokenFile`、`AgentLeaseSeconds`、`AgentHeartbeatSeconds`、`AgentCommandTimeoutSeconds`、`bAllowRemoteAgents` 和 `bAllowRemoteViewers`，并在 `DefaultEpicode.ini` 写入默认值；验证：`Epicode.Network.Settings` 读取全部配置，查看者令牌文件首次生成时权限为 0600。
- [x] 1.4 实现场景运行时元数据校验，拒绝不存在的可控角色、重复出生点标识、空动作名、无效默认查看相机和缺失运行时关卡；验证：`Epicode.Scene.RuntimeValidation` 覆盖每个错误码和原子失败，原场景文件不变。
- [x] 1.5 注册 `scene.runtime.get` 和 `scene.runtime.set` 命令，复用场景命令注册表并支持撤销；验证：`Epicode.Scene.RuntimeCommands` 覆盖读写、校验失败、撤销和 `system.methods` 目录。
- [x] 1.6 更新 `Docs/SceneModel.md` 的 schema v2、`runtime` 字段、迁移和校验规则；验证：文档示例可被场景夹具加载，字段名与命令输出一致。

## 2. 参与者类型与会话策略

- [x] 2.1 在会话玩家记录增加 `ParticipantKind`，区分 `Performer`、`Agent` 和 `Viewer`，并让 `JoinSession` 接受参与者类型；验证：`Epicode.Network.Participants` 覆盖三种类型、重复标识和快照字段。
- [x] 2.2 实现 Viewer 槽位和 `MaxViewers`，Viewer 不占 `MaxPlayers` 表演者名额，角色耗尽时仍可加入；验证：`Epicode.Network.ViewerSlots` 覆盖槽位满、角色满、Viewer 加入/离开/重连。
- [x] 2.3 扩展会话权限策略，Viewer 只允许查询和接收事件，拒绝角色控制、世界交互、私有库存和会话管理；验证：`Epicode.Network.ViewerPermissions` 覆盖每种拒绝路径和 `VIEWER_READ_ONLY`。
- [x] 2.4 在会话快照增加参与者类型、查看者槽位、Agent 租约摘要和 Viewer 相机摘要；验证：`Epicode.Network.SnapshotParticipants` 校验快照字段和事件序号。
- [x] 2.5 扩展 `session.join` 支持 `participantKind`，Agent 返回参与者令牌，Viewer 返回查看者标识，表演者行为保持不变；验证：`Epicode.Network.JoinKinds` 覆盖三种加入方式和既有表演者回归。
- [x] 2.6 更新 `Docs/Multiplayer.md` 的参与者类型、Viewer 槽位和只读规则；验证：文档字段与快照输出一致，现有表演者流程说明不变。

## 3. Agent 控制 API

- [x] 3.1 在控制 API 增加参与者令牌绑定，从 `X-Epicode-Participant` 解析会话和参与者，拒绝缺失、伪造和过期绑定；验证：`Epicode.API.ParticipantBinding` 覆盖成功、`PARTICIPANT_NOT_BOUND`、`FORBIDDEN` 和 Viewer 调用。
- [x] 3.2 为 Agent 命令增加 `requestId`、`idempotencyKey` 和 `expectedStateVersion` 信封，响应返回 `requestId`、`ok`、`result` 和 `stateVersion`；验证：`Epicode.API.CommandEnvelope` 覆盖缺失请求标识、幂等重试和 `STALE_STATE`。
- [x] 3.3 注册 `session.heartbeat`、`character.moveTo`、`character.lookAt`、`character.stop` 和 `character.state`，并在 `system.methods` 标记参与者类型、修改性、异步性和租约要求；验证：`Epicode.API.AgentMethods` 覆盖方法目录、参数校验和成功响应。
- [x] 3.4 实现 WebSocket 按会话、参与者、角色和事件类别过滤，支持 `since` 补发和 `events.gap`；验证：`Epicode.API.EventFilters` 覆盖过滤、补发、缺口和慢客户端。
- [x] 3.5 增加 Agent 领域错误和审计：`CHARACTER_NOT_ASSIGNED`、`NOT_CONTROLLER`、`LEASE_EXPIRED`、`NAVIGATION_UNAVAILABLE`、`RATE_LIMITED`；验证：`Epicode.API.AgentErrors` 覆盖每种错误、审计字段和脱敏。
- [x] 3.6 更新 `Docs/ControlAPI.md` 或 `Docs/Multiplayer.md` 的 Agent 方法、信封、事件过滤和错误码；验证：文档命令可用 `curl` 复现，字段与 `system.methods` 一致。

## 4. Agent 租约与服务器控制器

- [x] 4.1 实现角色租约状态机，包含获取、心跳、过期、释放、踢出和会话结束；验证：`Epicode.Network.AgentLease` 覆盖正常续期、冲突、超时和重复释放。
- [x] 4.2 实现服务器侧 Agent 控制器，占有一个 `EpicodeNetworkCharacter`，支持导航移动、朝向和停止，不依赖 UE 客户端；验证：`Epicode.Network.AgentController` 在服务器测试世界中生成角色、移动并更新权威位置。
- [x] 4.3 实现 `character.moveTo` 的导航成功、不可达和无导航数据路径，发布 `character.move.completed`、`character.move.blocked` 和 `character.move.stopped`；验证：`Epicode.Network.AgentMovement` 覆盖三种路径和服务器权威位置。
- [x] 4.4 实现 `character.state` 观察结果，返回位置、朝向、速度、动作、表情、持有物、租约和状态版本，并保护其他 Agent 的私有字段；验证：`Epicode.Network.AgentObservation` 覆盖自己、他人和世界查询。
- [x] 4.5 实现租约过期后的移动停止、角色释放和事件发布，处理 Agent 重连续租；验证：`Epicode.Network.AgentLeaseExpiry` 覆盖过期、重连和角色重新申请。
- [x] 4.6 实现 Agent 命令速率限制、幂等缓存上限和审计日志轮转；验证：`Epicode.Network.AgentLimits` 覆盖超限、缓存命中和日志脱敏。
- [x] 4.7 新增 `Docs/AgentClient.md`，说明加入、租约、移动、观察、反馈、错误和最小 Agent 循环；验证：文档中的 Python/curl 示例与冒烟脚本一致。

## 5. Viewer 登录与只读路径

- [x] 5.1 新增 Viewer PlayerController 和 SpectatorPawn 类，Viewer 登录后不生成表演者角色、不调用表演者注册；验证：`Epicode.Network.ViewerSpawn` 断言 Viewer 没有角色且表演者槽位不变。
- [x] 5.2 在服务器登录阶段校验 `ViewerTokenFile` 或配置令牌，选择 Viewer 控制器；无效令牌返回 `VIEWER_NOT_AUTHORIZED`；验证：`Epicode.Network.ViewerAuth` 覆盖有效、缺失、错误和远程禁用。
- [x] 5.3 实现 Viewer 加入、离开、重连和槽位释放，Viewer 断开不影响角色所有权和会话；验证：`Epicode.Network.ViewerLifecycle` 覆盖加入、离开、重连和会话结束。
- [x] 5.4 在会话策略、角色 RPC 和 API 处理层强制 Viewer 只读，拒绝伪造的移动、表演、物品、制作、建造和会话管理请求；验证：`Epicode.Network.ViewerReadOnly` 直接构造伪造 RPC/HTTP 请求并断言状态不变。
- [x] 5.5 实现 Viewer 与表演者、Agent 同时在线时的会话快照和参与者列表，Viewer 不计入表演者数量；验证：`Epicode.Network.MixedParticipants` 覆盖三种类型同时在线。
- [x] 5.6 更新 `Docs/Multiplayer.md` 的 Viewer 加入、令牌、只读和槽位说明；验证：文档命令与 `Viewer.sh` 参数一致。

## 6. Viewer 相机与画面

- [x] 6.1 实现 Spectator 相机组件和输入，支持自由飞行、跟随角色、固定位置和场景预置相机；验证：`Epicode.Viewer.CameraModes` 覆盖四种模式、模式切换和相机不影响角色。
- [x] 6.2 实现 Viewer 初始化消息，发送查看者标识、会话状态、默认相机变换、预置相机列表和角色摘要；验证：`Epicode.Viewer.Setup` 覆盖默认相机、空列表和多人加入。
- [x] 6.3 从场景 `runtime.viewer` 读取相机列表、默认相机和出生变换，缺失时回退到活动相机；验证：`Epicode.Viewer.SceneCameras` 覆盖显式配置、回退和无效引用。
- [x] 6.4 实现跟随目标选择、目标离开后的相机保持和重新选择；验证：`Epicode.Viewer.FollowTarget` 覆盖目标销毁、角色离开和切换。
- [x] 6.5 实现可关闭的信息叠加和干净画面，显示会话状态、参与者数量、相机模式和跟随目标；验证：`Epicode.Viewer.Overlay` 覆盖显示、隐藏和会话结束。
- [x] 6.6 更新 `Docs/ViewerClient.md` 的相机模式、操作键、预置相机和干净画面说明；验证：文档按键与输入映射一致，截图或日志可复现。

## 7. 运行时世界表现

- [x] 7.1 扩展 `FEpicodeNetworkSceneSummary`，从角色摘要扩展为角色、道具、灯光、相机、环境和运行时元数据摘要；验证：`Epicode.Network.SceneSummary` 使用包含全部实体类型的场景夹具。
- [x] 7.2 实现运行时关卡引导：会话加载后解析 `LevelPath`，需要时执行带恢复标记的 `ServerTravel`，重新进入 GameMode 时接管已有会话；验证：`Epicode.Network.LevelTravel` 覆盖同关卡、切换关卡、恢复标记和会话不重复创建。
- [x] 7.3 新增可复制的运行时道具、灯光和相机表现 Actor，由服务器世界表现器按场景摘要生成；验证：`Epicode.Network.SceneActors` 断言实体生成、复制标记和查看端可见。
- [x] 7.4 实现环境状态复制，至少同步时间、太阳、雾、云、曝光和预设；验证：`Epicode.Network.EnvironmentReplication` 覆盖默认值、修改和查看端接收。
- [x] 7.5 为运行时物品和建造物生成可复制表现 Actor，并与 `EpicodeWorldState` 的生成/拾取/建造/撤销同步；验证：`Epicode.Network.WorldActors` 覆盖生成、持有、建造、撤销和查看端可见。
- [x] 7.6 实现 Viewer 加入时的世界快照和增量事件缺口恢复，确保晚加入查看端看到当前角色、道具、建造物、灯光和环境；验证：`Epicode.Network.ViewerLateJoin` 覆盖初次复制、事件缺口和重新快照。
- [x] 7.7 记录查看端世界复制基线，包括实体数量、角色更新频率、道具/建造物变化频率和本地回环带宽；验证：`Docs/ViewerClient.md` 基线表与 `Saved/Logs/viewer-smoke.log` 一致。
- [x] 7.8 更新 `Docs/Architecture.md` 和 `Docs/SceneModel.md` 的运行时关卡、世界表现器和动态实体复制边界；验证：文档图与代码中的生成路径一致。

## 8. 脚本与进程边界

- [x] 8.1 新增 `Scripts/Viewer.sh`，支持 `--server`、`--name`、`--token`、`--camera`、`--res` 和额外 Unreal 参数，默认使用 `UnrealEditor -game` 回退；验证：非法地址和缺失令牌返回非零，合法启动日志包含 Viewer 角色和服务器地址。
- [x] 8.2 新增 Agent 客户端辅助脚本或 Python 模块，仅使用 HTTP/WebSocket 完成加入、租约、移动、观察和释放，不启动 Unreal；验证：脚本在无引擎进程时完成闭环并输出请求/事件摘要。
- [x] 8.3 新增 `Scripts/AgentSmoke.sh`，启动服务器、运行 Agent 客户端、断言 `character.state` 和事件、清理会话；验证：脚本输出 `Epicode agent smoke passed`，失败时输出具体步骤。
- [x] 8.4 新增 `Scripts/ViewerSmoke.sh`，启动服务器和 Viewer，验证相机切换、角色可见、世界实体可见、只读拒绝和会话结束；验证：脚本输出 `Epicode viewer smoke passed`，日志写入 `Saved/Logs/viewer-smoke.log`。
- [x] 8.5 在启动日志中标记 `Editor`、`Headless`、`Server`、`Viewer`、`Agent` 角色，并确认 Viewer 日志包含控制 API 禁用、Agent 日志不包含 Unreal 启动；验证：`Epicode.Foundation.ProcessRoles` 或日志断言覆盖五种角色。
- [x] 8.6 更新 `Docs/Operations.md` 和 `Docs/Usage.md` 的 Viewer/Agent 启动、令牌、端口和日志路径；验证：文档命令逐条执行，退出码与脚本一致。

## 9. 集成验收与回归

- [x] 9.1 运行 `Scripts/AgentSmoke.sh`，覆盖 Agent 加入、租约、移动完成、移动受阻、观察、事件、释放和重连；验证：脚本返回 0，`Saved/Logs/agent-smoke.log` 包含对应事件和状态版本。
- [x] 9.2 运行 `Scripts/ViewerSmoke.sh`，覆盖 Viewer 加入、相机模式、角色和世界实体可见、只读拒绝、多 Viewer 和会话结束；验证：脚本返回 0，`Saved/Logs/viewer-smoke.log` 包含断言摘要。
- [x] 9.3 运行现有 `Scripts/NetworkSmoke.sh` 和 `Scripts/Test.sh`，确认表演者客户端、会话、表演、物品、制作、建造和重连没有回归；验证：`Saved/Logs/network-smoke-clean.log` 和 `test.log` 返回 0，测试数不少于现有 63 项。
- [x] 9.4 执行 `Scripts/Clean.sh` 后的干净构建、全量测试、Agent 冒烟、Viewer 冒烟和表演者网络冒烟；验证：构建成功，首次测试全部通过，三个冒烟脚本均返回 0，失败时保留日志。
- [x] 9.5 更新 `Docs/Acceptance.md` 和 `Docs/Architecture.md` 的 Agent/Viewer 验收证据、已知限制和进程边界；验证：文档路径、测试数量、冒烟结果与实际日志一致。
- [x] 9.6 运行 `openspec validate add-agent-and-viewer-clients --strict`，检查 proposal、五个 spec delta、design 和 tasks 的场景格式与一致性；验证：命令返回通过，无缺失场景或格式错误。

## Workflow follow-up

- 经用户评审后归档变更，按依赖顺序先归档 `add-multiplayer-production-baseline`，再归档本变更。
- 归档后确认主 spec 已包含 `agent-control`、`world-viewing` 以及 `control-api`、`scene-authoring`、`engine-foundation` 的增量要求。
