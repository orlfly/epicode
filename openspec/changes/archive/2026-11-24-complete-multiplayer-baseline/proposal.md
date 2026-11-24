# Proposal

## Why

`add-multiplayer-production-baseline` 已完成 46/51 项，核心会话、移动、表演、物品、制作、建造和双客户端冒烟均可用，但剩余 5 项阻塞归档：独立 Server 目标受引擎发行版限制、Game 客户端未打包运行、IK 只有权重没有实际动画应用、Studio 面板没有远程 HTTP 连接、面板文档未在真实双客户端环境验收。同时 `redesign-sandbox-director` 和 `add-agent-and-viewer-clients` 任务已完成却未归档，主 specs 仍缺少多人、沙盒、重演和 Agent/Viewer 能力，导致“任务完成”与“规格完成”不一致。

本变更只做收尾和证据闭环，不重新设计多人基线，也不扩大到 P4–P8。

## What Changes

- **构建目标收尾**：保持 `/usr/local/unreal` 只读，明确 `EpicodeVideoServer` 在当前引擎发行版不可构建时的受支持回退路径；为 `EpicodeVideo` Game 目标补齐可复现的启动修复或明确降级；`Build.sh`、`Server.sh`、`Client.sh` 和 `system.health` 报告实际运行模式与回退原因。
- **本地 IK 收尾**：把手部、脚部和视线 IK 从“只限制权重”推进到可配置骨骼的实际本地动画修正；缺少骨骼、动画实例或 IK 资产时安全降级并给出一次警告，服务器权威变换不变。
- **Studio 远程连接**：把多人面板的 `Server` 输入接入控制 API 的 HTTP JSON-RPC，支持令牌、超时、连接错误和本进程/远程模式切换；远程轮询不阻塞编辑器超过 100 ms。
- **网络基线证据**：重新运行构建、162 项自动化测试、沙盒/Agent/Viewer/重演/网络冒烟，统一任务、验收文档和日志中的测试数量、漂移样本、RTT 和丢包口径；补充本地回环之外的延迟/丢包采样方法或明确标注未采样。
- **规格归档**：在评审通过后按顺序归档 `add-multiplayer-production-baseline`、本变更、`redesign-sandbox-director` 和 `add-agent-and-viewer-clients`，把 delta specs 同步到主 specs。
- **仓库卫生**：提交当前 Fab 文档和 `decode_json_manifest.py`，补充 Unreal 构建产物忽略规则，避免把 `Intermediate`、`Binaries`、`Saved` 和 `Content` 误纳入版本库。

## Capabilities

### New Capabilities

- `multiplayer-session`: 增加服务器目标不可用时的回退诊断、网络基线采样和会话健康字段，使服务器模式、RTT、丢包和样本口径可查询、可复验。
- `networked-performance`: 增加本地 IK 的实际动画应用要求，覆盖手部、脚部、视线、缺失骨骼降级和服务器权威变换不变。

### Modified Capabilities

- `engine-foundation`: 增加构建目标降级与诊断要求，明确独立 Server 目标、Game 客户端和编辑器回退路径的退出码、日志和健康状态。
- `production-ui`: 增加多人面板远程会话连接要求，覆盖令牌、连接、超时、错误显示和本进程模式保留。

## Impact

- **代码**：`EpicodeNetworkCharacter`、新的本地 IK 组件或动画实例、`EpicodeNetworkSubsystem` 健康字段、`EpicodeStudio` 多人面板与 HTTP 客户端、`EpicodeControlAPI` 客户端辅助代码、`Scripts/Build.sh`、`Server.sh`、`Client.sh`、`env.sh`、`DefaultEpicode.ini`。
- **测试**：新增或扩展 `Epicode.Network.IK`、`Epicode.Network.ServerFallback`、`Epicode.Studio.MultiplayerRemote`、`Epicode.Foundation.BuildTargets`，并重跑现有 162 项测试和五个冒烟脚本。
- **文档**：`Docs/Multiplayer.md`、`Docs/Studio.md`、`Docs/Usage.md`、`Docs/Acceptance.md`、`Docs/Architecture.md`、`Docs/Operations.md`。
- **平台**：Linux x86_64 + UE 5.8.4 保持不变；不修改 `/usr/local/unreal`；Windows/MetaHuman 仍不在本变更范围。
- **规格顺序**：`add-multiplayer-production-baseline` 必须先归档，再归档本变更；本变更的 `multiplayer-session` 和 `networked-performance` delta 只补充基线未覆盖的收尾要求，不重复定义会话和表演主链路。
- **非目标**：P4 场景控制、P5 资产注册表、P6 重演/Take Recorder、P7 多机位导出、P8 Replication Graph 调优；语音音频传输；Windows 客户端打包。
