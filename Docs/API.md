# Epicode API

## 鉴权

除 `GET /api/v1/health` 外，所有 HTTP 请求需要：

```http
Authorization: Bearer <Saved/Epicode/api-token>
```

也接受 `X-Epicode-Token: <token>`。默认只允许回环地址；远程访问需要 `bAllowRemote=true`。CORS 默认关闭，需要浏览器跨域访问时设置 `bEnableCors=true`。每个来源默认限制每秒 200 个请求，超限返回 429 `RATE_LIMITED`。

## HTTP 端点

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| GET | `/api/v1/health` | 健康、版本、场景和端口状态 |
| GET | `/api/v1/methods` | 命令方法列表 |
| POST | `/api/v1/command` | 自定义命令信封 `{method, params}` |
| POST | `/api/v1/commands/:method` | 以路径指定方法，body 作为 params |
| GET | `/api/v1/commands/:method` | 查询类方法，query 作为 params |
| POST | `/api/v1/rpc` | JSON-RPC 2.0 单请求或批量 |
| POST | `/api/v1/batch` | 简化批量 `{commands:[{method,params}]}` |
| POST | `/api/v1/ws-ticket` | 生成 30 秒单次 WebSocket 票据 |

### 自定义信封

```bash
TOKEN=$(cat Saved/Epicode/api-token)
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"method":"scene.new","params":{"name":"Demo"}}'
```

响应：

```json
{
  "ok": true,
  "requestId": "...",
  "method": "scene.new",
  "httpStatus": 200,
  "result": {"sceneId": "...", "name": "Demo", "scene": {}},
  "affected": [],
  "warnings": [],
  "eventSequence": 1,
  "command": {}
}
```

失败：

```json
{
  "ok": false,
  "error": {"code":"NO_SCENE","message":"No scene is open.","detail":"","retryable":false}
}
```

### JSON-RPC 2.0

单请求：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/rpc \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"system.health","params":{}}'
```

批量：

```json
[
  {"jsonrpc":"2.0","id":1,"method":"scene.snapshot","params":{}},
  {"jsonrpc":"2.0","id":2,"method":"entity.list","params":{}}
]
```

无 `id` 的请求按通知处理，不返回响应。错误码：`-32700` 解析错误、`-32600` 无效请求、`-32601` 方法不存在、`-32602` 参数错误、`-32603` 内部错误；业务错误使用 `data.domainCode`。

HTTP 层：缺少或错误令牌返回 401，非 `application/json` 返回 415，请求体超过 `MaxRequestBytes` 返回 413，非法 JSON 返回 400，超过速率上限返回 429。

### 幂等

写操作可带 `Idempotency-Key` 请求头或 body `idempotencyKey`。相同键与相同参数在 24 小时内返回缓存结果，响应中 `idempotentReplay=true`；相同键不同参数返回 409 `IDEMPOTENCY_CONFLICT`。缓存上限 10000 条，持久化在 `Saved/Epicode/idempotency.json`，重启后仍可重放。

## 命令分组

- 系统：`system.health`、`system.methods`、`system.stats`
- 场景：`scene.new/open/switch/save/saveAs/close/list/delete/snapshot/transaction/import/render`
- 实体：`entity.create/delete/duplicate/rename/reparent/setTransform/setVisible/list/get`
- 角色：`character.spawn/bind/play/pause/seek/setSpeed/lookAt/attach/detach/setMaterial/import`
- 道具：`prop.spawn/create/setMaterial/resetMaterial/setPhysics/attach/import`
- 灯光：`light.create/update/delete/list`
- 环境：`environment.get/set/applyPreset/listPresets`
- 相机：`camera.create/update/delete/activate/list`
- 导入：`import.capabilities/classify/submit/file/run/status/list/cancel`
- 渲染：`render.capabilities/submit/status/list/cancel/retry`
- 多人会话：`session.create/start/join/leave/reconnect/status/snapshot/export/end/kick/setRole/assignCharacter/releaseCharacter/transferCharacter`、`network.status`

`system.methods` 是方法列表的权威来源。

多人会话命令的权限、参数和快照字段见 `Docs/Multiplayer.md`。`system.health` 的 `network` 对象包含模块、在线子系统、端口和当前会话状态。

## WebSocket

```bash
TICKET=$(curl -sS -X POST http://127.0.0.1:8710/api/v1/ws-ticket \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" -d '{}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["ticket"])')
```

连接：

```
ws://127.0.0.1:8711/api/v1/ws?ticket=<TICKET>&since=<optional sequence>
```

票据 30 秒内有效且只能使用一次。连接后收到：

```json
{"type":"welcome","apiVersion":"v1","sequence":12}
```

发送：

```json
{"id":"1","method":"entity.list","params":{}}
{"type":"ping"}
```

收到：

```json
{"type":"response","id":"1","ok":true,"result":{},"affected":[]}
{"type":"pong","sequence":12}
{"type":"event","event":{"sequence":13,"type":"entity.created","sceneId":"...","entityId":"..."}}
```

`since` 会补发该序号之后的事件（最多 256 条）。事件字段包含 `sequence`、`type`、`sceneId`、`entityId`、`timestamp`、`payload`。

## 错误码

| HTTP | 业务码示例 | 说明 |
| --- | --- | --- |
| 400 | `INVALID_PARAMS` | 参数校验失败 |
| 401 | `UNAUTHORIZED` | 令牌缺失或错误 |
| 403 | `REMOTE_FORBIDDEN` | 未允许远程访问 |
| 404 | `METHOD_NOT_FOUND`、`ENTITY_NOT_FOUND` | 方法或实体不存在 |
| 409 | `NO_SCENE`、`UNSAVED_CHANGES` | 状态冲突 |
| 413 | `INVALID_BODY` | 请求体过大 |
| 422 | `INVALID_ASSET`、`IMPORT_REJECTED` | 资产或导入校验失败 |
| 500 | `SCENE_SAVE_FAILED` | 服务端写入失败 |
| 503 | `NOT_INITIALIZED` | 引擎子系统未就绪 |
