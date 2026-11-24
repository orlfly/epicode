# Design

## Context

当前工程是 UE 5.8.4 Linux 编辑器扩展，`/usr/local/unreal` 只读。多人基线已经实现会话、权限、移动、表演、物品、制作、建造和双客户端冒烟，但收尾工作卡在四个技术边界：

- `EpicodeVideoServer.Target.cs` 已存在，但该引擎发行版不支持独立 Server 目标，`Server.sh` 已回退到 `UnrealEditor-Cmd -server`，只是没有把回退模式变成正式契约。
- `EpicodeVideo` Game 目标可以构建，但未打包的 Game 二进制在启动阶段触发 `BufferReader` 断言，`Client.sh` 默认仍用 `UnrealEditor -game`。
- `AEpicodeNetworkCharacter::ApplyLocalIKCorrections` 只保存权重，没有动画实例、骨骼配置或 IK 资产接入。
- `SEpicodeMultiplayerPanel` 有 `ServerInput`，但所有按钮直接调用 `UEpicodeEngineSubsystem::ExecuteCommand`，没有 HTTP 客户端。

现有 162 项测试和五个冒烟脚本已通过，但任务、验收文档和日志中的测试数量与网络样本数字不一致。本设计只处理这些收尾边界，不改变多人基线的会话、复制和权限模型。

## Goals / Non-Goals

**Goals:**

- 让服务器和客户端回退路径成为可查询、可验证、可归档的正式行为。
- 让本地 IK 从权重占位变成实际动画修正，并在资产缺失时安全降级。
- 让 Studio 多人面板可以连接远程控制 API，同时保留本进程模式。
- 让测试数量、网络样本和漂移数字只有一个来源，文档不再手抄。
- 让四个变更按依赖顺序归档，主 specs 与实现一致。

**Non-Goals:**

- 不替换或修改 `/usr/local/unreal`，不引入引擎源码构建。
- 不承诺 Windows 客户端、MetaHuman、Audio2Face 或 Live Link 的打包验收。
- 不实现 P4–P8，不改造玩家传输层，不把 HTTP 变成游戏帧内传输。
- 不引入语音音频传输、混音或降噪。

## Decisions

### D1. 服务器目标采用“回退优先”的正式契约

`Build.sh` 继续支持 `TARGET=EpicodeVideoServer`。当 UBT 返回当前发行版不支持 Server 目标时，脚本以专用退出码失败，并在日志写入请求目标、引擎原始错误和 `serverMode=editor-fallback`。`Server.sh` 依次尝试：

1. `engine/Binaries/Linux/EpicodeVideoServer`
2. `UnrealEditor-Cmd -server`
3. 两者都不可用时以非零退出码退出

实际模式通过命令行参数和 `system.health.network.serverMode` 暴露，取值为 `dedicated` 或 `editor-fallback`。健康检查同时报告 `serverTargetSupported`、`fallbackReason` 和 `serverBinary`。客户端模式单独用 `clientMode=game-binary` 或 `clientMode=editor-game` 记录，不混入服务器模式。

**为什么**：用户要求引擎安装只读，当前发行版也不提供 Server 目标。继续把 1.1/1.2 挂成“未完成”会让基线永远无法归档；直接声称支持专用服务器又会伪造证据。回退优先把限制变成可验证契约。

**替代方案**：换用支持 Server 目标的引擎源码构建。被拒绝，因为会修改引擎安装、显著扩大构建面，也超出本变更范围。

### D2. Game 客户端采用“诊断后回退”，不强行掩盖崩溃

`Client.sh` 增加 `--mode auto|game|editor`，默认 `auto`。`auto` 优先使用 `engine/Binaries/Linux/EpicodeVideo`；如果没有该二进制，直接使用 `UnrealEditor -game`。如果 Game 二进制在启动窗口内以崩溃码退出或日志出现 `BufferReader` 断言，脚本记录 `clientMode=game-binary` 和崩溃日志，然后用 `UnrealEditor -game` 重试一次，并记录 `clientMode=editor-game`。

**为什么**：崩溃可能是引擎发行版或项目资产问题，不能在没有根因时承诺修复。自动回退保留未来打包客户端的路径，同时让当前双客户端冒烟可复验。

**替代方案**：直接删除 Game 目标，只保留编辑器客户端。被拒绝，因为会丢失后续打包入口，也无法区分项目问题和引擎问题。

### D3. 本地 IK 使用可选 IK Rig，权重接口保持兼容

新增 `FEpicodeLocalIKSettings`，包含手部、脚部、视线骨骼名、IK Rig 软引用、目标偏移和启用开关。`AEpicodeNetworkCharacter::ApplyLocalIKCorrections` 继续接受三个权重，但改为把权重写入本地 IK 组件；组件在本地客户端把权重转换为 IK Rig 目标，服务端和远程客户端不执行。

选择 `IKRig` 插件加 `UIK Rig` 资产，而不是手写骨骼变换覆盖。原因是 IK Rig 是引擎支持的运行时 IK 路径，能处理链长、关节限制和骨骼空间；手写 `SetBoneTransformByName` 容易和动画评估顺序冲突，也会在不同骨架上报错。

当 IK Rig 资产、骨骼或动画实例缺失时，组件只记录一次带角色和缺失项的警告，然后跳过修正。权重为零时不求解，也不重复告警。服务器权威位置、旋转和复制状态不参与 IK 计算。

**替代方案**：在 AnimBP 中固定两个 `FAnimNode_TwoBoneIK` 节点。被拒绝，因为需要为每个角色资产手工搭图，无法通过命令或配置复用，也不适合测试夹具。

### D4. Studio 远程连接使用异步 HTTP JSON-RPC 客户端

在 `EpicodeStudio` 增加 `FEpicodeControlApiClient`，使用 `FHttpModule` 异步请求 `POST /api/v1/rpc`，Bearer Token 从用户输入或 `Saved/Epicode/api-token` 读取。面板的 `Server` 输入改为控制 API 地址，默认提示 `http://127.0.0.1:8710`，不再使用游戏端口 `7777`。

客户端提供 `Connect`、`Disconnect`、`Call` 和 1 秒轮询 `session.snapshot`。所有回调切回游戏线程更新 Slate，单次界面阻塞不超过 100 ms。错误分为连接失败、401、超时、JSON-RPC 领域错误四类，分别显示，不把失败写成成功。

`Server` 为空时继续走现有 `UEpicodeEngineSubsystem::ExecuteCommand`，保留本进程会话。令牌默认掩码，日志只记录地址、方法和错误码。

控制 API 地址和游戏端口是两件事：面板的 `Server` 填写 `http://127.0.0.1:8710`，只用于 `session.*` 管理；真正的表演者或 Viewer 仍通过 `Client.sh`、`Viewer.sh` 或 Agent API 连接游戏端口 `7777`。远程面板的 `Join` 只注册控制面参与者，不代替 UE 客户端连接。

**为什么**：现有面板已经有本进程视图和权限模型，最小改动是替换命令执行后端，而不是重写面板。异步 HTTP 能复用现有控制 API 和令牌体系。

**替代方案**：在 Studio 内嵌 WebSocket 客户端。被拒绝，因为首版只需要请求/响应和 1 秒快照，HTTP 更简单；WebSocket 推送留给后续。

### D5. 证据由脚本生成，文档只消费摘要

新增 `Scripts/CollectEvidence.sh`，读取 `build-clean.log`、`test.log`、`test-clean.log`、`network-smoke.log` 和各冒烟摘要，生成 `Saved/Logs/acceptance-summary.json`。字段至少包含：

- `build.result`、`build.target`、`build.log`
- `tests.total`、`tests.passed`、`tests.failed`
- `smoke.sandbox`、`smoke.agent`、`smoke.viewer`、`smoke.replay`、`smoke.network`
- `network.sampleCount`、`network.maxRecentDrift`、`network.rttMs`、`network.packetLoss`、`network.serverMode`

`NetworkSmoke.sh` 同时写 `network-smoke-summary.json` 和人类可读摘要。`Docs/Acceptance.md` 与 `Docs/Multiplayer.md` 的数字从摘要复制，任务记录只引用摘要路径，不再手写第二套数字。

**为什么**：当前 63/162 测试数和 48/41 样本数已经漂移，靠人工同步会继续漂移。生成摘要让验收有一个可 diff 的真相源。

**替代方案**：只改文档不写脚本。被拒绝，因为下一次冒烟还会产生新的数字。

### D6. 归档顺序固定为基线、收尾、旧变更

归档顺序：

1. `add-multiplayer-production-baseline`
2. `complete-multiplayer-baseline`
3. `redesign-sandbox-director`
4. `add-agent-and-viewer-clients`

每步执行 `openspec validate --all --strict`。如果基线归档因 delta 冲突失败，先修正基线 specs，再继续。`complete-multiplayer-baseline` 的 `multiplayer-session` 和 `networked-performance` delta 只补充基线未覆盖的收尾要求，不重复定义会话和表演主链路。归档前会先把基线 `engine-foundation` 的“多人网络构建目标”改为条件式：引擎支持时使用 Server 目标，不支持时使用 `UnrealEditor-Cmd -server` 并标记 `editor-fallback`，避免主 specs 同时保留“必须构建 Server 目标”和“允许回退”两条冲突要求。

**为什么**：基线和收尾都引用同名能力，先归档基线可以让主 specs 先建立能力，再让收尾 delta 有明确目标。

**替代方案**：先归档旧的两个已完成变更。被拒绝，因为它们与多人基线没有依赖，先做也不会解决同名能力问题；固定顺序更容易复现。

### D7. 仓库卫生作为交付前置，不作为产品行为

新增 `.gitignore`，至少忽略 `engine/Binaries/`、`engine/Intermediate/`、`engine/Saved/`、`engine/DerivedDataCache/`、`engine/Plugins/*/Binaries/`、`engine/Plugins/*/Intermediate/`、`engine/Scripts/Fab/__pycache__/`。`engine/Content/` 是否忽略按项目资产策略单独决定，默认不把 181MB 未跟踪内容直接提交。

Fab 文档和 `decode_json_manifest.py` 单独提交，避免和多人代码混在一个提交里。

**为什么**：当前没有 `.gitignore`，一次 `git add -A` 会把数百 MB 构建产物纳入仓库；上一轮 Fab 源码也未提交。

**替代方案**：把构建产物也提交，保证开箱即用。被拒绝，因为仓库会迅速膨胀，且二进制与引擎版本强绑定。

## Risks / Trade-offs

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| Game 客户端崩溃根因在引擎侧 | 无法产出独立 Game 包 | `auto` 回退到编辑器客户端，文档明确 `clientMode=editor-game`，不宣称已打包 |
| IK Rig 资产或测试骨架不可用 | 无法证明实际 IK 效果 | 先提供可选 IK Rig 和合成测试夹具；缺失时 no-op 警告，验收只勾选有日志的路径 |
| HTTP 客户端在编辑器线程阻塞 | Studio 卡顿 | 全部请求异步，回调切游戏线程，100 ms 响应预算测试 |
| 令牌泄漏 | 安全风险 | 默认掩码，日志脱敏，错误消息不回显 Authorization |
| 归档顺序错误导致 delta 冲突 | spec 合并失败 | 固定顺序，每步 `validate --all --strict`，失败即停 |
| 证据摘要解析脆弱 | 数字仍可能错 | 摘要生成脚本带自检，缺少日志时输出 `not-measured` 而不是猜测 |

## Migration Plan

1. 先落 `.gitignore` 和 Fab 文档/解码器提交，清理工作区。
2. 落服务器/客户端回退与健康字段，跑构建和启动验证。
3. 落 IK 组件和测试，跑 `Epicode.Network.IK`。
4. 落 Studio HTTP 客户端和远程测试，保留本进程模式。
5. 跑全量测试和五个冒烟，生成 `acceptance-summary.json`，更新文档数字。
6. 按 D6 顺序归档四个变更，最后跑 `openspec validate --all --strict`。

回滚策略：远程面板可清空服务器地址回到本进程模式；IK 可通过权重归零和清空 IK Rig 引用禁用；服务器/客户端脚本保留 `--mode editor` 和显式二进制覆盖。

## Open Questions

无。服务器回退、客户端回退、IK 资产策略、HTTP 客户端和归档顺序都已在本设计中确定；剩余未知只在实现阶段通过日志和测试暴露。
