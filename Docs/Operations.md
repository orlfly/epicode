# Epicode 运维手册

## 环境要求

- Linux x86_64
- Unreal Engine 5.8.4，默认安装路径 `/usr/local/unreal`
- `dotnet`、`clang`、`curl`、`python3`
- 工程根目录：`engine/`

引擎安装目录只读，不要在其中创建工程文件。所有路径均通过 `Scripts/env.sh` 解析，可用 `UE_ROOT` 覆盖：

```bash
UE_ROOT=/opt/UnrealEngine ./Scripts/Build.sh
```

## 脚本

| 脚本 | 用途 |
| --- | --- |
| `Scripts/env.sh` | 解析 `UE_ROOT`、项目文件、编辑器/Game/Server 二进制和 `SERVER_MODE`/`CLIENT_MODE`；`--check` 做环境检查 |
| `Scripts/Build.sh` | 构建 `EpicodeVideoEditor Linux Development`；`TARGET=EpicodeVideoServer` 失败时写回退日志并返回 6 |
| `Scripts/Editor.sh` | 启动带界面的编辑器；默认聚焦模式，`--legacy` 进入完整 Editor |
| `Scripts/Headless.sh` | 无界面启动，监听 HTTP/WS |
| `Scripts/Test.sh` | 运行 `Epicode.*` 自动化测试 |
| `Scripts/Smoke.sh` | 启动、建场景、建实体、保存、可选渲染并退出 |
| `Scripts/Server.sh` | 启动服务器和 HTTP/WS 控制面；无 `EpicodeVideoServer` 时回退到 `UnrealEditor-Cmd -server`，支持端口、场景和额外 Unreal 参数 |
| `Scripts/Client.sh` | 启动客户端并连接服务器；`--mode auto|game|editor`，Game 失败时回退 `UnrealEditor -game` |
| `Scripts/NetworkSmoke.sh` | 启动服务器和两个无界面客户端，验证加入、角色出生点/外观、移动、表演、物品/制作/建造、重连和事件顺序，并写 `network-smoke-summary.json` |
| `Scripts/CollectEvidence.sh` | 汇总构建、测试和五个冒烟日志到 `Saved/Logs/acceptance-summary.json`；`--check` 校验摘要 |
| `Scripts/AgentSmoke.sh` | 启动服务器和纯 HTTP Agent，验证加入、租约、移动、观察、释放和事件 |
| `Scripts/Viewer.sh` | 启动只读 Viewer，支持服务器、名称、令牌、相机模式和分辨率 |
| `Scripts/ViewerSmoke.sh` | 启动服务器和两个 Viewer，验证只读注册、无角色、相机摘要、世界快照、多 Viewer 和会话结束 |
| `Scripts/Director.sh` | 以 `-EpicodeDirector` 启动导演客户端，支持服务器、制作包、会话、日志和额外 Unreal 参数 |
| `Scripts/Replay.sh` | 运行 `Epicode.Replay` 自动化重演验证，支持过滤器和日志路径 |
| `Scripts/ReplaySmoke.sh` | 覆盖 journal、快照、ghost、resim、分支、保留清理和作业队列 |
| `Scripts/SandboxSmoke.sh` | 启动制作包会话，验证制作包、交互、生成、NPC、资源、制作、导演节拍和 cue 配置 |
| `Scripts/Clean.sh` | 删除工程生成的 `Binaries/`、`Intermediate/`、`Saved/` |

## 构建

```bash
cd engine
./Scripts/env.sh --check
./Scripts/Build.sh
```

构建日志写入 `engine/Saved/Logs/build.log`。首次构建会编译八个插件：`EpicodeCore`、`EpicodeImport`、`EpicodeSandbox`、`EpicodeDirector`、`EpicodeControlAPI`、`EpicodeStudio`、`EpicodeRender`、`EpicodeNetwork`。

Editor 目标还包含 `EpicodeEditorShell`（默认聚焦工作台）和 `EpicodeAssistant`（对话与编辑脚本）；`EpicodeStudio` 在默认模式只提供事务 sink，九页签 UI 仅在 `--legacy` 注册。

首次使用 DeepSeek 对话前安装 sidecar 依赖：

```bash
cd engine
./Scripts/Assistant/SetupSidecar.sh
```

venv 位于 `Saved/Epicode/Assistant/venv`，不提交仓库。`SetupSidecar.sh --force` 可重建；`PIP_INDEX_URL` 可指定镜像。

当前安装的 Unreal Engine 发行版不支持独立 `Server` 构建目标，`TARGET=EpicodeVideoServer` 会返回退出码 6，并把请求目标、引擎错误、`serverMode=editor-fallback` 和 `serverTargetSupported=0` 写入 `Saved/Logs/build-server-target.log`。多人服务器改用 `UnrealEditor-Cmd -server` 启动，`Server.sh` 把实际二进制和回退原因写入 `Saved/Logs/server-fallback.log`。详见 `Docs/Multiplayer.md`。

## 启动

带界面：

```bash
./Scripts/Editor.sh
```

默认是聚焦工作台（菜单、视口、对象列表/本地库、DeepSeek sidecar 对话、状态栏）。布局、折叠状态、最近场景和视口镜头保存在 `Saved/Epicode/Editor/layout.json`；删除该文件即可恢复默认布局。要打开九页签 Studio，使用 `./Scripts/Editor.sh --legacy`，然后打开 `Window > Epicode Studio`。

无界面：

```bash
./Scripts/Headless.sh
```

默认监听：

- HTTP：`127.0.0.1:8710`
- WebSocket：`127.0.0.1:8711`

健康检查会返回 `network` 对象，包含 `module`、`moduleLoaded`、`onlineSubsystem`、`port`、`maxPlayers`、`sessionRoot`、`state`、`sessionId`、`sceneId`、`playerCount`、`connectedPlayers` 和 `lastError`。

配置位于 `Config/DefaultEpicode.ini`，也可用环境变量覆盖。CORS 默认关闭，只有显式设置 `bEnableCors=true` 才会返回跨域头。`EPICODE_MAX_RPS` 可覆盖每秒请求上限：

```bash
EPICODE_API_HTTP_PORT=18710 EPICODE_API_WS_PORT=18711 ./Scripts/Headless.sh
```

## 令牌

首次启动生成 `Saved/Epicode/api-token`，权限 `0600`。请求需要：

```bash
TOKEN=$(cat Saved/Epicode/api-token)
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8710/api/v1/health
```

令牌不会写入日志。若令牌文件缺失，重启编辑器会重新生成。

## 自动化测试

```bash
./Scripts/Test.sh
```

当前注册 193 个 `Epicode.*` 自动化测试，覆盖核心模型、场景迁移、命令回滚、导入、渲染与 Take 渲染、制作包校验、沙盒世界/规则/玩法/随机流/固定 tick、重演 journal/快照/ghost/resim/分支/保留、导演节拍/cue/接管/Take/视角、网络会话/权限/快照/复制/重连/本地 IK/服务器回退、Agent/Viewer/Studio 工作区、远程控制 API 客户端和文档契约；结果写入 `Saved/Logs/test.log`。

## 多人服务器与客户端

```bash
cd engine
./Scripts/Server.sh --port 7777 --scene <sceneId>
./Scripts/Client.sh --server 127.0.0.1:7777 --name Alice
./Scripts/Client.sh --server 127.0.0.1:7777 --name Bob
```

`Server.sh` 在端口占用时返回非零状态。`EPICODE_GAME_PORT`、`EPICODE_SESSION_SCENE`、`EPICODE_SERVER`、`EPICODE_PLAYER_NAME`、`EPICODE_SERVER_MODE`、`EPICODE_CLIENT_MODE` 可覆盖默认值。服务端日志写入 `Saved/Logs/server.log`，客户端日志写入 `Saved/Logs/client.log`。回退证据分别写入 `Saved/Logs/server-fallback.log` 和 `client-fallback.log`，`system.health.network` 和 `session.snapshot` 返回 `serverMode`、`serverTargetSupported`、`fallbackReason`、`serverBinary`、`clientMode`。

```bash
./Scripts/Client.sh --mode auto --server 127.0.0.1:7777 --name Alice
./Scripts/Client.sh --mode editor --server 127.0.0.1:7777 --name Bob
```

### Agent 与 Viewer

Agent 不启动 Unreal，只使用控制 API。服务器默认 HTTP `8710`、WebSocket `8711`；可用 `EPICODE_API_HTTP_PORT`、`EPICODE_API_WS_PORT` 覆盖。Agent 令牌由 `session.join` 返回，后续请求放在 `X-Epicode-Participant`。

```bash
./Scripts/AgentSmoke.sh
python3 Scripts/AgentClient.py --base http://127.0.0.1:8710 --name Agent --character <id>
```

Viewer 使用 UE 客户端和查看者令牌。首次服务器启动生成 `Saved/Epicode/viewer-token`（`ViewerTokenFile`，权限 0600）；远程 Viewer 需要 `bAllowRemoteViewers=true`。

启动日志会标记进程角色：`Editor`、`Headless`、`Server`、`Viewer`、`Agent`、`Director` 或 `Replay`。命令行标记 `-EpicodeDirector`、`-EpicodeReplay` 优先于通用服务器/无界面判定。纯 HTTP Agent 不启动 Unreal，因此其日志只有请求和事件摘要，不包含引擎启动行。

确定性运行时配置位于 `[/Script/EpicodeSandbox.EpicodeSandboxSettings]`：`FixedTickRate` 默认 30，`RandomSeed` 默认 0 表示启动时生成并记录种子，`CheckpointIntervalEvents` 默认 1000，`ReplayRetentionHours` 默认 24，`MaxConcurrentReplays` 默认 1。启动时会按排序路径对制作包内容计算 SHA1 摘要，并与引擎、项目和规则 schema 版本组成构建摘要写入 `system.health` 和事件日志头。生产目录写入失败时模块仍启动，但 `system.health` 的 `sandbox.journalWritable` 为 false。

```bash
./Scripts/Viewer.sh --server 127.0.0.1:7777 --name Viewer --camera follow --res 1280x720
./Scripts/ViewerSmoke.sh
```

日志与证据：

| 文件 | 内容 |
| --- | --- |
| `Saved/Logs/agent-smoke.log` | Agent 冒烟摘要与通过标记 |
| `Saved/Logs/agent-smoke-summary.json` | 加入、租约、状态、移动、停止、释放和事件 |
| `Saved/Logs/agent-audit.log` | 脱敏 Agent 命令审计，超过 `AgentAuditLogMaxBytes` 轮转为 `.1` |
| `Saved/Logs/viewer-smoke.log` | Viewer 客户端日志 |
| `Saved/Logs/viewer-smoke-server.log` | Viewer 注册、会话和只读检查 |
| `Saved/Logs/viewer-smoke-second.log` | 第二个 Viewer 的客户端日志 |
| `Saved/Logs/viewer-smoke-summary.log` | 最终快照：Viewer 数量、相机摘要、场景实体和环境 |
| `Saved/Logs/viewer-smoke-baseline.json` | 实体数量、角色更新频率、事件序号和本地回环带宽 |
| `Saved/Logs/network-smoke-clean.log` | 干净构建后的表演者网络回归摘要 |
| `Saved/Logs/sandbox-smoke.log` | 制作包、沙盒、NPC、资源和导演工作流摘要 |
| `Saved/Logs/sandbox-smoke-server.log` | 沙盒会话服务器日志 |
| `Saved/Logs/replay-smoke.log` | 重演自动化日志 |
| `Saved/Logs/replay-smoke-summary.txt` | journal、ghost、resim、分支、清理和作业结果 |
| `Saved/Logs/director.log` | 导演客户端启动和进程角色日志 |

## 制作包、导演与重演

制作包位于 `Saved/Epicode/Productions/<productionId>/production.json`，服务器启动时按 `sceneId` 选择匹配的制作包；找不到时回退到仅场景的 legacy 沙盒视图。

导演客户端：

```bash
./Scripts/Director.sh --server 127.0.0.1:7777 --production prod-1 --session <sceneId>
```

非法的服务器地址、分辨率或未知参数返回退出码 2。合法启动会在日志写入 `role=Director`、`production=<id>`，引擎日志同时包含 `Epicode process role: Director`。

沙盒与重演：

```bash
./Scripts/SandboxSmoke.sh
./Scripts/ReplaySmoke.sh
```

`SandboxSmoke.sh` 自动生成场景和制作包，启动服务器后验证 `production.list/open/validate`、`sandbox.rules.get/state/interact/collect/craft/spawn`、NPC 档案、导演目标/节拍/倍率/暂停恢复/幂等和只读结束，成功输出 `Epicode sandbox smoke passed`。制作包和场景夹具在退出时删除。

`ReplaySmoke.sh` 通过 `Replay.sh` 运行 `Epicode.Replay`，要求 Journal、Snapshot、Ghost、Resim、Branch、Retention 和 Job 全部成功，日志写入 `Saved/Logs/replay-smoke.log`。

端口冲突时修改 `ApiHttpPort`/`ApiWsPort` 或使用环境变量；令牌缺失返回 `401`，参与者令牌缺失返回 `403 PARTICIPANT_NOT_BOUND`，Viewer 令牌错误返回 `VIEWER_NOT_AUTHORIZED`。

网络冒烟：

```bash
./Scripts/NetworkSmoke.sh --scene <sceneId> --port 7796
```

脚本自动生成双角色场景夹具并传入 `-EpicodeSmokeMove -EpicodeSmokePerformance -EpicodeSmokeWorld`，等待服务器创建会话、两个客户端占有 `EpicodeNetworkCharacter`，再校验出生点、外观、远程漂移、表演事件、世界事件和 Bob 断线重连。成功输出 `Epicode network smoke passed`，日志写入 `Saved/Logs/network-smoke*.log`；夹具在退出时删除。

`Server.sh` 和 `Client.sh` 在可用时用 `stdbuf -oL` 行缓冲日志，避免冒烟等待日志时受标准输出块缓冲影响。

多人配置位于 `Config/DefaultEpicode.ini` 的 `[/Script/EpicodeNetwork.EpicodeNetworkSettings]` 段：

```ini
GamePort=7777
MaxPlayers=8
ReconnectGraceSeconds=60
SessionRoot=Saved/Epicode/Sessions
DefaultCharacterMesh=
OnlineSubsystem=LAN
bAllowRemote=False
bAutoExportSession=True
```

## 冒烟验收

```bash
./Scripts/Smoke.sh
```

默认执行 API 与场景生命周期检查，不提交渲染。要包含三帧 PNG 渲染：

```bash
EPICODE_SMOKE_RENDER=1 ./Scripts/Smoke.sh
```

渲染需要可用的 GPU/离屏渲染环境；无 GPU 时工作进程会在 `result.json` 中返回诊断。

汇总最近一次构建、全量测试和冒烟证据：

```bash
./Scripts/CollectEvidence.sh
./Scripts/CollectEvidence.sh --check
```

摘要写入 `Saved/Logs/acceptance-summary.json`。缺少日志的组写 `not-measured`，不会把缺失证据报告为通过。

## 清理

```bash
./Scripts/Clean.sh
```

该命令只删除工程目录下的生成物，不触碰 `/usr/local/unreal`。

## 故障排查

- `Unable to bind HTTP port`：端口被占用，修改 `ApiHttpPort` 或设置 `EPICODE_API_HTTP_PORT`。
- `401 UNAUTHORIZED`：确认 `Authorization: Bearer <token>`，令牌来自 `Saved/Epicode/api-token`。
- `NO_SCENE`：先调用 `scene.new` 或 `scene.open`。
- `InterchangeEngine` 未加载：确认插件 `Interchange` 已启用，且在编辑器进程中运行导入。
- 渲染工作进程失败：查看 `Saved/Epicode/RenderJobs/<jobId>/result.json` 和该目录下的日志。

## 首版限制

- `.umap` 通过场景库/编辑器打开，不在资产导入器中批量导入。
- Studio Render 页签只提供固定 320 x 180、三帧 PNG 的快捷提交；完整格式、分辨率、帧范围和相机选择通过 `scene.render` API。
- Studio 列表按场景 revision 轮询刷新，不订阅 WebSocket 事件；导入/渲染任务由服务层持有，重新打开面板后会重新查询。
- 运行中的同步 Interchange 导入无法中断。取消后会删除本次创建的实体，但保留已经导入的资产。
- `scene.render` 只输出活动相机；多相机使用 `take.render` 的逐轨道作业。`includeAudio` 被忽略，MP4 依赖系统 `ffmpeg`。
