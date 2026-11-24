# Tasks

## 1. 工作区与提交基线

- [x] 1.1 新增 `.gitignore`，覆盖 `engine/Binaries/`、`engine/Intermediate/`、`engine/Saved/`、`engine/DerivedDataCache/`、`engine/Plugins/*/Binaries/`、`engine/Plugins/*/Intermediate/` 和 `engine/Scripts/Fab/__pycache__/`。验证：`git status --short` 不再列出这些构建产物，`git check-ignore` 对每个模式返回匹配路径。
- [x] 1.2 提交当前 Fab 文档和 `engine/Scripts/Fab/decode_json_manifest.py`，不包含构建产物或缓存。验证：`git status --short` 中 Fab 文档和解码器不再是未提交状态，`python3 -m py_compile engine/Scripts/Fab/decode_json_manifest.py` 返回 0。

## 2. 服务器与客户端回退

- [x] 2.1 扩展 `Scripts/env.sh`，解析 `SERVER_BIN`、`CLIENT_BIN`、`EPICODE_SERVER_MODE` 和 `EPICODE_CLIENT_MODE`，并让 `env.sh --check` 输出实际路径与模式。验证：`./Scripts/env.sh --check` 输出四项且不修改 `/usr/local/unreal`。
- [x] 2.2 扩展 `Scripts/Build.sh`，支持 `TARGET=EpicodeVideoServer` 和 `TARGET=EpicodeVideo`；当引擎返回不支持 Server 目标时以退出码 6 失败并写入 `Saved/Logs/build-server-target.log`。验证：该日志包含请求目标、引擎原始错误和 `serverMode=editor-fallback`，编辑器目标构建仍返回 `Result: Succeeded`。
- [x] 2.3 扩展 `Scripts/Server.sh`，按 `EpicodeVideoServer`、`UnrealEditor-Cmd -server` 顺序选择二进制，记录 `serverMode`、`serverBinary` 和回退原因，回退不可用时返回非零退出码。验证：`Saved/Logs/server-fallback.log` 包含 `serverMode=editor-fallback` 和实际二进制路径，端口可监听。
- [x] 2.4 扩展 `Scripts/Client.sh`，增加 `--mode auto|game|editor`；`auto` 优先 Game 二进制，启动崩溃或日志出现 `BufferReader` 时记录 `clientMode=game-binary` 并回退到 `UnrealEditor -game`。验证：`Saved/Logs/client-fallback.log` 包含崩溃原因、回退命令和 `clientMode=editor-game`，客户端能连接本地服务器。
- [x] 2.5 在 `system.health.network` 和 `session.snapshot` 增加 `serverMode`、`serverTargetSupported`、`fallbackReason`、`serverBinary` 字段，并在客户端健康信息或日志中单独记录 `clientMode`。验证：新增 `Epicode.Network.ServerFallback` 测试通过，HTTP `system.health` 返回服务器字段，客户端日志返回 `clientMode`。
- [x] 2.6 更新 `Docs/Operations.md`、`Docs/Multiplayer.md` 和 `Docs/Architecture.md` 的服务器/客户端回退、退出码和健康字段说明。验证：文档中的命令逐条执行成功，字段名与 `Epicode.Network.ServerFallback` 和日志一致。

## 3. 本地 IK

- [x] 3.1 启用 `IKRig` 插件依赖，新增 `FEpicodeLocalIKSettings`，包含手部、脚部、视线骨骼名、IK Rig 软引用、目标偏移和启用开关。验证：`EpicodeVideo.uproject` 和 `EpicodeNetwork.Build.cs` 包含依赖，默认 CDO 的权重为零且不加载 IK 资产。
- [x] 3.2 新增本地 IK 组件或动画桥接，把 `AEpicodeNetworkCharacter::ApplyLocalIKCorrections` 的三个权重写入组件；服务器和远程客户端跳过求解。验证：新增 `Epicode.Network.IK` 用例断言本地组件收到权重、服务端和远程实例不创建 IK 目标。
- [x] 3.3 实现 IK Rig 目标应用，覆盖手部、脚部和视线；权重为零时跳过，缺少 IK Rig、骨骼或动画实例时记录一次带角色和缺失项的警告。验证：`Epicode.Network.IK` 覆盖手部、脚部、视线、缺失骨骼、缺失动画实例和权重归零。
- [x] 3.4 增加可复现的 IK 测试夹具或合成骨架，断言应用前后本地骨骼变换有差异，服务器快照的位置、旋转和复制字段不变。验证：`Saved/Logs/test.log` 中 `Epicode.Network.IK` 为 `Success`，日志包含骨骼变换差异和服务端快照不变断言。
- [x] 3.5 更新 `Docs/Multiplayer.md` 的 IK 章节，说明 IK Rig 资产要求、本地-only 边界和缺失资产降级。验证：文档中的测试名、警告文本和限制与 `Epicode.Network.IK` 日志一致。

## 4. Studio 远程连接

- [x] 4.1 新增 `FEpicodeControlApiClient`，使用 `FHttpModule` 异步请求 `POST /api/v1/rpc`，支持 Bearer Token、超时、401、连接失败和 JSON-RPC 领域错误。验证：新增 `Epicode.Studio.ControlApiClient` 测试覆盖成功、401、超时和错误解析。
- [x] 4.2 把多人面板的 `Server` 输入改为控制 API 地址，增加令牌输入、连接/断开和本进程/远程模式切换，默认提示 `http://127.0.0.1:8710`。验证：`Epicode.Studio.MultiplayerRemote` 构造面板并断言连接控件、模式切换和空地址回退。
- [x] 4.3 远程模式下把创建、加入、刷新、结束、踢人、分配角色和修改角色路由到远程 `session.*` 方法；远程错误显示在面板，不更新本地伪状态。验证：`Epicode.Studio.MultiplayerRemote` 覆盖远程创建、`FORBIDDEN`、超时和本进程回退。
- [x] 4.4 实现 1 秒异步 `session.snapshot` 轮询、100 ms 界面响应预算和令牌掩码/日志脱敏。验证：`Epicode.Studio.MultiplayerRemote` 和 `Epicode.Studio.Responsiveness` 通过，日志不包含完整令牌。
- [x] 4.5 更新 `Docs/Studio.md` 和 `Docs/Usage.md` 的远程连接、令牌来源、默认地址和错误处理说明。验证：文档中的地址、命令和错误码与 `Epicode.Studio.MultiplayerRemote` 日志一致。

## 5. 证据与基线收口

- [x] 5.1 扩展 `Scripts/NetworkSmoke.sh`，写 `Saved/Logs/network-smoke-summary.json`，包含 `sampleCount`、`maxRecentDrift`、`rttMs`、`packetLoss`、`serverMode`，未采样指标写 `not-measured`。验证：摘要 JSON 可解析，字段与 `network-smoke.log` 一致。
- [x] 5.2 新增 `Scripts/CollectEvidence.sh`，读取构建、测试和五个冒烟日志，生成 `Saved/Logs/acceptance-summary.json`。验证：摘要包含 `build`、`tests`、`smoke`、`network` 四组字段，缺少日志时写 `not-measured` 而不是猜测。
- [x] 5.3 运行干净构建、全量测试、`SandboxSmoke.sh`、`AgentSmoke.sh`、`ViewerSmoke.sh`、`ReplaySmoke.sh` 和 `NetworkSmoke.sh`，要求全部返回 0。验证：`acceptance-summary.json` 中 `tests.failed=0`，五个 `smoke` 均为 `passed`。
- [x] 5.4 用 `acceptance-summary.json` 更新 `Docs/Acceptance.md` 和 `Docs/Multiplayer.md` 的测试数量、样本数、漂移、RTT 和丢包口径。验证：`CollectEvidence.sh --check` 或等价比对返回 0，文档与摘要无差异。
- [x] 5.5 更新 `Docs/Operations.md` 和 `Docs/Usage.md` 的证据脚本用法，说明本地回环与网络模拟的边界。验证：文档中的 `CollectEvidence.sh` 和 `NetworkSmoke.sh` 命令逐条执行成功。
- [x] 5.6 回填 `add-multiplayer-production-baseline/tasks.md` 的 1.1、1.2、5.6、7.1、7.6，引用本变更的日志和测试；确认基线任务达到 51/51。验证：`openspec list --json` 中基线 `completedTasks` 等于 `totalTasks`，每一项验证都指向实际日志。
- [x] 5.7 在归档基线前，更新其 `engine-foundation` delta 的“多人网络构建目标”要求：引擎支持时使用 Server 目标，不支持时使用 `UnrealEditor-Cmd -server` 并在健康检查标记 `editor-fallback`；同步 1.2 的验收描述。验证：基线 spec 不再要求当前发行版不可用的独立 Server 目标，`openspec validate add-multiplayer-production-baseline --strict` 通过。
- [x] 5.8 更新基线变更的“当前实现偏差”和 `Docs/Multiplayer.md` 的“当前边界”，移除已经闭环的限制，保留仍未实现的 P4–P8。验证：文档不再声称 IK 只有权重、Studio 只能本进程、服务器目标未验证。

## Workflow follow-up

- 评审通过后按顺序归档：`add-multiplayer-production-baseline`、`complete-multiplayer-baseline`、`redesign-sandbox-director`、`add-agent-and-viewer-clients`。
- 每归档一个变更后运行 `openspec validate --all --strict`，确认主 specs 包含 `multiplayer-session`、`networked-performance`、`interactive-world`、`sandbox-world`、`session-replay`、`agent-control` 和 `world-viewing`。
- 最后运行 `openspec list --specs` 和 `git status --short`，确认没有未归档变更、没有未忽略的构建产物、工作区干净。
