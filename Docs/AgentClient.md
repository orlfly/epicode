# Epicode Agent 客户端

Agent 是只通过 HTTP/WebSocket 操作会话的自动化参与者。它不启动 Unreal、不渲染画面，使用 `ParticipantKind=agent`、`Role=performer` 和角色租约控制一个场景角色。

## 先决条件

- 服务器已创建并启动会话，场景 `runtime.agent.controllableCharacterIds` 包含目标角色。
- 已生成 `Saved/Epicode/api-token`，或在环境变量设置 `EPICODE_API_TOKEN`。
- 控制 API 可访问，默认 `http://127.0.0.1:8710`。

## 最小循环

```bash
python3 Scripts/AgentClient.py \
  --base http://127.0.0.1:8710 \
  --name Agent \
  --character <characterEntityId> \
  --target 300,200,0 \
  --summary Saved/Logs/agent-client.json
```

客户端依次执行：

1. `session.join`，`participantKind=agent`，保存 `participantToken`。
2. `session.assignCharacter`，服务器自动获取租约并创建 Agent 控制器。
3. `session.heartbeat` 续期。
4. `character.state` 读取自身位置、朝向、速度、动作、表情、持有物、租约和状态版本。
5. `character.moveTo` 移动；无导航数据时服务器使用直接移动回退并最终发布 `character.move.completed`。
6. `character.stop` 发布 `character.move.stopped`。
7. `session.releaseCharacter` 释放角色和租约。
8. `session.snapshot` 读取事件，验证移动完成。

所有 Agent 请求带 `X-Epicode-Participant`。缺少、伪造、断开或 Viewer 令牌返回 `PARTICIPANT_NOT_BOUND` / `FORBIDDEN`。`expectedStateVersion` 可用于乐观并发；不匹配返回 `STALE_STATE`。

## 反馈事件

| 事件 | 触发 |
| --- | --- |
| `agent.lease.acquired` | 获取角色租约 |
| `agent.lease.expired` | 心跳超时，服务器释放角色 |
| `character.move.completed` | 到达目标 |
| `character.move.blocked` | 导航失败或移动受阻 |
| `character.move.stopped` | 主动停止、释放或租约过期 |

WebSocket 可用 `filter.participant`、`filter.character`、`filter.category=agent,character` 和 `since` 订阅；历史缺口返回 `events.gap`。

## 错误与重试

- `LEASE_EXPIRED`：重新 `session.assignCharacter` 或重新加入后获取租约。
- `NOT_CONTROLLER` / `CHARACTER_NOT_ASSIGNED`：先获取角色租约。
- `NAVIGATION_UNAVAILABLE`：目标不可达或无导航；检查场景或改用直接回退。
- `RATE_LIMITED`：降低命令频率，默认每秒 20 条。
- `STALE_STATE`：读取 `character.state` 或快照后，用新版本重试。
- 幂等请求携带 `idempotencyKey`；不同参数复用同一键返回 `IDEMPOTENCY_CONFLICT`。

审计日志位于 `Saved/Logs/agent-audit.log`，不包含令牌、参数或请求体。

## 冒烟

```bash
Scripts/AgentSmoke.sh
```

脚本生成临时场景、启动 API、运行 HTTP Agent、断言 `character.state` 和 `character.move.completed`，输出 `Saved/Logs/agent-smoke.log` 与 `Saved/Logs/agent-smoke-summary.json`。
