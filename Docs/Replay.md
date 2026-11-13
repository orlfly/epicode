# 重演与事件日志

本文描述 `redesign-sandbox-director` 的事件溯源格式、检查点、ghost/resim、历史分支、保留策略和重演作业。权威世界由 `EpicodeSandbox` 持有，日志与检查点用于复盘、分支和 Take 渲染，不参与 UE Actor 投影。

## 目录布局

每个会话目录：

```
<sessionRoot>/
  replay.json                 # 日志头副本
  journal.jsonl               # 只追加事件日志
  journal.archive.jsonl       # 压缩后的旧日志（可选）
  snapshot.json               # 兼容快照（世界 + 事件）
  checkpoints/
    checkpoint-0.json         # 会话开始
    checkpoint-1000.json      # 周期检查点
  branch.json                 # 仅分支目录
```

`FPaths` 组合使用项目 `Saved/Epicode/Sessions/<sessionId>` 或调用方提供的根目录。分支使用独立目录，父目录不写入。

## journal.jsonl

每行一个 JSON 对象，`kind` 区分头、事件和结束摘要。

### 头

```json
{"kind":"header","schemaVersion":1,"sessionId":"session-1","contentDigest":"sha1:...","seed":42,"startSequence":1,"checksum":"..."}
```

- `startSequence` 在压缩后大于 1，读取时允许从该序号开始。
- `contentDigest` 是录制环境的引擎、项目、规则和内容摘要；重演默认要求一致。

### 事件

```json
{
  "kind": "event",
  "event": {
    "sequence": 1,
    "tick": 10,
    "type": "sandbox.interaction",
    "actorId": "hero",
    "targetId": "crate",
    "payload": {
      "changes": [
        {"op": "narrative.variable", "name": "alarmLevel", "value": 1},
        {"op": "entity.inventory", "id": "hero", "itemId": "keycard", "count": 1}
      ]
    }
  },
  "time": "2026-01-01T00:00:00Z",
  "worldDigest": "975FE2E6...",
  "checksum": "D8271F28..."
}
```

- `sequence` 从 `startSequence` 连续递增，重复或缺口返回 `JOURNAL_CORRUPT`。
- `tick` 是固定 30 Hz 模拟 tick，不是墙钟时间。
- `changes` 是权威变更，resim 只应用这些变更，不重新执行规则，因此不受规则实现变化影响。
- `checksum` 为事件规范 JSON、`time` 和 `worldDigest` 的 SHA1；读取时逐行校验。
- `worldDigest` 是事件应用后的世界摘要，用于目标点精确性检查。

支持的状态变更操作：

| op | 字段 | 说明 |
| --- | --- | --- |
| `narrative.variable` | `name`, `value` | 设置叙事变量。 |
| `entity.add` | `entity` | 添加完整实体。 |
| `entity.remove` | `id` | 删除实体。 |
| `entity.location` | `id`, `location` | 设置位置。 |
| `entity.rotation` | `id`, `rotation` | 设置旋转。 |
| `entity.inventory` | `id`, `itemId`, `count` | 设置库存绝对数量。 |
| `entity.visible` | `id`, `value` | 设置可见性。 |
| `entity.owner` | `id`, `value` | 设置所有者。 |
| `entity.dataString` / `entity.dataInt` | `id`, `key`, `value` | 设置实体数据字段。 |
| `entity.dataRemove` | `id`, `key` | 删除实体数据字段。 |
| `participant.takeover` | `participantId`, `value` | 设置接管状态。 |

未知操作返回 `REPLAY_EVENT_UNSUPPORTED`，不静默跳过。

### 结束摘要

```json
{"kind":"footer","lastSequence":3,"digest":"sha1(checksum1|checksum2|checksum3)"}
```

会话结束调用 `Finalize`。结束后日志保持可读，新的导演操作进入分支或新会话；对已结束日志追加返回 `JOURNAL_FINALIZED`。

## 检查点

`checkpoints/checkpoint-<sequence>.json` 包含：

- `sequence`、`tick`、`tickRate`、`seed`；
- `contentDigest`；
- 完整 `world`（实体、参与者、叙事变量、目标、未知字段）；
- `random`（每个域当前种子和随机抽取记录）；
- `checksum`。

`FEpicodeReplayCheckpoints::Save` 使用临时文件加原子移动。`LoadNearest(root, target)` 选择不晚于目标序号的最近检查点；没有检查点返回 `SNAPSHOT_MISSING`，JSON 或校验和损坏返回 `SNAPSHOT_CORRUPT`。

检查点间隔由 `EpicodeSandboxSettings::CheckpointIntervalEvents` 控制（默认 1000）。会话开始写入 `checkpoint-0`。

## Ghost 回放

`FEpicodeReplayGhost` 提供只读时间轴：

```cpp
FEpicodeReplayGhost Ghost;
Ghost.Open(SessionRoot, Error);
FEpicodeReplayGhostPage Page;
Ghost.GetPage(100, 50, Page, Error);   // 从序号 100 起最多 50 条
FEpicodeReplayResimResult State;
Ghost.Seek(120, State, Error);         // 跳转到 120 的世界
```

- Ghost 不写日志、不触发级联模拟、不影响 live 会话。
- 请求早于 `startSequence` 返回 `REPLAY_HISTORY_UNAVAILABLE`。
- 在 Ghost 模式提交修改返回 `REPLAY_READ_ONLY`（API 层强制）。
- `Seek` 加载目标前最近检查点并应用后续事件。

## 确定性 Resim

```cpp
FEpicodeReplayResimOptions Options;
Options.ExpectedContentDigest = BuildContentDigest;
FEpicodeReplayResimResult Result;
FEpicodeReplayResim::Run(SessionRoot, TargetSequence, Options, Result, Error);
```

步骤：

1. 打开并校验日志。
2. 比较日志头与检查点的 `contentDigest`；不一致默认返回 `REPLAY_VERSION_MISMATCH`，显式 `bAllowInexact=true` 时继续并标记 `inexact`。
3. 加载不晚于目标序号的最近检查点。
4. 按序应用 `sequence > checkpoint.sequence && <= target` 的事件变更。
5. 计算世界摘要，与目标事件或目标检查点记录的摘要比较。

输出：

- `digest`：目标世界摘要；
- `exact`：版本一致且摘要一致；
- `inexact`：允许非精确重演；
- `appliedEvents`：从检查点后应用的事件数；
- `diffs`：首个差异，包含 `sequence`、`entityId`、`field`、`expected`、`actual`。

摘要不一致返回 `REPLAY_DIVERGED`，不覆盖原日志。`FEpicodeReplayResimRunner` 支持分步执行和 `Cancel()`；取消返回 `REPLAY_CANCELLED`，不修改父会话。

## 历史分支

```cpp
FEpicodeReplayBranch::Create(
    ParentRoot,
    BranchRoot,
    BranchId,
    ForkSequence,
    CreatedBy,
    EEpicodeSandboxParticipantKind::Director,
    Error);
```

- 仅 owner/director 可创建；其他角色返回 `FORBIDDEN`。
- 分叉点不存在、早于保留窗口或重演失败返回 `BRANCH_POINT_UNAVAILABLE`，不创建目录。
- 分支目录包含 `branch.json`、初始 `snapshot.json`、分叉点检查点和 `startSequence = fork + 1` 的新日志。
- 分支事件只写入分支目录；父日志字节和摘要不变。
- `branch.json` 记录 `branchId`、`parentSessionId`、`forkSequence`、`createdBy`、`contentDigest` 和校验和。

## 保留与压缩

`FEpicodeReplayRetention::Compact(root, keepFrom, actorKind, error)`：

1. 找到不晚于 `keepFrom` 的最近检查点边界。
2. 把原日志移动到 `journal.archive.jsonl`。
3. 重写 `journal.jsonl`，头 `startSequence` 设为边界序号 + 1，只保留边界后的事件，重算结束摘要。
4. 删除边界之前的检查点，保留边界检查点。

压缩后请求更早目标返回 `REPLAY_HISTORY_UNAVAILABLE`；边界检查点仍可直接重演。只有 owner/director 可压缩，其他角色返回 `FORBIDDEN`。

`ApplyPolicy` 按 `MaxEvents` 和 `RetentionHours` 自动压缩；`MaxEvents` 保留最近 N 条事件，`RetentionHours` 超过窗口时压缩到最新检查点。

## 重演作业

`FEpicodeReplayJobQueue` 提供协作式调度，不阻塞 live tick：

```cpp
FEpicodeReplayJobQueue Queue;
Queue.Initialize(1);                    // 最大并发 1
const FString JobId = Queue.Submit(Root, 120, Options, "director-1", EEpicodeSandboxParticipantKind::Director, Error);
Queue.StartQueued(Error);
Queue.Step(JobId, 100, Error);          // 每次最多 100 个事件
Queue.TickLive(0.033);                  // live 继续推进
Queue.Cancel(JobId, Error);
```

状态：`queued`、`running`、`completed`、`failed`、`cancelled`。`StartQueued` 遵守 `MaxConcurrent`；`RunToCompletion` 仅用于测试或离线工具。作业状态可通过 `ToJson()` 暴露给 `replay.status`。

## 错误码

| 错误 | 含义 |
| --- | --- |
| `JOURNAL_CORRUPT` | JSON、校验和、序号或结束摘要不一致。 |
| `JOURNAL_EXISTS` | 目标目录已有日志。 |
| `JOURNAL_FINALIZED` | 已结束日志拒绝追加。 |
| `JOURNAL_SEQUENCE` | 事件序号不连续。 |
| `JOURNAL_WRITE_FAILED` | 追加失败。 |
| `SNAPSHOT_MISSING` | 缺少可用检查点。 |
| `SNAPSHOT_CORRUPT` | 检查点 JSON 或校验和损坏。 |
| `SNAPSHOT_SAVE_FAILED` | 检查点写入失败。 |
| `REPLAY_HISTORY_UNAVAILABLE` | 目标早于保留窗口。 |
| `REPLAY_TARGET_UNAVAILABLE` | 目标超过日志末尾。 |
| `REPLAY_VERSION_MISMATCH` | 内容摘要不一致且未允许非精确。 |
| `REPLAY_DIVERGED` | 目标摘要与录制不一致。 |
| `REPLAY_CANCELLED` | 重演被取消。 |
| `REPLAY_EVENT_INVALID` / `REPLAY_EVENT_UNSUPPORTED` | 事件变更格式错误或未知操作。 |
| `BRANCH_POINT_UNAVAILABLE` | 分叉点无效或重演失败。 |
| `BRANCH_EXISTS` | 分支目录已存在。 |
| `BRANCH_CREATE_FAILED` | 分支写入或发布失败。 |
| `BRANCH_CORRUPT` | 分支元数据损坏。 |
| `REPLAY_JOB_NOT_FOUND` / `REPLAY_JOB_TERMINAL` | 作业不存在或已结束。 |
| `FORBIDDEN` | 非 owner/director 执行重演、分支或压缩。 |

## 确定性约束

- 事件序号、tick、变更数组和随机记录均显式保存。
- 实体遍历使用稳定 ID 排序；检查点和摘要不依赖哈希容器迭代顺序。
- 随机流按域派生并保存当前种子；恢复后下一次抽取与录制一致。
- 不允许在权威逻辑中使用墙钟、帧率或未记录的随机源。
- 渲染线程、Actor 投影和本地表现不改变日志或世界摘要。

## 验证

```bash
./Scripts/Build.sh
./Scripts/Test.sh
```

`Epicode.Replay.Journal`、`Snapshot`、`Ghost`、`Resim`、`Branch`、`Retention`、`Job` 覆盖追加、损坏、检查点恢复、跳转、精确/非精确重演、差异、取消、分支隔离、压缩和并发限制。日志字段与 `Saved/Logs/test.log` 中的测试输出一致。
