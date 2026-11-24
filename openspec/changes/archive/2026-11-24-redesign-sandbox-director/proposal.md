# Proposal

## Why

当前 Epicode 的主链路是“作者场景 → 会话装载 → 线性表演/渲染”：场景 JSON 描述布景，运行时会话只覆盖有限角色、表演、物品和建造，导演侧只有入口列表，没有可回看、可分支、可重演的叙事时间轴。这无法支撑高自由度互动沙盒，也无法让导演在保留演员自主性的同时重排剧情并稳定产出镜头。

如果继续把沙盒规则、导演操作和重演状态塞进现有 `scene.json` 或会话快照，作者意图、运行时结果和回放数据会互相污染。需要在保留现有场景、导入和渲染能力的前提下，把产品重新组织为“制作包 + 权威沙盒运行时 + 事件溯源时间轴 + 导演工作区”。

## What Changes

- 新增制作包（Production Package）：在场景之上增加 `production.json`，绑定场景、演员阵容、世界规则、叙事节拍、导演提示和镜头预设；现有 `scene.json` 继续只描述布景与静态实体。
- 新增高自由度沙盒运行时：可配置的交互点、可操作物、区域、生成器、NPC/Agent 行为、资源循环、状态持久化和重生规则；玩家、Agent、Viewer 和 Director 共用同一权威世界。
- 新增确定性事件日志：固定 tick、随机种子、内容/引擎版本摘要和有序命令事件；会话可从初始快照重演到任意事件序号。
- 新增叙事导演层：节拍图（beat graph）、目标/条件、导演 cue、暂停/慢放、角色接管、生成/移除、环境与镜头调度、Take 标记和分支点。
- 新增重演与分支：live、ghost 回放、确定重演、从历史点分叉为新分支；重演不修改原始会话，分支写入独立会话目录。
- 新增 Take 与渲染：把时间轴片段、相机轨道和导演 cue 固化为 Take，交给现有 Movie Render Pipeline 输出；渲染使用 Take 的确定性快照，而不是实时世界。
- 新增 `EpicodeSandbox` 与 `EpicodeDirector` 运行时模块；`EpicodeNetwork` 继续承担会话与复制，`EpicodeStudio` 增加导演工作区，`EpicodeControlAPI` 增加制作、沙盒、导演和重演方法。
- 保留现有场景作者化、导入、控制 API、多人会话和视频渲染语义；仅使用 `scene.json` 的旧场景以默认沙盒规则和空叙事图运行，不要求迁移。
- **BREAKING**：不删除现有命令或场景字段，但“会话”默认语义从一次性表演会话扩展为可从事件日志重演的沙盒会话；依赖“会话结束后事件不可重放”的客户端需要改用显式 `session.archive`。

## Capabilities

### New Capabilities

- `sandbox-world`: 高自由度运行时世界，包括制作包装载、交互点/可操作物、区域与生成器、NPC/Agent 行为、资源循环、状态持久化、重生和参与者权限。
- `narrative-direction`: 叙事导演层，包括节拍图、目标与条件、导演 cue、实时干预、角色接管、暂停/慢放、Take 标记和分支点。
- `session-replay`: 确定性会话录制与重演，包括事件日志、初始快照、种子与版本摘要、ghost 回放、重演、分叉、差异检测和 Take 固化的时间轴语义。

### Modified Capabilities

- `scene-authoring`: 增加制作包与场景的绑定、沙盒规则和叙事元数据的作者化与校验，同时保持现有场景文档兼容。
- `control-api`: 增加 `production.*`、`sandbox.*`、`director.*`、`replay.*` 和 `take.*` 方法、导演权限、事件过滤和重演/分支错误语义。
- `production-ui`: 增加导演工作区、时间轴/节拍图、沙盒规则面板、Live/Director 模式切换、分支与 Take 操作，并保持与 API 命令一致。
- `video-rendering`: 增加从 Take/时间轴片段提交渲染、相机轨道绑定、确定性快照和重演产物验证。
- `engine-foundation`: 增加沙盒与导演模块、确定性运行时配置、重演/导演脚本和进程角色边界。

## Impact

- **新增代码**：`EpicodeSandbox`（制作包、世界规则、交互点、生成器、NPC/Agent 行为）、`EpicodeDirector`（节拍图、导演 cue、时间轴、重演、分支、Take）；`EpicodeNetwork` 增加事件溯源会话和确定性录制；`EpicodeStudio` 增加导演工作区；`EpicodeControlAPI` 增加新方法与事件。
- **修改代码**：`EpicodeCore` 增加制作包文档、事件日志和内容摘要；`EpicodeRender` 增加 Take 到渲染作业的适配；`EpicodeNetworkSubsystem` 把会话状态迁移为可重演事件流。
- **数据**：新增 `Saved/Epicode/Productions/<productionId>/production.json`、`Saved/Epicode/Sessions/<sessionId>/journal.jsonl`、`snapshots/`、`branches/`、`takes/`；现有 `scene.json`、导入索引和渲染输出保持不变。
- **API**：新增制作包生命周期、沙盒规则、导演干预、重演控制、分支和 Take 方法；现有 `session.*`、`scene.*`、`render.*` 保持兼容。
- **平台与确定性**：Linux 无头服务器为权威模拟基线；重演要求固定 tick、显式随机种子和内容摘要；跨 GPU 的像素结果仍不要求一致。
- **兼容性**：旧场景可直接创建默认沙盒会话；旧客户端不使用新方法时行为不变；导演/重演是可选能力，不阻塞现有表演者、Agent 和 Viewer。
- **非目标**：不实现公网多租户、UGC 市场、像素流/NDI/Spout、完整 MMO 级 Replication Graph、完整 MetaHuman/IK 生产链、Windows 客户端和已有独立服务器构建限制的修复。
- **风险**：确定性重演对浮点、随机数和 Actor 迭代顺序敏感；导演实时干预与演员自主性可能冲突；事件日志增长和回放性能需要预算；Take 渲染需要与现有 MRQ 工作进程隔离，避免影响实时会话。
