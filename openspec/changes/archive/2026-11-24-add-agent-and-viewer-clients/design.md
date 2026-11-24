# Design

## Context

现有运行时基线已经具备服务器权威会话、`EpicodeNetworkSubsystem`、`EpicodeNetworkCharacter`、表演状态复制、物品/制作/建造状态和控制 API。限制也很明确：

- `AEpicodeNetworkGameMode` 只有一种玩家路径，所有 Game 客户端都以表演者身份注册并申请角色。
- `AEpicodeNetworkPlayerController` 固定调用 `ServerRegisterPlayer`，没有查看者路径。
- 控制 API 的命令以请求参数中的 `requesterId` 为准，没有参与者绑定和角色租约。
- `EpicodeNetworkSceneLoader` 只提取角色摘要，场景道具、灯光、环境没有进入运行时表现。
- 服务器仍以 `/Engine/Maps/Entry` 启动，而场景文档已有 `LevelPath`，查看端无法据此加载同一关卡。
- 控制 API 的 WebSocket 当前向所有连接广播事件，没有按参与者或角色过滤。
- Game 客户端仍使用 `UnrealEditor -game`，独立 Game 包启动崩溃是已知问题。

本设计只解决两类客户端的边界和闭环，不重做已有会话、表演和世界领域模型。需求见 `specs/agent-control/spec.md`、`specs/world-viewing/spec.md` 和三个修改能力的 delta。

## Goals / Non-Goals

**Goals:**

- 让 Agent 完全不启动 Unreal，通过控制 API 完成加入、租约、移动、观察、反馈和释放。
- 让 Viewer 以只读参与者加入，使用自己的 Spectator 相机渲染服务器世界，不占角色、不修改世界。
- 在不改变现有表演者客户端语义的前提下，增加参与者类型、角色租约和查看者槽位。
- 让场景中的角色、道具、建造物、物品、灯光和环境进入查看端表现。
- 让只读限制在服务器权限层生效，而不是只藏在 UI 里。
- 用可复现的 Agent 冒烟和 Viewer 冒烟覆盖两类客户端。

**Non-Goals:**

- 不实现多租户 Agent 密钥、配额计费和远程公网暴露。
- 不实现像素流、NDI/Spout、视频编码和推流。
- 不实现共享导演相机、多机位切换台和录制控制。
- 不实现 Replication Graph、分层相关度和大规模查看者优化。
- 不修复独立 `EpicodeVideoServer` 目标构建限制和打包 Game 客户端崩溃。
- 不把 Viewer 做成完整玩家客户端，不增加角色输入、背包、制作或建造 UI。
- 不改变现有表演者客户端的移动、表演和世界交互语义。

## Decisions

### D1. 用参与者类型区分 Agent、Viewer 和表演者，不新增重复角色

会话玩家记录增加参与者类型：

- `Performer`：现有 Game 客户端，占角色，可控制角色和世界。
- `Agent`：无 UE 客户端，占角色，通过控制 API 控制角色。
- `Viewer`：UE 查看端，不占角色，只读。

权限仍复用现有 `Owner`、`Director`、`Performer`、`Observer` 角色。Agent 使用 `Performer` 角色加 `Agent` 类型；Viewer 使用 `Observer` 角色加 `Viewer` 类型。这样可以保持现有角色字符串、权限矩阵和测试兼容。

替代方案是新增 `Viewer` 和 `Agent` 角色。它会让 `Observer` 与 `Viewer` 的权限几乎重复，还要迁移已有角色字符串和测试，收益不大。

### D2. Agent 是控制面参与者，不是无渲染的 UE 客户端

Agent 不启动 Unreal，不加载地图，不创建 PlayerController。它通过控制 API 加入会话，服务器在会话中创建参与者记录。Agent 的身份由 API Bearer Token 加参与者令牌绑定。

`session.join` 在参与者类型为 Agent 时返回：

- `participantId`
- `sessionId`
- `participantToken`
- 当前会话摘要

后续 Agent 命令携带 `X-Epicode-Participant` 或等价的参与者令牌。服务器从令牌解析参与者和会话，而不是相信请求体里的 `playerId`。这样即使请求伪造了其他 Agent 的标识，也会被拒绝。

首版仍使用本地可信 API Token，参与者令牌用于绑定和防误用，不作为多租户安全边界。远程多 Agent 的独立密钥留到后续变更。

替代方案是让 Agent 启动 `UnrealEditor -game -nullrhi` 的 headless 客户端。这样会引入引擎启动成本、客户端预测和额外控制面，和“Agent 不需要渲染”的设计冲突。

### D3. Agent 角色使用服务器侧控制器和独占租约

Agent 申请角色时，服务器创建租约并生成服务器侧控制器占有角色。控制器负责：

- 导航到目标位置。
- 朝向目标。
- 停止移动。
- 将移动完成、受阻和失败回传给 Agent。
- 在租约过期时停止移动并释放角色。

租约包含 `leaseId`、`playerId`、`characterId`、`acquiredAt`、`expiresAt` 和 `stateVersion`。心跳续期，释放、超时、踢出和会话结束都会终止租约。角色仍然使用现有 `EpicodeNetworkCharacter` 和 `CharacterMovementComponent`，所以查看端看到的是正常的服务器复制。

移动使用高层命令，而不是让 Agent 以 60Hz 发送原始输入。`character.moveTo` 由服务器做导航和避障；没有导航数据时返回 `NAVIGATION_UNAVAILABLE`。连续输入通道作为后续可选能力，不进入本变更。

替代方案是让 Agent 直接发送位置或速度。这样会绕过碰撞、导航和服务器校正，也会让慢速 LLM Agent 产生不自然的高频修正。

### D4. Agent 命令使用关联标识、状态版本和观察闭环

Agent 控制命令使用统一信封：

```json
{
  "requestId": "req-001",
  "idempotencyKey": "move-001",
  "expectedStateVersion": 41,
  "command": "character.moveTo",
  "participantToken": "...",
  "params": {
    "characterId": "char-3",
    "target": { "x": 120, "y": 0, "z": 96 }
  }
}
```

响应返回：

```json
{
  "ok": true,
  "requestId": "req-001",
  "stateVersion": 42,
  "result": {
    "accepted": true,
    "characterId": "char-3",
    "estimatedSeconds": 1.8
  }
}
```

同步响应只表示命令是否被接受。移动完成、受阻、动作完成等结果通过事件通道推送。Agent 还可以调用 `character.state`、`world.status` 和 `session.snapshot` 主动观察。`character.state` 至少返回位置、朝向、速度、当前动作、表情、持有物、租约和状态版本。

命令幂等缓存按参与者和幂等键保存，相同参数返回首次结果，不同参数返回冲突。过期 `expectedStateVersion` 返回 `STALE_STATE`。

替代方案是只返回 `ok`，让 Agent 轮询快照。这样慢、浪费带宽，也无法区分“命令已接受”和“动作已完成”。

### D5. Viewer 是独立的只读 Game 客户端路径

`Server.sh` 和 `Viewer.sh` 继续使用现有 Game 目标，但 Viewer 走独立路径：

- `Viewer.sh` 传入 `-EpicodeRole=viewer`、`-EpicodeViewerToken`、服务器地址和相机模式。
- 服务器在登录阶段校验查看者令牌，选择查看者 PlayerController 和 SpectatorPawn。
- Viewer 不调用 `ServerRegisterPlayer`，不申请角色，不生成 `EpicodeNetworkCharacter`。
- Viewer 以 `Observer` 角色和 `Viewer` 参与者类型加入会话。
- 查看者槽位由 `MaxViewers` 单独控制，不占用 `MaxPlayers` 的表演者名额。

查看者令牌从 `Saved/Epicode/viewer-token` 读取或由配置指定，权限为 0600。令牌无效时服务器拒绝加入，不把客户端声明的角色当成授权。

替代方案是让 Viewer 复用表演者 PlayerController，再加入后销毁角色。这样容易在登录窗口期占角色、触发角色分配和复制，也会让只读路径难以审计。

### D6. Viewer 相机是本地表现，服务器只提供相机元数据

Viewer 使用 SpectatorPawn 加相机组件，支持四种模式：

- 自由飞行：本地移动和旋转。
- 跟随角色：本地插值到目标角色位置。
- 固定位置：保持预置变换。
- 场景预置相机：从场景相机列表选择。

相机变换不写回服务器，也不改变角色。服务器通过查看者初始化消息提供：

- `viewerId`
- 会话状态
- 默认相机变换
- 预置相机列表
- 当前角色摘要

相机列表来自场景的 Viewer 相机元数据。Viewer 切换预置相机只改变本地相机，不修改场景的 `ActiveCameraId`。

替代方案是让服务器权威复制每个 Viewer 的相机。它会增加无意义的复制流量，也让本地相机输入产生网络延迟。共享导演相机需要单独的相机通道，留到后续。

### D7. Viewer 世界表现采用“同一关卡 + 复制动态实体”

Viewer 要看到世界，必须同时解决静态场景和动态实体。

静态部分：

- 场景文档的 `LevelPath` 作为运行时关卡。
- 服务器启动会话后，如果当前地图不是目标关卡，执行一次带恢复标记的 `ServerTravel` 到目标关卡。
- 引擎子系统在关卡切换后保留会话，重新进入 GameMode 时使用恢复标记接管已有会话，而不是重复创建。
- Viewer 连接到服务器后加载同一关卡，获得静态几何、光照和地形。

动态部分：

- 扩展场景装载摘要，包含角色、道具、灯光、相机、环境和运行时元数据。
- 服务器在关卡就绪后生成世界表现Actor，至少覆盖道具、灯光、相机和运行时物品/建造物。
- 所有动态Actor开启复制，Viewer 加入时通过 UE 初始复制获得当前世界，之后接收增量。
- 角色继续使用现有 `EpicodeNetworkCharacter` 复制。
- 表演状态继续使用现有 `PerformerState` 复制。
- 环境参数通过复制的世界状态结构同步，或在关卡已烘焙时以关卡为准。
- 运行时物品和建造物在服务器生成对应表现Actor，而不是只存在于会话 JSON。

替代方案是让 Viewer 根据场景 JSON 自行重建世界。它会导致服务器和查看端两套投影逻辑，材质、光照和碰撞容易漂移，也会让运行时建造物难以同步。

### D8. 查看者只读在会话权限和命令处理层强制

只读检查分三层：

1. 会话参与者类型为 `Viewer` 时，`CanPerform` 拒绝 `ManageSession`、`ControlOwnCharacter`、`WorldInteract` 和 `ViewPrivateInventory`。
2. 游戏 RPC 处理前检查参与者类型，Viewer 的移动、表演、物品和建造 RPC 返回 `VIEWER_READ_ONLY`。
3. 控制 API 绑定到 Viewer 参与者时，所有修改方法返回 `VIEWER_READ_ONLY`，查询方法仍可用。

Viewer 不生成角色 Pawn，因此正常路径没有角色 RPC；上述检查用于防止伪造 RPC 和错误绑定。测试必须直接构造伪造请求，而不是只验证 UI 按钮被禁用。

替代方案是只在 Viewer HUD 隐藏控制按钮。它无法防止自定义客户端或重放请求，不能作为权限边界。

### D9. 场景运行时元数据使用 schema v2 的可选 `runtime` 对象

场景文档增加可选的顶层 `runtime` 对象：

```json
{
  "runtime": {
    "agent": {
      "controllableCharacterIds": ["..."],
      "spawnPoints": [{ "id": "agent-spawn-a", "transform": {...} }],
      "allowedActions": ["wave", "bow"]
    },
    "viewer": {
      "cameraIds": ["..."],
      "defaultCameraId": "...",
      "spawnTransform": {...},
      "showOverlay": true
    }
  }
}
```

场景 schema 从 1 升到 2。版本 1 迁移时写入空 `runtime`，行为保持不变：没有 Agent 可控角色，默认查看相机回退到当前活动相机，`LevelPath` 沿用原字段。旧字段不删除，保存时保留未知的 `runtime` 子字段。

替代方案是把元数据塞进实体 `Data`。这样不需要 schema 迁移，但跨实体引用、默认相机和校验会散落在多个实体里，难以在装载前统一验证。

### D10. 配置、脚本和进程边界

新增配置：

- `MaxViewers=4`
- `ViewerTokenFile=Saved/Epicode/viewer-token`
- `ViewerDefaultCamera` 可选覆盖
- `AgentLeaseSeconds=30`
- `AgentHeartbeatSeconds=10`
- `AgentCommandTimeoutSeconds=5`
- `bAllowRemoteAgents=false`
- `bAllowRemoteViewers=false`

脚本：

- `Viewer.sh`：启动 `UnrealEditor -game`，传入查看者身份、令牌、相机模式和分辨率。
- `AgentSmoke.sh`：只启动 HTTP/WebSocket 客户端，完成加入、租约、移动、观察、释放。
- `ViewerSmoke.sh`：启动服务器和 Viewer，验证相机、世界表现、只读拒绝和会话结束。
- `Client.sh` 保留为表演者调试客户端。

进程日志明确角色：`Editor`、`Headless`、`Server`、`Viewer`、`Agent`。Viewer 日志继续记录控制 API 在 Game 客户端模式禁用。

### D11. 验证分层

1. **领域测试**：参与者类型、租约状态机、权限矩阵、相机元数据、场景 schema 迁移。
2. **服务器集成测试**：Agent 加入、租约、移动完成/受阻、事件过滤、Viewer 加入、只读拒绝、查看者槽位。
3. **Agent 冒烟**：无 Unreal 进程，HTTP/WS 完成闭环，断言 `character.state` 和事件。
4. **Viewer 冒烟**：服务器 + Viewer，断言相机切换、角色可见、世界实体可见、伪造控制被拒绝。
5. **回归**：现有 63 个测试、`Smoke.sh`、`NetworkSmoke.sh` 和干净构建继续通过。

## Risks / Trade-offs

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 场景关卡切换导致会话重建 | Agent 租约和 Viewer 连接丢失 | 引擎子系统保留会话；URL 恢复标记接管已有会话；冒烟覆盖切换 |
| 导航数据缺失 | Agent `moveTo` 不可用 | 场景校验返回警告；移动命令返回 `NAVIGATION_UNAVAILABLE`；不伪造位置 |
| 查看端世界复制范围过大 | 带宽和 CPU 压力 | 首版只复制可见实体和变化状态；Replication Graph 留到后续 |
| Viewer 只读被绕过 | 查看端可控制角色 | 权限层、RPC 层和 API 层三重拒绝；伪造请求测试 |
| Viewer 令牌泄漏 | 未授权观看 | 默认只本机/LAN、令牌文件 0600、远程需显式开启 |
| Agent 命令过慢或重试 | 重复动作、租约过期 | 幂等键、状态版本、租约心跳和速率限制 |
| Game 客户端打包崩溃 | Viewer 只能用编辑器二进制 | 本变更继续 `UnrealEditor -game`；打包修复另立变更 |
| 场景 schema v2 迁移 | 旧场景加载失败或字段丢失 | 自动迁移 v1；未知 runtime 子字段保留；保存往返测试 |
| 表演者与 Viewer 共用 GameMode | 登录阶段误占角色 | 查看者令牌在登录阶段校验；独立查看者控制器和 Pawn |
| Agent 与表演者同时控制角色 | 所有权冲突 | 会话角色租约独占；表演者 PlayerController 也走同一所有权检查 |

## Migration Plan

1. 增加场景 schema v2 和 v1 迁移，旧场景保存时写入空 `runtime` 对象。
2. 增加参与者类型和租约字段，默认值保持现有表演者行为不变。
3. 增加 Viewer 配置和令牌文件生成，默认查看者功能启用但仅本机/LAN。
4. 增加 Agent 控制 API 方法和参与者令牌，现有命令继续兼容。
5. 增加运行时场景表现和关卡恢复，先在网络冒烟场景验证，再启用正式场景。
6. 增加脚本和冒烟，现有 `Client.sh`、`NetworkSmoke.sh` 和 Studio 流程不变。
7. 回滚时关闭 Viewer/Agent 配置并使用原表演者客户端；场景 v2 可被新代码读取，旧代码需要保留 v1 备份或迁移回退。

## Open Questions

- 共享导演相机和录制控制是否需要独立的相机复制通道，留到多机位变更。
- Agent 是否需要低延迟连续输入通道，取决于后续智能体的控制频率。
- Viewer 是否需要像素流或 NDI 输出，留到视频输出变更。
- 打包 Game 客户端崩溃的根因和修复排期，另立变更跟踪。
