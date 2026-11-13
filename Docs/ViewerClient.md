# Epicode Viewer 客户端

Viewer 是只观看世界、不控制角色的 UE 客户端。服务器用 `ParticipantKind=viewer`、`Role=observer` 记录它，Viewer 不占表演者槽位、不生成表演者角色，也不能发送移动、表演、物品、制作、建造或会话管理请求。

## 启动

```bash
Scripts/Viewer.sh \
  --server 127.0.0.1:7777 \
  --name Viewer \
  --token "$(cat Saved/Epicode/viewer-token)" \
  --camera free \
  --res 1280x720 \
  -- -nullrhi -unattended -nosplash
```

参数：

| 参数 | 说明 |
| --- | --- |
| `--server` | 服务器 `host:port` |
| `--name` | 显示名，服务器生成 `<name>-viewer` 参与者标识 |
| `--token` | 查看者令牌；省略时读取 `Saved/Epicode/viewer-token` |
| `--camera` | 初始模式：`free`、`follow`、`fixed`、`preset` |
| `--res` | 窗口分辨率 `宽x高` |
| `--` | 其余参数原样传给 Unreal |

令牌来自 `ViewerTokenFile`（默认 `Saved/Epicode/viewer-token`，权限 0600）。远程 Viewer 还需要 `bAllowRemoteViewers=true`，否则返回 `VIEWER_NOT_AUTHORIZED`。

## 相机模式

| 模式 | 行为 | 按键 |
| --- | --- | --- |
| `free` | 自由飞行观察 | `1`，WASD/QE 移动，鼠标转向 |
| `follow` | 跟随一个角色，目标离开后保持最后位置 | `2` |
| `fixed` | 固定位置观察 | `3` |
| `preset` | 使用场景 `runtime.viewer.cameraIds` 的预置相机 | `4` |

`O` 切换信息叠加。叠加显示会话状态、参与者数量、相机模式和跟随目标；关闭后为干净画面。Viewer 相机变化写入会话参与者摘要，快照中可通过 `viewerCameraMode`、`viewerCameraId` 和 `viewerCameras` 读取。

## 初始化消息

Viewer 注册成功后，服务器发送 `ClientViewerSetup` JSON：

```json
{
  "viewerId": "SmokeViewer-viewer",
  "sessionId": "...",
  "state": "active",
  "cameraMode": "free",
  "cameraId": "",
  "followTarget": "",
  "defaultCameraId": "<cameraEntityId>",
  "defaultCameraTransform": {"location": [0, -450, 200], "rotation": [-10, 0, 0], "scale": [1, 1, 1]},
  "cameras": [{"entityId": "...", "type": "camera", "name": "Viewer Smoke Camera", "transform": {}}],
  "participants": [],
  "session": {}
}
```

场景 `runtime.viewer.defaultCameraId` 和 `cameraIds` 缺失时回退到场景活动相机和空列表。

## 世界表现与复制

Viewer 与服务器进入场景 `levelPath` 指定的同一关卡。会话创建时如果当前地图不是目标关卡，服务器执行一次带 `EpicodeRestoreSession=1` 标记的 `ServerTravel`；新关卡初始化时网络子系统发现已有活动会话，只重新挂接世界表现，不重复创建会话。

服务器按场景摘要生成可复制的运行时表现 Actor：

| 类型 | 来源 | 复制内容 |
| --- | --- | --- |
| 道具、灯光、相机 | 场景实体摘要 | 变换、可见性、类型和数据 |
| 运行时物品 | `EpicodeWorldState` 物品记录 | 生成、拾取、持有、丢弃和撤销 |
| 运行时建造物 | `EpicodeWorldState` 建造记录 | 生成、撤销 |
| 环境 | 场景 `environment` 对象 | 时间、雾、云、曝光、泛光和预设 |

晚加入 Viewer 通过 UE 初始复制获得当前 Actor；随后连接 `session.snapshot` 或控制 API 快照获得 `items`、`builds`、`environment`、`scene`、角色和事件序号。WebSocket 客户端用 `since` 增量补发，游标过旧时返回 `events.gap` 并要求重新快照。

环境复制使用 `AEpicodeRuntimeEnvironmentActor`，服务器写入环境 JSON，查看端在 `OnRep_Environment` 中通过共享 `FEpicodeEnvironmentController` 应用，避免查看端自行解释场景文档。

## 复制基线

`Scripts/ViewerSmoke.sh` 在会话结束前采样当前快照和本地回环流量，结果同时写入 `Saved/Logs/viewer-smoke-baseline.json` 与 `Saved/Logs/viewer-smoke.log`。以下是 2026-11-04 单 Viewer 冒烟记录：

| 指标 | 值 |
| --- | --- |
| 场景实体 / 角色 / 相机 | 2 / 1 / 1 |
| 运行时物品 / 建造物 | 0 / 0 |
| 在线 Viewer | 1 |
| 角色更新频率 | 30 Hz |
| 事件序号 | 6 |
| 本地回环接收 / 发送 | 5.2 / 5.2 KiB/s |

基线只代表本地 `-nullrhi` 冒烟环境；回环采样会随机器负载波动，实体数量、角色更新频率和事件序号是稳定断言。真实场景、绘制距离、角色数量和网络模拟会改变带宽。日志中的 `Epicode viewer baseline:` 行是比对基准，脚本失败时不会写入通过标记。

## 只读边界

- 登录阶段选择 `AEpicodeViewerPlayerController`，不调用表演者注册。
- 服务器跳过表演者 `RestartPlayer`，不生成 `AEpicodeNetworkCharacter`。
- Viewer 只拥有 `AEpicodeViewerSpectatorPawn`，可自由飞行、跟随和切换预置相机。
- 会话权限、角色 RPC 和 API 处理层均拒绝 Viewer 控制请求，返回 `VIEWER_READ_ONLY`。

## 冒烟

```bash
Scripts/ViewerSmoke.sh
```

脚本启动服务器和 `UnrealEditor -game` Viewer，断言 Viewer 注册、快照中 `viewerCount=1`、Viewer 无角色且相机摘要存在，输出 `Saved/Logs/viewer-smoke.log` 和 `Saved/Logs/viewer-smoke-server.log`。

## 已知限制

- 当前无头环境使用 `-nullrhi`，因此冒烟验证状态与日志，不比较像素。
- 运行时物品和建造物使用引擎基础立方体作为占位表现，正式资源替换留待后续美术接入。
- 自动化测试验证复制标记、Actor 生成和快照字段，不建立真实网络连接比较像素。
- 预置相机使用场景相机 Actor 变换；镜头切换没有过场动画。
- 跟随模式按 10 Hz 更新位置，不插值相机旋转。
