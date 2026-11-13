# Epicode 控制 API：Agent 与会话

本文补充 `Docs/API.md`，说明 Agent 参与者令牌、命令信封、幂等、状态版本、WebSocket 事件过滤、错误码和审计。基础鉴权、HTTP 端点、JSON-RPC 与批量请求仍以 `Docs/API.md` 为准。

## 参与者令牌

`session.join` 的 `participantKind=agent` 会返回 `participantToken`。后续 Agent 请求必须携带：

```http
X-Epicode-Participant: <participantToken>
```

也可以放在请求体 `participantToken`。服务端会：

- 解析令牌到会话参与者。
- 拒绝空、伪造、已离职或已断开的令牌，返回 `PARTICIPANT_NOT_BOUND` 或 `FORBIDDEN`。
- 拒绝 `playerId` / `requesterId` 指向其他参与者的请求。
- 自动把绑定的 `playerId` 和 `requesterId` 注入命令参数。
- 对 Viewer 令牌调用 Agent 命令返回 `FORBIDDEN`。

HTTP、WebSocket 和内部命令共用同一套绑定规则。

## 命令信封

请求字段：

| 字段 | 说明 |
| --- | --- |
| `params` | 命令参数 |
| `requestId` | 可选；缺失时服务端生成，响应原样返回 |
| `idempotencyKey` | 可选；同一参与者、同一方法、同一参数重复请求返回首次结果 |
| `expectedStateVersion` | 可选；与当前会话 `worldVersion` 不一致时返回 `STALE_STATE` |

响应字段：

```json
{
  "ok": true,
  "requestId": "5b7c...",
  "method": "character.moveTo",
  "httpStatus": 200,
  "stateVersion": 12,
  "result": {"playerId": "agent-1", "characterId": "...", "target": "...", "accepted": true},
  "affected": [],
  "warnings": [],
  "eventSequence": 44,
  "idempotentReplay": false,
  "command": {"success": true, "result": {}}
}
```

失败响应使用 `error.code`、`error.message`、`error.detail` 和 `error.retryable`。幂等键以参与者为作用域，使用不同参数复用同一键返回 `IDEMPOTENCY_CONFLICT`。

## Agent 方法

| 方法 | 修改性 | 异步 | 需要租约 | 说明 |
| --- | --- | --- | --- | --- |
| `session.heartbeat` | 是 | 否 | 是 | 续期角色租约，返回 `leaseId` 和 `leaseExpiresAt` |
| `character.moveTo` | 是 | 是 | 是 | 导航移动；无导航数据时服务器使用直接移动回退 |
| `character.lookAt` | 是 | 否 | 是 | 设置朝向 |
| `character.stop` | 是 | 否 | 是 | 停止移动并发布 `character.move.stopped` |
| `character.state` | 否 | 否 | 否 | 观察自己的位置、朝向、速度、动作、表情、租约和状态版本 |

`GET /api/v1/methods` 会输出 `requiredParticipantKind=agent`、`mutating`、`async` 和 `requiresLease`。Agent 命令默认限制为每人每秒 `AgentCommandsPerSecond` 次，超限返回 `RATE_LIMITED`。

### 最小调用

```bash
TOKEN=$(cat Saved/Epicode/api-token)
BASE=http://127.0.0.1:8710

JOIN=$(curl -sS -X POST "$BASE/api/v1/commands/session.join" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"params":{"playerId":"agent-1","displayName":"Agent","participantKind":"agent"}}')
PARTICIPANT=$(printf '%s' "$JOIN" | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["participantToken"])')

curl -sS -X POST "$BASE/api/v1/commands/session.assignCharacter" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -H "X-Epicode-Participant: $PARTICIPANT" \
  -d '{"params":{"characterId":"<characterEntityId>"}}'
```

完整闭环由 `Scripts/AgentClient.py` 和 `Scripts/AgentSmoke.sh` 覆盖。

## 制作包、沙盒与导演方法

以下方法由 `EpicodeStudioAPI` 命令提供者注册，和 `session.*` 共用 HTTP/JSON-RPC 信封、Bearer 令牌、幂等键和 `{code,message,detail,retryable}` 错误模型。带 `actorId` 的写方法会校验沙盒参与者角色；Viewer 返回 `VIEWER_READ_ONLY`，表演者调用导演写方法返回 `FORBIDDEN`。

### 制作包

| 方法 | 变更 | 参数 | 结果 |
| --- | --- | --- | --- |
| `production.list` | 否 | 无 | `productions[]`、`count`、`root` |
| `production.open` | 否 | `productionId` | 文档字段和 `documentPath` |
| `production.create` | 是 | `sceneId`、`name`、可选 `productionId`、`document` | 文档字段和 `documentPath` |
| `production.save` | 是 | `productionId`、可选 `document` | 文档字段和 `documentPath` |
| `production.delete` | 是 | `productionId` | `productionId`、`trashPath` |
| `production.validate` | 否 | `productionId` 或 `document`、可选 `sceneRoot` | 校验报告 |

制作包写方法在存在活跃沙盒桥时要求 `actorId` 为 owner 或 director；编辑器和 CLI 未提供 `actorId` 时按作者模式执行。

### 沙盒

| 方法 | 变更 | 参数 | 结果 |
| --- | --- | --- | --- |
| `sandbox.state` | 否 | 无 | 世界、`digest`、`eventSequence`、`tick`、`replayable`、`legacy` |
| `sandbox.rules.get` | 否 | 无 | 当前制作包 `worldRules` |
| `sandbox.rules.update` | 是 | `productionId`、`rules`、可选 `actorId` | 更新后的规则 |
| `sandbox.budget` | 否 | 无 | 实体、事件、生成和导演 cue 预算 |
| `sandbox.interact` | 是 | `playerId`、`entityId`、`verb` | 交互后的实体或世界 |
| `sandbox.spawn` | 是 | `actorId`、`entityId`、`kind`、可选 `archetypeId`、`location`、`rotation`、`data` | 新实体 |
| `sandbox.collect` | 是 | `playerId`、`entityId`、可选 `amount` | 世界状态 |
| `sandbox.craft` | 是 | `playerId`、`recipeId` | 世界状态 |

`sandbox.spawn` 仅 owner/director 可调用，实体数超过 `budgets.maxEntities` 时返回 `SANDBOX_BUDGET_EXCEEDED`。`sandbox.interact`、`collect` 和 `craft` 允许 performer/agent/owner/director，Viewer 只读。

### 导演

| 方法 | 参数 | 结果 |
| --- | --- | --- |
| `director.status` | 无 | 暂停、倍率、时间、相机、节拍、目标、视角、消息和接管租约 |
| `director.pause` / `director.resume` | `actorId`、可选 `idempotencyKey` | `duplicate` |
| `director.setTimeScale` | `actorId`、`scale`（0.1–4.0） | `duplicate` |
| `director.acquireTakeover` / `director.releaseTakeover` | `actorId`、`participantId`、可选 `reason`、`endCondition` | `duplicate` |
| `director.setObjective` | `actorId`、`objectiveId` | `duplicate` |
| `director.enterBeat` | `actorId`、`beatId` | `duplicate` |
| `director.setView` | `actorId`、`mode`（0=live, 1=ghost, 2=resim）、可选 `replayRoot`、`sequence` | `duplicate` |

导演写方法必须有 owner/director 参与者；缺少 `idempotencyKey` 时 API 生成一次性键，重复键会返回 `duplicate=true`。ghost/resim 视角下除 `director.setView` 外的写命令返回 `REPLAY_READ_ONLY`。

### 重演与 Take

| 方法 | 变更 | 参数 | 结果 |
| --- | --- | --- | --- |
| `replay.ghost` | 否 | `root`、可选 `startSequence`、`count` | 首末序号和事件页 |
| `replay.branch` | 是 | `parentRoot`、`branchRoot`、`branchId`、`forkSequence`、可选 `actorId` | 分支元数据路径 |
| `replay.job` | 是 | `root`、`targetSequence`、可选 `actorId`、`allowInexact`、`expectedContentDigest` | 作业状态、摘要和差异 |
| `replay.cancel` | 是 | `jobId` | 取消后的作业状态 |
| `take.list` | 否 | `takeId`、可选 `root` | 版本和文件路径 |
| `take.get` | 否 | `takeId`、`version`、可选 `root` | Take 字段和校验和 |
| `take.create` | 是 | `take` 对象、可选 `root` | 不可变版本 |
| `take.revise` | 是 | `takeId`、可选 `take` 补丁、`root` | 新版本 |

分支创建要求 fork 序号位于 journal 范围内并存在可重演检查点；历史不足返回 `REPLAY_HISTORY_UNAVAILABLE` 或 `BRANCH_POINT_UNAVAILABLE`。Take 创建和修订写入 `Saved/Epicode/Takes/<takeId>-v<version>.json`，版本号只增不减。

## WebSocket 事件过滤

连接地址：

```text
ws://127.0.0.1:8711/...?since=42&filter.session=<sceneId>&filter.participant=agent-1&filter.character=<id>&filter.category=character,agent
```

- `since` 从指定事件序号补发。
- `filter.session` 匹配事件 `sceneId` 或 payload 的 `sessionId`。
- `filter.participant` 匹配 payload 的 `playerId`、`requesterId`、`agentPlayerId`、`targetPlayerId` 或 `ownerPlayerId`。
- `filter.character` 匹配 `entityId` 或 payload 的 `characterId`。
- `filter.category` 是逗号分隔的类型前缀，例如 `character` 匹配 `character.move.completed`。
- 历史已被裁剪时先发送 `events.gap`，包含 `requestedSince`、`oldestSequence` 和 `latestSequence`，再按过滤器补发仍保留的事件。

## 错误码

| 错误码 | 含义 |
| --- | --- |
| `PARTICIPANT_NOT_BOUND` | 缺少或无法解析参与者令牌 |
| `FORBIDDEN` | 令牌与请求参与者不匹配，或 Viewer 调用 Agent 方法 |
| `CHARACTER_NOT_ASSIGNED` | 尚未获得角色 |
| `NOT_CONTROLLER` | 服务器没有该 Agent 的控制器 |
| `LEASE_EXPIRED` | 租约已过期，需要重新获取 |
| `NAVIGATION_UNAVAILABLE` | 世界没有导航数据且未启用回退 |
| `RATE_LIMITED` | 超过 Agent 命令速率 |
| `STALE_STATE` | `expectedStateVersion` 与会话状态不一致 |
| `IDEMPOTENCY_CONFLICT` | 同一幂等键使用了不同参数 |

## 审计

Agent 命令结果写入 `Saved/Logs/agent-audit.log`，只包含时间、方法、请求标识、参与者、来源、成功/错误码、HTTP 状态、状态版本和事件序号。令牌、幂等键、请求参数和请求体不会进入审计日志。文件超过 `AgentAuditLogMaxBytes` 后轮转为 `agent-audit.log.1`。
