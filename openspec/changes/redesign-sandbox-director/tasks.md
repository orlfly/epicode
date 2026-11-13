# Tasks

## 1. 模块、配置与确定性基础

- [x] 1.1 新增 `EpicodeSandbox` 运行时插件与模块声明，加入项目构建和依赖；验证：`Scripts/Build.sh` 成功，启动日志包含 `EpicodeSandbox` 版本。
- [x] 1.2 新增 `EpicodeDirector` 运行时插件与模块声明，加入项目构建和依赖；验证：`Scripts/Build.sh` 成功，启动日志包含 `EpicodeDirector` 版本。
- [x] 1.3 增加固定 tick、随机种子、检查点间隔、日志保留、实体/事件预算和功能开关配置；验证：`Epicode.Foundation.DeterminismConfig` 覆盖默认值、非法值和保留窗口约束。
- [x] 1.4 实现内容目录摘要与构建摘要服务，汇总引擎、项目、规则 schema 和内容哈希；验证：`Epicode.Foundation.ContentDigest` 覆盖稳定摘要、内容变化和目录不可读。
- [x] 1.5 扩展进程角色和 `system.health`，报告 Sandbox/Director/Replay 模块、日志可写性、重演队列和预算；验证：`Epicode.Foundation.ProcessRoles` 增加 Director/Replay，健康检查字段完整。
- [x] 1.6 更新 `Docs/Architecture.md` 与 `Docs/Operations.md` 的模块边界、确定性配置、摘要和进程角色；验证：文档中的模块名和配置键与启动日志、健康检查一致。

## 2. 制作包与场景绑定

- [x] 2.1 定义 `production.json` schema v1 的 C++ 数据模型、JSON 序列化、未知字段保留和 `schemaVersion`；验证：`Epicode.Production.Document` 覆盖往返、默认值、未知字段和版本拒绝。
- [x] 2.2 实现制作包库的创建、打开、保存、另存为、列出、删除和回收目录；验证：`Epicode.Production.Library` 覆盖生命周期、当前包删除保护和回收路径。
- [x] 2.3 实现制作包到场景的绑定校验，包括场景存在、版本、实体类型、演员槽位和重复引用；验证：`Epicode.Production.Validation` 覆盖每个错误码和原子失败。
- [x] 2.4 实现仅场景旧工作流的默认制作包视图，不写盘且保持现有会话可用；验证：`Epicode.Production.Legacy` 断言旧会话字段、默认规则和空叙事图。
- [x] 2.5 把制作包修改接入现有事务/撤销机制，失败回滚内存和磁盘；验证：`Epicode.Production.Undo` 覆盖批量修改、撤销和重做。
- [x] 2.6 新增 `Docs/ProductionPackage.md`，说明字段、版本、校验、存储和示例；验证：文档示例可被 `production.validate` 和制作包库加载。

## 3. 确定性沙盒核心

- [x] 3.1 实现 `FEpicodeSandboxWorld` 的实体、参与者、库存、叙事变量和世界状态容器，使用稳定标识；验证：`Epicode.Sandbox.World` 覆盖创建、查询、序列化和未知字段。
- [x] 3.2 实现固定 tick 调度器和命令队列，区分输入、规则、投影和系统事件；验证：`Epicode.Sandbox.Tick` 覆盖 30 Hz 推进、积压输入和 tick 单调性。
- [x] 3.3 实现按域拆分的确定性随机流和稳定排序，记录随机抽取；验证：`Epicode.Sandbox.Determinism` 覆盖相同种子、不同域隔离和哈希容器顺序。
- [x] 3.4 定义沙盒领域事件、事件序号和状态摘要算法；验证：`Epicode.Sandbox.Digest` 覆盖相同状态相同摘要、单字段变化和事件顺序。
- [x] 3.5 实现投影指令输出，把角色、NPC、道具、物品和建造物投影请求与权威状态分离；验证：`Epicode.Sandbox.ProjectionBoundary` 断言核心不依赖 Actor/Slate。
- [x] 3.6 实现世界创建、装载、重置和关闭生命周期，校验制作包与规则；验证：`Epicode.Sandbox.Lifecycle` 覆盖成功、校验失败和重复关闭。
- [x] 3.7 新增 `Docs/SandboxWorld.md`，说明状态、tick、随机流、事件和投影边界；验证：文档术语与 `Epicode.Sandbox.*` 输出字段一致。

## 4. 沙盒规则与玩法

- [x] 4.1 实现交互点、动词、前置条件、距离/朝向/冷却校验和结果应用；验证：`Epicode.Sandbox.Interactions` 覆盖成功、超距、未知动词和条件失败。
- [x] 4.2 实现区域、生成器、生成表、预算、冷却和重生；验证：`Epicode.Sandbox.Spawners` 覆盖预算内生成、上限、稳定种子和重生。
- [x] 4.3 实现 NPC 行为档案、感知、目标选择、巡逻和交互行为；验证：`Epicode.Sandbox.NPC` 覆盖自主行为、目标丢失和规则降级。
- [x] 4.4 实现资源节点、采集、库存、消耗、制作和产出，保证并发采集不产生负库存；验证：`Epicode.Sandbox.Resources` 覆盖采集、材料不足和并发。
- [x] 4.5 实现沙盒快照持久化、重连恢复、服务器重启恢复和损坏拒绝；验证：`Epicode.Sandbox.Persistence` 覆盖重连、重启和损坏快照。
- [x] 4.6 实现 owner/director/performer/agent/viewer 的沙盒权限和接管优先级；验证：`Epicode.Sandbox.Permissions` 覆盖越权、Viewer 只读和导演管理限制。
- [x] 4.7 实现运行预算、事件限流、内容路径白名单和降级报告；验证：`Epicode.Sandbox.Budget` 覆盖生成率越界、路径逃逸和预算耗尽。
- [x] 4.8 更新 `Docs/SandboxWorld.md` 的规则示例、错误码和权限矩阵；验证：示例制作包通过校验并能启动冒烟会话。

## 5. 事件日志、检查点与重演

- [x] 5.1 实现只追加 `journal.jsonl`，包含序号、tick、类型、发起者、载荷和校验和；验证：`Epicode.Replay.Journal` 覆盖追加、序号连续、校验失败和结束摘要。
- [x] 5.2 实现初始快照和周期检查点，包含世界、叙事、参与者、种子和内容摘要；验证：`Epicode.Replay.Snapshot` 覆盖加载、检查点间隔和缺失快照。
- [x] 5.3 实现 ghost 历史视图、分页事件和跳转；验证：`Epicode.Replay.Ghost` 覆盖跳转、只读拒绝和历史缺口。
- [x] 5.4 实现确定性 resim 工作器、状态摘要比较和差异报告；验证：`Epicode.Replay.Resim` 覆盖精确重演、差异、取消和版本不匹配。
- [x] 5.5 实现历史分支目录、父会话引用和隔离写入；验证：`Epicode.Replay.Branch` 覆盖创建、分支写入、无效分叉点和父会话不变。
- [x] 5.6 实现日志保留、检查点边界压缩、权限和 `REPLAY_HISTORY_UNAVAILABLE`；验证：`Epicode.Replay.Retention` 覆盖压缩、保留窗口和越权。
- [x] 5.7 实现重演作业调度、状态、取消和并发限制；验证：`Epicode.Replay.Job` 覆盖排队、运行、取消和 live 会话不受阻塞。
- [x] 5.8 新增 `Docs/Replay.md`，说明日志格式、检查点、ghost/resim、分支和错误；验证：`Scripts/ReplaySmoke.sh` 生成的日志字段与文档一致。

## 6. 叙事导演层与 Take

- [x] 6.1 实现节拍图模型、入口可达性、循环和引用校验；验证：`Epicode.Director.Beats` 覆盖进入节拍、条件不满足和无效图。
- [x] 6.2 实现叙事变量、条件表达式、目标和完成事件；验证：`Epicode.Director.NarrativeState` 覆盖变量、目标完成和未声明变量。
- [x] 6.3 实现时间/事件/节拍/条件 cue 调度、动作执行和幂等；验证：`Epicode.Director.Cues` 覆盖时间 cue、条件 cue 和动作失败。
- [x] 6.4 实现导演暂停、倍率、镜头、环境、生成/移除、目标和节拍命令；验证：`Epicode.Director.Control` 覆盖命令、幂等和事件序号。
- [x] 6.5 实现角色接管 lease、冲突、结束恢复和审计；验证：`Epicode.Director.Takeover` 覆盖 NPC、冲突和恢复自主。
- [x] 6.6 实现 Take 入出点、镜头轨道、版本不可变和依赖引用；验证：`Epicode.Director.Take` 覆盖创建、空区间、新版本和缺失依赖。
- [x] 6.7 实现 live/ghost/resim 视角切换和导演分支入口；验证：`Epicode.Director.Views` 覆盖 ghost 只读、分支创建和 live 隔离。
- [x] 6.8 新增 `Docs/Director.md`，说明节拍、cue、接管、视角、Take 和错误；验证：文档命令可创建节拍、cue 和 Take。

## 7. 网络会话与 UE 投影集成

- [x] 7.1 在 `EpicodeNetwork` 会话中桥接 `EpicodeSandbox` 世界，创建/装载/关闭时同步生命周期；验证：`Epicode.Network.SandboxIntegration` 覆盖会话创建、结束和重复关闭。
- [x] 7.2 把演员阵容绑定到参与者、角色和 Agent/Viewer 权限；验证：`Epicode.Network.CastBinding` 覆盖绑定、槽位冲突和重连恢复。
- [x] 7.3 实现角色、NPC、道具、物品和建造物的 UE 投影与清理；验证：`Epicode.Network.Projection` 覆盖生成、更新、销毁和复制标记。
- [x] 7.4 复制沙盒状态摘要与事件，保持现有角色移动和表演复制兼容；验证：`Epicode.Network.SandboxReplication` 覆盖初次复制、增量事件和 Viewer 只读。
- [x] 7.5 实现旧会话适配层和功能开关回退，旧事件标记不可精确重演；验证：`Epicode.Network.LegacySession` 覆盖旧会话、关闭开关和字段降级。
- [x] 7.6 更新 `Docs/Multiplayer.md` 与 `Docs/Architecture.md` 的沙盒会话、投影和复制边界；验证：`Scripts/NetworkSmoke.sh` 继续通过且文档字段与快照一致。

## 8. 控制 API 与事件流

- [x] 8.1 注册 `production.*` 方法并复用参与者令牌、幂等和错误模型；验证：`Epicode.API.Production` 覆盖创建、打开、保存、校验、删除和越权。
- [x] 8.2 注册 `sandbox.*` 方法，覆盖规则查询/更新、交互、生成、资源、预算和状态；验证：`Epicode.API.Sandbox` 覆盖参数校验、成功和领域错误。
- [x] 8.3 注册 `director.*` 方法，覆盖暂停、倍率、cue、接管、目标和节拍；验证：`Epicode.API.Director` 覆盖 owner/director 允许和 performer/view 拒绝。
- [x] 8.4 注册 `replay.*` 与 `take.*` 方法，支持作业、取消、ghost、分支和 Take 版本；验证：`Epicode.API.Replay` 覆盖精确重演、差异、取消和 Take 依赖错误。
- [x] 8.5 扩展 WebSocket 事件类别、参与者/节拍/实体过滤、`since` 补发和 `events.gap`；验证：`Epicode.API.Events` 覆盖过滤、分页、缺口和慢客户端。
- [x] 8.6 扩展 `system.methods`、`system.health` 和会话快照的重演/内容摘要/权限元数据；验证：`Epicode.API.Methods` 覆盖目录、健康字段和 `replayable` 标记。
- [x] 8.7 更新 `Docs/ControlAPI.md` 与 `Docs/Usage.md` 的新方法、事件、错误和 curl 示例；验证：示例逐个执行并返回文档所述字段。

## 9. Studio 沙盒面板与导演工作区

- [x] 9.1 新增沙盒世界面板，显示制作包、规则、预算和校验结果；验证：`Epicode.Studio.SandboxPanel` 覆盖列表、空状态、错误和保存。
- [x] 9.2 新增导演工作区页签、时间轴和事件过滤；验证：`Epicode.Studio.DirectorWorkspace` 覆盖页签、布局、缩放和播放头。
- [x] 9.3 新增节拍图、当前目标、叙事状态和 cue 面板；验证：`Epicode.Studio.BeatGraph` 覆盖当前节拍、空图和条件显示。
- [x] 9.4 新增实时导演控制、角色接管和权限禁用；验证：`Epicode.Studio.DirectorControls` 覆盖暂停、接管、冲突和只读。
- [x] 9.5 新增 ghost/resim 传输、分支创建、Take 入出点和版本渲染入口；验证：`Epicode.Studio.ReplayControls` 覆盖模式切换、分支和 Take 版本。
- [x] 9.6 验证长会话时间轴、ghost 预览和重演进度不阻塞 UI；验证：`Epicode.Studio.DirectorResponsiveness` 覆盖 100 ms 预算、错误回退和 live 继续。
- [x] 9.7 更新 `Docs/Studio.md` 与 `Docs/StudioAcceptance.md` 的导演/沙盒面板操作和限制；验证：文档入口与实际菜单、页签和错误提示一致。

## 10. Take 渲染

- [x] 10.1 实现 Take 渲染提交、版本校验、依赖检查和输出目录校验；验证：`Epicode.Render.Take` 覆盖成功、Take 不存在和目录不可写。
- [x] 10.2 实现 Take 确定性快照和内容摘要，拒绝不一致渲染；验证：`Epicode.Render.TakeDeterminism` 覆盖 live 隔离、摘要不匹配和元数据。
- [x] 10.3 实现多镜头轨道、切换事件和输出命名；验证：`Epicode.Render.TakeTracks` 覆盖多轨道、轨道重叠和相机缺失。
- [x] 10.4 实现进度、取消、重试和产物验证，登记到 Take 清单；验证：`Epicode.Render.TakeLifecycle` 覆盖取消、失败重试、帧数不足和重复渲染。
- [x] 10.5 更新 `Docs/Rendering.md` 的 Take 提交、轨道、命名和确定性说明；验证：文档命令生成可验证的 Take 渲染作业。

## 11. 脚本、验收与回归

- [x] 11.1 新增 `Scripts/Director.sh`，支持服务器、制作包、会话和额外 Unreal 参数；验证：非法参数非零退出，合法启动日志包含 Director 角色和制作包标识。
- [x] 11.2 新增 `Scripts/Replay.sh` 与 `Scripts/ReplaySmoke.sh`，覆盖创建、事件、重演、分支和清理；验证：脚本返回 0 并写出 `Saved/Logs/replay-smoke.log`。
- [x] 11.3 新增 `Scripts/SandboxSmoke.sh`，覆盖制作包、交互、生成、NPC、资源和导演 cue；验证：脚本输出 `Epicode sandbox smoke passed` 和事件摘要。
- [x] 11.4 扩展 `Scripts/AgentSmoke.sh`/`ViewerSmoke.sh`/`NetworkSmoke.sh` 的沙盒回归，确保旧路径不受新模块影响；验证：三个冒烟均返回 0。
- [x] 11.5 更新 `Docs/Operations.md`、`Docs/Usage.md`、`Docs/Acceptance.md` 的启动、角色、日志、确定性限制和验收证据；验证：文档路径、命令和结果与最终日志一致。
- [x] 11.6 执行 `Scripts/Clean.sh` 后的干净构建、全量测试、确定性重演、Take 渲染和三个客户端冒烟；验证：构建成功，首次测试全部通过，脚本返回 0。
- [x] 11.7 运行 `openspec validate redesign-sandbox-director --strict`，检查 proposal、八个 spec delta、design 和 tasks；验证：命令通过，无缺失场景或格式错误。

## Workflow follow-up

- 经用户评审后归档；按依赖顺序先处理 `add-multiplayer-production-baseline` 和 `add-agent-and-viewer-clients`，再归档本变更。
- 归档后确认主 spec 已包含 `sandbox-world`、`narrative-direction`、`session-replay`，并更新 `scene-authoring`、`control-api`、`production-ui`、`video-rendering`、`engine-foundation` 的长期要求。
