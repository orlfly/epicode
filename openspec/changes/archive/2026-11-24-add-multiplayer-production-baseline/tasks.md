# Tasks

## 1. 网络基础与构建目标

- [x] 1.1 新增 `EpicodeNetwork` 运行时插件骨架、模块声明和最小依赖，确保不依赖 Editor 模块；验证：编辑器目标编译成功，启动日志列出网络模块版本；当前引擎发行版不支持独立 Server 目标时，`TARGET=EpicodeVideoServer ./Scripts/Build.sh` 以退出码 6 写入 `Saved/Logs/build-server-target.log` 并标记 `serverMode=editor-fallback`。
- [x] 1.2 新增 `EpicodeVideoServer` Server 目标，复用现有 `EpicodeVideo` Game 目标并补齐网络模块，扩展 `Scripts/env.sh` 解析服务器/客户端/Game 二进制；验证：`./Scripts/Build.sh` 构建编辑器目标返回 `Result: Succeeded`；Server 目标不可用时 `Server.sh` 回退 `UnrealEditor-Cmd -server`，`Client.sh` 支持 `game`、`editor`、`auto` 并在 Game 崩溃时回退 `UnrealEditor -game`，日志写入 `Saved/Logs/server-fallback.log` 和 `client-fallback.log`。
- [x] 1.3 新增 `Scripts/Server.sh` 与 `Scripts/Client.sh`，支持端口、场景标识、服务器地址、玩家名称和额外 Unreal 参数；验证：`Scripts/NetworkSmoke.sh` 启动专用服务器和两个客户端并输出 `Epicode network smoke passed`；`Saved/Logs/script-error-audit.log` 记录非法端口返回 2、非法地址返回 2、端口占用返回 3。
- [x] 1.4 在 `DefaultEpicode.ini` 增加游戏端口、最大玩家数、重连宽限期、会话目录、默认角色和在线子系统配置，并在 `system.health` 暴露网络状态；验证：新增配置读取测试通过，健康检查包含网络模块和在线子系统字段。`Epicode.Network.Health` 在 `Saved/Logs/test.log` 中通过；`Saved/Logs/network-smoke-server.log` 的启动诊断也包含网络模块状态。
- [x] 1.5 配置专用服务器模块过滤，确保制作界面、导入和渲染模块不进入 Server 目标；验证：Studio/Import/Render 均为 Editor 模块，回退服务器日志只加载 `EpicodeVideo`、`EpicodeCore`、`EpicodeNetwork`、`EpicodeControlAPI`；`server-api-audit.log` 确认服务器 HTTP `system.health` 可用，`server-api-session.log` 与 `server-api-session-response.json` 确认 HTTP `session.snapshot` 返回服务器进程内的活动会话。真正 Server 目标的构建仍由 1.2 跟踪。
- [x] 1.6 更新 `Docs/Operations.md` 和 `Docs/Architecture.md` 中的双平面架构、服务器/客户端脚本和端口说明；验证：`Build.sh`、`Test.sh`、`Smoke.sh`、`Server.sh`、`Client.sh`、`NetworkSmoke.sh` 已逐条执行，路径和默认值与文档一致。

## 2. 会话生命周期与权限

- [x] 2.1 实现会话标识、创建、启动、加入、离开和结束状态机，拒绝非法状态迁移；验证：`Epicode.Network.SessionLifecycle` 覆盖正常流程和非法迁移。
- [x] 2.2 实现玩家标识、加入、离开、会话满员和重连宽限期；验证：`Epicode.Network.Reconnect` 覆盖宽限期内恢复和超时释放，`Epicode.Network.SessionLifecycle` 覆盖满员拒绝。
- [x] 2.3 实现所有者、导演、表演者和观察者角色及权限矩阵；验证：`Epicode.Network.Permissions` 覆盖每种角色的允许和拒绝路径。
- [x] 2.4 实现角色所有权分配、转移、断线保留和超时释放；验证：`Epicode.Network.Ownership` 覆盖重复占用、转移和释放，`Epicode.Network.Reconnect` 覆盖断线保留和超时释放。
- [x] 2.5 实现会话快照、世界版本和单调事件序号，支持按序号补发或要求完整快照；验证：`Epicode.Network.Snapshot` 覆盖加入快照、增量事件和事件缺口。
- [x] 2.6 注册 `session.create/start/join/leave/status/snapshot/export/end/kick/setRole` 命令并复用现有命令注册表；验证：`Epicode.Network.Commands` 覆盖方法成功、越权和未知会话错误，另注册 `session.reconnect/assignCharacter/releaseCharacter/transferCharacter`。
- [x] 2.7 编写 `Docs/Multiplayer.md` 的会话、角色、所有权和重连章节；验证：文档示例中的会话流程与自动化测试结果一致。

## 3. 运行时场景装载与隔离

- [x] 3.1 实现从已保存场景装载会话世界，校验 schema 版本、场景标识和受支持实体；验证：`Epicode.Network.SceneLoad` 使用现有场景夹具装载成功并返回实体摘要。
- [x] 3.2 实现独立会话文档和会话目录 `Saved/Epicode/Sessions/<sessionId>/`，运行时修改只写会话文档；验证：`Epicode.Network.SceneIsolation` 修改运行时实体后原场景 JSON 内容和修改时间不变。
- [x] 3.3 实现场景版本或不可投影实体不兼容时的拒绝路径；验证：`Epicode.Network.SceneLoad` 覆盖未来版本和未知实体，编辑器场景保持不变。
- [x] 3.4 实现会话结束摘要和显式快照导出，不覆盖原场景；验证：`Epicode.Network.SessionExport` 校验导出文件包含玩家、角色、worldVersion 和 ended 状态，原场景字节不变。
- [x] 3.5 更新 `Docs/SceneModel.md` 和 `Docs/Multiplayer.md` 的作者平面/运行时平面边界；验证：文档明确列出会话目录、隔离规则和导出行为。

## 4. 玩家角色与移动同步

- [x] 4.1 实现会话表演者角色的生成、外观应用、出生点放置和 PlayerController 占有；验证：`Scripts/NetworkSmoke.sh` 使用双角色场景夹具，两个客户端分别占用场景角色，日志 `Spawn mapping ... location=X=100.000 Y=200.000 Z=88.000 scene=1 appearance='/Engine/EngineMeshes/SkeletalCube'` 同时覆盖出生点和外观复制。
- [x] 4.2 启用服务器权威移动、客户端预测和服务器校正，配置合理的速度和瞬移校验；验证：`Epicode.Network.Movement` 覆盖速度预算、预测校正阈值和非法传送拒绝，`EpicodeNetworkCharacter` 使用 `UCharacterMovementComponent` 服务器权威与客户端预测。
- [x] 4.3 实现远程角色插值和 Root Motion 禁用策略，确保 Motion Matching 不直接复制根运动；验证：`Epicode.Network.Interpolation` 校验指数平滑和 Root Motion 禁用，网络冒烟采集 48 个远程漂移样本，最近 10 个样本最大漂移 91.92–205.74，客户端日志记录 `Remote interpolation ... smoothing=Exponential ... rootMotion=Ignore`。
- [x] 4.4 配置角色、相机/灯光和道具的复制优先级、相关度和休眠策略，并记录基线；验证：网络冒烟在客户端启动参数中包含 `stat net`，命令行写入摘要（不断言引擎渲染层统计数值），`Epicode.Network.ReplicationPriority` 校验 30Hz/10Hz/priority 2/100m cull，会话快照角色包含 `replicationClass=high` 与 `netUpdateFrequency=30`。
- [x] 4.5 实现断线重连后的角色重新占有和状态恢复；验证：`Epicode.Network.Reconnect` 覆盖运行时变换保留与非法变换拒绝，网络冒烟断开 Bob 后重连，日志 `Spawn mapping ... restored=1` 并恢复原角色、外观和记录位置。
- [x] 4.6 在 `Docs/Multiplayer.md` 增加移动预测、校正、远程插值和带宽基线说明；验证：文档参数与配置文件和冒烟日志一致。

## 5. 表演动作、表情与口型

- [x] 5.1 实现复制的表演状态结构，包含动作标签、动作队列、情绪、表情强度、说话状态和口型驱动源；验证：`FEpicodePerformanceState` 复制到角色，`Epicode.Network.PerformanceState` 覆盖默认值、JSON 往返和复制字段。
- [x] 5.2 实现动作请求、服务器前置条件校验、动作队列、打断和权威广播，允许本地预测，并注册 `performance.action`；验证：`Epicode.Network.Actions` 覆盖播放、排队、打断和非法动作，网络冒烟两名玩家各产生动作事件。
- [x] 5.3 实现情绪和表情关键状态复制，表情强度范围校验，微表情保持本地，并注册 `performance.expression`；验证：`Epicode.Network.Expression` 覆盖 0–1 范围和越界拒绝，网络冒烟快照包含两名玩家的表情事件。
- [x] 5.4 实现无、音频、Live Link 和预设四种口型驱动源及不可用时的降级警告，并注册 `performance.voice`；验证：`Epicode.Network.VoiceDrive` 覆盖全部四种源，网络冒烟在缺少 Live Link 时降级为 `preset` 并记录 `LIVELINK_UNAVAILABLE`。
- [x] 5.5 实现 Motion Matching 可选启用和标准移动回退，确保服务器位置仍为权威；验证：`Epicode.Network.MotionMatching` 覆盖启用、缺少数据和回退路径，并断言 `ResolveMotionMatching` 在缺少数据时返回 `MOTION_DATA_UNAVAILABLE` 警告；`performance.motionMatching` 命令复用同一规则。当前基线不包含 Motion Matching 数据资产，启用仅作为可选标志，未改变服务器位置。
- [x] 5.6 实现手部 IK、脚部防穿地和视线跟随的本地表现修正，不改变服务器权威变换；验证：`UEpicodeLocalIKComponent` 读取 `IKRig` 资产并按手部、脚部、视线权重求解，非本地/服务器/专用服务器跳过；`Epicode.Network.IK` 校验权重钳制、本地应用条件、骨骼变换混合、缺失资产降级和服务器快照不含本地 IK 权重。
- [x] 5.7 发布带序号和时间戳的动作、表情和口型事件，并加入会话快照；验证：`Epicode.Network.PerformanceEvents` 覆盖事件顺序、时间戳和快照字段，网络冒烟快照包含 6 条 `performance.*` 事件。
- [x] 5.8 在 `Docs/Multiplayer.md` 增加动作队列、表情同步、口型驱动和 Motion Matching/IK 限制说明；验证：文档中的能力矩阵与测试覆盖一致。

## 6. 物品、库存、制作与建造

- [x] 6.1 实现物品运行时状态、物品标识、类型、耐久度、交互锚点和复制规则；验证：`Epicode.Network.Items` 覆盖生成、状态变化、复制分类和客户端伪造拒绝。
- [x] 6.2 实现拾取、持握、释放和放下，校验距离、所有权和插槽，并注册 `item.pickup/hold/release/drop`；验证：`Epicode.Network.ItemInteraction` 覆盖成功、距离过远和非法持有。
- [x] 6.3 实现服务器库存、容量校验和拥有者可见性，并提供 `inventory.get`；验证：`Epicode.Network.Inventory` 覆盖加入、容量满和非拥有者不可见。
- [x] 6.4 实现配方定义、制作开始/状态/取消、材料扣除、进度和完成产出，并注册 `craft.start/status/cancel`；验证：`Epicode.Network.Crafting` 覆盖成功、缺少材料、取消和完成。
- [x] 6.5 实现建造预览、确认、碰撞/区域/材料校验和撤销，并注册 `build.confirm/undo`；验证：`Epicode.Network.Building` 覆盖有效建造、非法位置、碰撞、材料不足、撤销和材料返还。建造预览当前由客户端投影，服务器只接受 `build.confirm`。
- [x] 6.6 发布物品、制作和建造世界事件，并加入会话事件流；验证：`Epicode.Network.WorldEvents` 覆盖事件类型、序号和快照 `worldEvents` 查询。
- [x] 6.7 实现世界权限和并发修改串行化，处理玩家离开后的制作归属；验证：`Epicode.Network.WorldPermissions` 覆盖观察者拒绝、顺序建造和离开接管。
- [x] 6.8 在 `Docs/Multiplayer.md` 增加物品、库存、配方、建造和事件说明；验证：文档字段与自动化测试夹具一致。

## 7. Studio 多人界面

- [x] 7.1 在 Studio 增加会话面板，支持创建、加入、控制 API 地址、令牌、场景选择和结束会话；验证：`Epicode.Studio.MultiplayerPanel` 构造面板并校验控件、快照和空状态，`Window > Epicode Multiplayer` 注册独立 Tab；`FEpicodeControlApiClient` 通过 `/api/v1/rpc` 异步连接远程控制面，`Epicode.Studio.ControlApiClient` 与 `Epicode.Studio.MultiplayerRemote` 覆盖连接、401、超时、领域错误、令牌掩码、远程命令路由和本进程回退。
- [x] 7.2 增加玩家列表、角色列表、角色分配、角色修改和踢出操作，并按权限禁用按钮；验证：`Epicode.Studio.MultiplayerRoles` 覆盖所有者、导演、表演者和观察者视图，`Kick/Set Role/Assign` 按钮按 `CanManage/CanAssignCharacters` 启用。
- [x] 7.3 增加表演者 HUD，显示当前角色、动作队列、情绪、表情强度、说话状态和口型驱动源；验证：`Epicode.Studio.PerformerHud` 覆盖动作、表情、口型和观察者空状态。
- [x] 7.4 增加导演控制台，显示玩家、角色、活动相机和灯光入口；验证：`Epicode.Studio.DirectorConsole` 覆盖导演可操作和观察者只读。
- [x] 7.5 增加断线重连、角色权限变化和会话结束状态显示；验证：`Epicode.Studio.SessionStates` 覆盖重连中、观察者和已结束状态。
- [x] 7.6 更新 `Docs/Studio.md` 和 `Docs/Usage.md` 的多人面板操作说明；验证：文档包含控制 API 地址、令牌、连接/断开、本进程/远程切换、1 秒轮询和错误处理，并与 `Epicode.Studio.MultiplayerRemote` 日志一致。

## 8. 集成验收与文档

- [x] 8.1 编写并运行 `Scripts/NetworkSmoke.sh`，启动专用服务器和两个客户端，覆盖加入、角色分配、移动、动作、拾取、制作、建造、重连和事件顺序；验证：脚本输出 `Epicode network smoke passed`，覆盖 48 个漂移样本、6 条表演事件、8 条世界事件和 Bob 重连，非零退出时输出失败步骤。
- [x] 8.2 运行现有 `Scripts/Test.sh` 全量回归，确认现有 `Epicode.*` 测试全部成功；验证：`Saved/Logs/test.log` 中 0 个 `Result={Fail}`，当前 63 个测试全部 `Success`。
- [x] 8.3 记录首版网络基线，包括玩家数、复制优先级、带宽、延迟和丢包下的行为；验证：`Docs/Multiplayer.md` 基线表记录 2 玩家、30Hz、priority 2、48 个漂移样本、最近 10 个最大漂移 91.92–205.74 cm（阈值 300 cm），并明确带宽/延迟/丢包需后续网络模拟采样，`Saved/Logs/network-smoke.log` 可交叉核对。
- [x] 8.4 更新 `Docs/Acceptance.md`、`Docs/Usage.md` 和 `Docs/Architecture.md`，加入 P1–P3 验收证据和 P4–P8 后续范围；验证：文档链接、命令和验收数字与实际日志一致，`Acceptance.md` 记录 63 个测试、双客户端冒烟和基线数字。
- [x] 8.5 执行干净构建和验收：`Scripts/Clean.sh`、编辑器/服务器/客户端构建、`Scripts/Test.sh`、`Scripts/Smoke.sh`、`Scripts/NetworkSmoke.sh`；验证：干净构建 `Saved/Logs/build-clean.log` 成功，首次 `test-clean.log` 63/63 通过，`smoke-clean.log` 和 `network-smoke-clean.log` 均返回 0。最后一次重编译使用 UBT `-Clean` 保留 `Saved` 证据；服务器目标因引擎发行版限制不可构建，`EpicodeVideo` Game 目标可构建但未打包二进制启动崩溃，`Client.sh` 仍使用 `UnrealEditor -game`。

## Workflow follow-up

- 经用户评审后归档变更，并运行 `openspec validate add-multiplayer-production-baseline`。
- 归档后确认主 spec 已包含三个新能力和四个修改能力的增量要求。

## 当前实现偏差

- `EpicodeNetwork` 插件已在编辑器目标编译通过，`EpicodeNetwork.Build.cs` 不依赖 Editor 模块；`network-smoke-server.log` 记录 `EpicodeNetwork module started (version 0.1.0)`。当前 Unreal Engine 发行版不支持独立 `Server` 目标，`TARGET=EpicodeVideoServer ./Scripts/Build.sh` 返回退出码 6 并写入 `Saved/Logs/build-server-target.log`，`Server.sh` 回退到 `UnrealEditor-Cmd -server`，`system.health.network` 报告 `serverMode=editor-fallback` 和 `serverTargetSupported=false`。
- `EpicodeVideo` Game 目标可以构建成功，但直接运行未打包的 Game 二进制仍可能因 `BufferReader` 断言崩溃。`Client.sh` 支持 `--mode auto|game|editor`，Game 退出时回退 `UnrealEditor -game`，并写入 `Saved/Logs/client-fallback.log`；双客户端冒烟的客户端当前为 `editor-game`。
- `EpicodeControlAPI` 已改为 Runtime 模块，编辑器、Headless 和回退服务器进程都会启动 HTTP/WS 控制面；`Saved/Logs/server-api-audit.log` 记录 `Epicode API listening` 和服务器 `system.health` 成功，Game 客户端只加载模块但不绑定端口。
- Studio 多人面板的 `API`/`Token` 输入和 `Connect`/`Disconnect`/`Local` 已接入 `FEpicodeControlApiClient`，远程模式通过 `/api/v1/rpc` 异步执行 `system.health`、`session.snapshot` 和会话写命令，1 秒轮询并只显示令牌掩码；尚未接入 WebSocket 推送。
- `Scripts/NetworkSmoke.sh` 已覆盖双客户端加入、场景角色出生点与外观、移动扫掠、远程漂移、动作/表情/口型、物品/拾取/制作/建造、断线重连和事件顺序；2026-11-24 运行返回 0，摘要写入 `Saved/Logs/network-smoke-summary.json`。
- 带宽、延迟和丢包曲线未在真实网络模拟环境中采样；`Docs/Multiplayer.md` 的基线只记录本地回环的复制参数和漂移指标，并明确这一限制。
- Motion Matching 只实现可选开关和标准移动回退，未接入 Motion Matching 数据资产；手部/脚部 IK 和视线跟随已通过 `UEpicodeLocalIKComponent` 接入 IK Rig 资产，默认关闭，需要项目配置实际 IK Rig 和骨骼名。
- 动作本地预测当前只在客户端记录动作标签（`PlayLocalActionPrediction`），不驱动本地动画；动作播放、排队、打断和广播仍由服务器权威状态驱动。
- 物品和建造状态保存在会话记录中，尚未为每个物品/建筑生成独立 UE Actor；Studio 多人面板通过轮询快照刷新，未接入 WebSocket 推送。
- `system.methods` 当前提供方法名、描述、`mutating` 和 `async`；会话角色与所有权在运行时校验。领域错误码使用 `CHARACTER_ALREADY_OWNED`、`SCENE_VERSION_UNSUPPORTED`、`SESSION_ALREADY_EXISTS`/`SESSION_ALREADY_ENDED` 等实际实现值，`control-api` spec 已按实现校准。
