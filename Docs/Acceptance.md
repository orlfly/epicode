# 验收记录

日期：2026-11-05  
环境：Linux x86_64，Unreal Engine 5.8.4，`UE_ROOT=/usr/local/unreal`  
范围：`redesign-sandbox-director`（制作包、沙盒、导演、重演、Take 渲染、Studio 工作区、控制 API、脚本回归）

## 干净构建

```bash
cd engine
./Scripts/Clean.sh
./Scripts/Build.sh
```

结果：`Result: Succeeded`，构建退出码 0。日志：`engine/Saved/Logs/build-clean.log`。

## 自动化测试

```bash
./Scripts/Test.sh
```

结果：162 个测试全部 `Success`，0 个 `Fail`，`TEST COMPLETE. EXIT CODE: 0`。日志：`engine/Saved/Logs/test-clean.log`。

覆盖分组：

| 前缀 | 数量 | 范围 |
| --- | --- | --- |
| `Epicode.Core.*` | 16 | 场景、实体、层级、投影、撤销、令牌、环境 |
| `Epicode.Foundation.*` | 4 | 内容摘要、确定性配置、健康、进程角色 |
| `Epicode.Import.*` | 5 | 分类、选项、角色导入、取消、撤销 |
| `Epicode.Production.*` | 7 | 制作包文档、示例、校验、库、命令、撤销、legacy |
| `Epicode.Sandbox.*` | 15 | 世界、tick、确定性、规则、交互、资源、NPC、生成、预算、权限、持久化 |
| `Epicode.Director.*` | 7 | 节拍、叙事、cue、控制、接管、Take、视角 |
| `Epicode.Replay.*` | 7 | journal、快照、ghost、resim、分支、保留、作业 |
| `Epicode.Render.*` | 10 | 作业、取消、超时、诊断、产物校验、Take/TakeDeterminism/TakeTracks/TakeLifecycle |
| `Epicode.Network.*` | 55 | 会话、权限、角色、复制、表演、Agent、Viewer、沙盒桥、旧路径回归 |
| `Epicode.API.*` | 12 | 信封、事件过滤、Agent、制作包、沙盒、导演、重演 |
| `Epicode.Studio.*` | 15 | 工作区、沙盒面板、节拍图、导演控制、重演控制、响应性、文档 |
| `Epicode.Viewer.*` | 5 | 相机模式、跟随、叠加、场景相机、初始化 |
| `Epicode.Scene.*` | 4 | runtime v2 命令、元数据、迁移、校验 |

关键新增测试：`Epicode.Network.SandboxIntegration`、`Epicode.Network.CastBinding`、`Epicode.Network.SandboxReplication`、`Epicode.API.Sandbox`、`Epicode.API.Director`、`Epicode.API.Replay`、`Epicode.Studio.DirectorWorkspace`、`Epicode.Studio.DirectorResponsiveness`、`Epicode.Render.TakeTracks`、`Epicode.Render.TakeDeterminism`。

## 沙盒与导演工作流

```bash
./Scripts/SandboxSmoke.sh
```

结果：`Epicode sandbox smoke passed`，退出码 0。日志：`Saved/Logs/sandbox-smoke.log`、`sandbox-smoke-server.log`。

验证链路：

- 生成场景和制作包，服务器按 `sceneId` 载入制作包，`sandbox.state.legacy=false` 且 `productionId` 匹配。
- `production.list`、`production.open`、`production.validate` 返回制作包、场景、cue 和校验结果。
- `sandbox.rules.get` 返回交互点、资源、NPC 档案和配方。
- `sandbox.interact` 执行 `terminal-use`，叙事变量 `alarmLevel=1`。
- `sandbox.collect` 采集 3 个 `metal`，`sandbox.craft` 消耗 2 个产出 `key`，库存保留 `keycard`。
- `sandbox.spawn` 创建道具实体，`sandbox.state` 可见该实体、NPC 角色和 `npcProfileId=guard`。
- `director.status` 初始 `beat-arrival`；`setObjective`、`enterBeat`、`setTimeScale`、`pause/resume` 后状态为 `beat-alarm`、目标 `objective-alarm`、倍率 1.5、未暂停。
- 重复 `director.pause` 使用同一幂等键返回 `duplicate=true`。
- 夹具场景和制作包在脚本退出时删除。

## 重演

```bash
./Scripts/ReplaySmoke.sh
```

结果：`Epicode replay smoke passed`，退出码 0。日志：`Saved/Logs/replay-smoke.log`、`replay-smoke-summary.txt`。

通过 `Epicode.Replay.Journal`、`Snapshot`、`Ghost`、`Resim`、`Branch`、`Retention`、`Job` 覆盖事件写入与校验、快照恢复、ghost 分页、确定性重演、隔离分支、保留清理和作业队列。

## 导演启动

```bash
./Scripts/Director.sh --server 127.0.0.1:1 --production DemoProduction --session DemoSession --log /tmp/director-launch.log -- -nullrhi -unattended -nosplash
```

参数校验：未知参数和非法服务器地址均返回退出码 2。合法启动日志包含：

```text
role=Director
production=DemoProduction
LogEpicodeNetwork: Epicode process role: Director
```

本次用不可达服务器验证启动和参数注入，未建立真实游戏连接；连接后的导演控制由 `SandboxSmoke.sh` 的控制 API 链路覆盖。

## Agent、Viewer 与网络回归

```bash
./Scripts/AgentSmoke.sh
./Scripts/ViewerSmoke.sh
./Scripts/NetworkSmoke.sh
```

三者均返回 0：

- Agent：`Epicode agent smoke passed`，加入、角色分配、租约、心跳、移动、停止、释放和 `character.move.completed` 全部通过，并新增 `sandbox.state` 的 legacy 场景校验。日志：`agent-smoke.log`、`agent-smoke-summary.json`。
- Viewer：`Epicode viewer smoke passed`，两个 Viewer 注册、无角色、相机摘要、世界快照、槽位释放和 `session.end` 通过，并新增 legacy `sandbox.state` 校验。日志：`viewer-smoke.log`、`viewer-smoke-summary.log`、`viewer-smoke-baseline.json`。
- Network：`Epicode network smoke passed`，双客户端占有、出生点/外观、插值（最近 41 个样本最大漂移 113.75 cm，阈值 300 cm）、表演事件、世界事件、断线重连和新增 `sandbox.state` 校验通过。日志：`network-smoke.log`、`network-smoke-server.log`、`network-smoke-client-a.log`、`network-smoke-client-b.log`。

## Take 渲染

自动化覆盖：

- `Epicode.Render.Take`：Take 版本、摘要、输出目录和不可写目录。
- `Epicode.Render.TakeDeterminism`：live 隔离、摘要不匹配、缺少摘要和稳定 digest。
- `Epicode.Render.TakeTracks`：隐式单轨、多轨、序列到帧映射、重叠、越界和相机缺失。
- `Epicode.Render.TakeLifecycle`：清单、轨道命名、未知作业取消/重试错误。

`take.render` 为每个轨道提交独立作业，输出到 `<outputDirectory>/track_<序号>_<cameraId>`，并写入 `take-render.json`。本轮未在 GPU 环境执行实际像素输出；`-nullrhi` 无界面进程只能验证提交、校验、轨道和清单。实际像素渲染仍依赖可用渲染后端。

## 安全与访问

- API 默认监听 `127.0.0.1`，Bearer 令牌来自 `Saved/Epicode/api-token`，权限 `0600`。
- 沙盒 Viewer 写操作返回 `VIEWER_READ_ONLY`；表演者调用导演写方法返回 `FORBIDDEN`。
- 导演命令要求幂等键；API 缺省生成一次性键，重复键返回 `duplicate=true`。
- `SandboxSmoke.sh` 自动选择空闲 API/WS 端口，避免固定端口残留导致启动失败。
- 引擎安装目录未被修改。

## 已知偏差与限制

- `take.render` 按轨道拆分为多个作业，不生成单文件切镜序列；每条轨道的输出目录和清单记录作业标识。
- Replay 冒烟通过自动化测试覆盖 journal 和重演引擎，不启动独立服务器写入实时 journal；实时会话导出仍需后续接入。
- `Scripts/Director.sh` 只负责启动和参数注入，会话角色由服务器参与者绑定决定；真实导演连接验收由控制 API 沙盒链路覆盖。
- 无 GPU 环境不比较像素；PNG/JPG/EXR/MP4 的实际帧渲染沿用既有渲染验收，Take 渲染本轮只验证作业与清单。
- 独立 `EpicodeVideoServer` 目标仍受引擎发行版限制，服务器使用 `UnrealEditor-Cmd -server`。
- Studio 九个页签的逐项手工截图验收未执行；工作区模型、100ms 响应预算和文档契约由自动化覆盖。
- `EpicodeVideo` Game 目标可构建，但未打包 Game 二进制；双客户端冒烟使用 `UnrealEditor -game`。

## 结论

干净构建、162 项自动化测试、制作包/沙盒/导演工作流、重演、Take 渲染校验、导演启动参数、Agent、Viewer 和网络回归均通过。制作包已成为新的作者根，沙盒状态是权威世界，导演命令和事件流可重演，Take 版本不可变并按轨道生成独立渲染作业。

## 2026-11-17 聚焦工作台与编辑脚本闭环

范围：`redesign-editor-shell` 的聚焦工作台、四类实体、本地库、对话/脚本、legacy 兼容和文档。

### 自动化

```bash
cd engine
./Scripts/Test.sh
```

结果：189 项 `Success`，0 项 `Fail`，`TEST COMPLETE. EXIT CODE: 0`，日志 `Saved/Logs/test.log`。新增覆盖：

- `Epicode.EditorShell.LegacyTabs`：legacy 模式三个页签 spawner 存在；`-EpicodeFocusedEditor` 模式不存在。
- `Epicode.Script.Validate`/`Apply`/`Undo`/`Reference`/`Safety`/`Audit`：白名单、资产路径、clientId 映射、单事务提交、一次撤销、失败回滚、确认和审计。
- `Epicode.Assistant.ScriptFlow`：对话脚本卡片、合法脚本应用、非法脚本拒绝、取消、2 次自动修复后停止。
- `Epicode.Assistant.SidecarProtocol`：JSONL 分帧、UTF-8 跨块、delta/done/error/cancelled、非法 JSON 和 1 MiB 行上限。
- `Epicode.Assistant.SidecarBackendFailure`：sidecar 启动/复用、超时、崩溃重启、取消、缺失/空 Key、缺失脚本和 HTTP 模式降级。

### 聚焦模式烟测

命令：

```bash
DISPLAY=:0 ./Scripts/Editor.sh -nosplash \
  -EpicodeFocusedEditorScriptSmoke -EpicodeFocusedEditorExitSmoke \
  -stdout -FullStdOutLogOutput
```

日志 `Saved/Logs/script-smoke.log` 关键行：

```text
Focused editor shell created (stock window hidden: yes)
FocusedEditor script smoke: captured=true status=校验通过，可以应用
FocusedEditor script smoke: applied=true
FocusedEditor script smoke: status=已应用，影响 1 个实体；Ctrl+Z 可整体撤销 entities=2
FocusedEditor script smoke: afterUndoEntities=1
FocusedEditor script smoke complete
FocusedEditor exit smoke: stock editor restored, requesting exit
```

截图：

- `Saved/Screenshots/EditorShellScriptCard.png`，716418 bytes，sha256 `0ff8a992fc622d7b8d2fae84c75a3a9dcb755c3392cd1d3ba006f52f597c58a1`。
- `Saved/Screenshots/EditorShellScriptApplied.png`，736265 bytes，sha256 `c34e9e99f7363864cea8afd052bbdde0320ba019ffd8a709a6ec658bc8bf0f0d`。
- `Saved/Screenshots/EditorShellScriptUndo.png`，716191 bytes，sha256 `5d9cfddff2a9533178cc015e6694db8285dbb813a843ec5732904586588de6b1`。

四类实体、检查器、库面板、导入和删除的既有截图仍见 `Saved/Logs/editor-shell-workbench.log`。

### 兼容

- `./Scripts/Editor.sh` 默认聚焦模式；`--legacy` 仍注册并打开 Studio/Multiplayer/Director 页签。
- `--focused` 与 `--legacy` 互斥；无效参数退出码 2。
- 聚焦模式不注册 Content Browser、蓝图、Sequencer、PIE、地形、植被等入口。

### 已知限制

- DeepSeek Harness sidecar 已接入；真实模型调用需要用户配置 `DEEPSEEK_API_KEY` 和网络。自动化使用 `TestSidecar.py` 和 Mock，不验证模型输出质量。
- sidecar 可选 HTTP 模式只监听 `127.0.0.1`，编辑器 C++ 客户端默认仍走 stdio；HTTP 模式用于本地诊断和外部客户端。
- 视觉烟测使用 Mock 脚本，不验证真实模型输出质量；真实 Key/网络由用户配置。
- 无 GPU 的自动化测试只验证 UI 逻辑和事务，不比较像素；本轮脚本烟测已用 `DISPLAY=:0` 抓取窗口截图。

## 2026-11-24 多人基线收尾

范围：`complete-multiplayer-baseline` 的构建回退、本地 IK、Studio 远程控制 API、网络证据和规格归档。

### 构建与测试

```bash
cd engine
source Scripts/env.sh
"${BUILD_SH}" EpicodeVideoEditor Linux Development -project="${PROJECT_FILE}" -waitmutex -Clean
./Scripts/Build.sh
./Scripts/Test.sh
```

- 先清理 `EpicodeVideoEditor` 目标二进制，再重建；`Result: Succeeded`，日志 `Saved/Logs/build.log`，清理输出 `Saved/Logs/run-clean-target.log`。
- 193 项 `Epicode.*` 测试全部 `Success`，0 项 `Fail`，`TEST COMPLETE. EXIT CODE: 0`，日志 `Saved/Logs/test.log`。
- 新增 `Epicode.Network.IK` 覆盖本地 IK 权重、应用边界、骨骼混合和服务器快照隔离；新增 `Epicode.Network.ServerFallback` 覆盖 Server/Game 回退字段；新增 `Epicode.Studio.ControlApiClient` 和 `Epicode.Studio.MultiplayerRemote` 覆盖远程 JSON-RPC、401、超时、领域错误、令牌掩码和本进程回退。

Server 目标回退验证：

```bash
TARGET=EpicodeVideoServer ./Scripts/Build.sh
```

返回退出码 6。`Saved/Logs/build-server-target.log` 包含引擎原始错误、`serverMode=editor-fallback`、`serverTargetSupported=0` 和回退原因。

### 五个冒烟

```bash
./Scripts/SandboxSmoke.sh
./Scripts/AgentSmoke.sh
./Scripts/ViewerSmoke.sh
./Scripts/ReplaySmoke.sh
./Scripts/NetworkSmoke.sh
```

五个脚本均返回 0：

- `SandboxSmoke.sh`：`Epicode sandbox smoke passed`。
- `AgentSmoke.sh`：`Epicode agent smoke passed`。
- `ViewerSmoke.sh`：`Epicode viewer smoke passed`，两个 Viewer 注册、第二个释放后回到 1 个 Viewer，回环基线写入 `viewer-smoke-baseline.json`。
- `ReplaySmoke.sh`：journal、snapshot、ghost、resim、分支、保留和作业全部通过。
- `NetworkSmoke.sh`：`Epicode network smoke passed`。

网络摘要 `Saved/Logs/network-smoke-summary.json`：

| 字段 | 值 |
| --- | --- |
| `serverMode` | `editor-fallback` |
| `serverBinary` | `/usr/local/unreal/Engine/Binaries/Linux/UnrealEditor-Cmd` |
| `sampleCount` | 58 |
| `maxRecentDrift` | 85.76 cm |
| `rttMs` | `not-measured` |
| `packetLoss` | `not-measured` |
| `measurementScope` | `local-loopback` |

`server-fallback.log` 和 `client-fallback.log` 记录实际二进制、`clientMode=editor-game` 和回退原因。`system.health.network` 与 `session.snapshot` 返回 `serverMode`、`serverTargetSupported`、`fallbackReason`、`serverBinary`、`clientMode`。

### Studio 与 IK

- Studio 多人面板新增 `API`、`Token`、`Connect`、`Disconnect`、`Local`。远程模式通过 `/api/v1/rpc` 异步发送 `system.health`、`session.snapshot` 和会话写命令，1 秒轮询，令牌只显示掩码。
- `UEpicodeLocalIKComponent` 在本地客户端读取 `IKRig` 资产，按手部、脚部和视线权重求解，服务器、专用服务器和远程代理跳过；缺少资产、骨骼或动画实例时记录一次 `LOCAL_IK_UNAVAILABLE` 警告。

### 证据汇总

```bash
./Scripts/CollectEvidence.sh
./Scripts/CollectEvidence.sh --check
```

`Saved/Logs/acceptance-summary.json` 中 `build=passed`、`tests.passed=193`、`tests.failed=0`、五个 `smoke` 均为 `passed`，`network.status=passed`。缺失日志的组写 `not-measured`。

### 已知限制

- 独立 `EpicodeVideoServer` 目标仍受当前 UE 5.8.4 发行版限制；服务器使用 `UnrealEditor-Cmd -server`。
- 未打包的 `EpicodeVideo` Game 二进制启动仍可能触发 `BufferReader`；`Client.sh` 会回退到 `UnrealEditor -game`。
- 本地 IK 需要配置实际 IK Rig 资产和骨骼名；默认配置关闭，仓库不附带 MetaHuman IK Rig。
- RTT 和丢包只在本地回环冒烟，真实网络模拟值仍为 `not-measured`。
