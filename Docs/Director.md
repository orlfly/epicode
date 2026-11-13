# 导演层

`EpicodeDirector` 在 `EpicodeSandbox` 之上提供叙事节拍、导演 cue、实时控制、角色接管、Take 和 live/ghost/resim 视角。导演不维护第二套世界：所有修改写入同一个 `FEpicodeSandboxWorld` 和事件流。

## 制作包中的叙事图

`production.json` 的 `narrative` 节点：

```json
{
  "entryBeatId": "intro",
  "variables": {
    "alarmLevel": 0,
    "hasKeycard": false
  },
  "objectives": [
    {
      "id": "steal",
      "description": "Steal the keycard",
      "condition": "hasKeycard == true"
    }
  ],
  "beats": [
    {
      "id": "intro",
      "name": "Intro",
      "entryConditions": [],
      "exitConditions": ["alarmLevel >= 1"],
      "objectives": ["steal"],
      "cues": ["cue-beat"],
      "next": ["escape"]
    },
    {
      "id": "escape",
      "name": "Escape",
      "entryConditions": ["alarmLevel >= 1"],
      "cues": ["cue-enter"],
      "next": []
    }
  ],
  "cues": [
    {
      "id": "cue-time",
      "trigger": "time",
      "time": 10.0,
      "once": true,
      "actions": ["message:Get ready", "camera:camera-main"]
    },
    {
      "id": "cue-event",
      "trigger": "event",
      "event": "sandbox.interaction",
      "once": true,
      "actions": ["setObjective:steal"]
    },
    {
      "id": "cue-beat",
      "trigger": "beat",
      "beatId": "intro",
      "once": true,
      "actions": ["message:Intro begins"]
    },
    {
      "id": "cue-condition",
      "trigger": "condition",
      "condition": "alarmLevel >= 1",
      "once": true,
      "actions": ["enterBeat:escape", "takeover:npc-guard"]
    }
  ]
}
```

### 节拍校验

`FEpicodeBeatGraph::Validate` 在会话启动前检查：

- 节拍、目标、cue 标识唯一；
- `entryBeatId` 存在；
- `next`、`objectives`、`cues` 引用存在；
- 条件引用的变量已在 `variables` 声明；
- 所有节拍从入口可达；允许有向循环，BFS 不会无限递归。

错误码：`DIRECTOR_BEAT_DUPLICATE`、`DIRECTOR_ENTRY_MISSING`、`DIRECTOR_BEAT_REFERENCE`、`DIRECTOR_BEAT_UNREACHABLE`、`DIRECTOR_OBJECTIVE_REFERENCE`、`DIRECTOR_CUE_REFERENCE`、`DIRECTOR_VARIABLE_UNDECLARED`、`DIRECTOR_CUE_BEAT_REFERENCE`。

## 叙事状态

会话级状态保存在 `FEpicodeSandboxNarrativeState`：

```json
{
  "currentBeatId": "intro",
  "currentObjectiveId": "steal",
  "variables": {"alarmLevel": 0, "hasKeycard": false},
  "objectives": {"steal": false},
  "completedEvents": [],
  "note": ""
}
```

- 变量只能通过规则、cue 或导演命令修改。
- 目标条件满足时自动完成并发布 `narrative.objectiveCompleted`。
- 进入节拍发布 `narrative.beatEntered`。
- 导演设置当前目标发布 `narrative.objectiveSet`。
- 导演消息发布 `narrative.message`。

## Cue 调度

| trigger | 触发条件 | 字段 |
| --- | --- | --- |
| `time` | 导演模拟时间到达 `time` | `time` |
| `event` | 上一个 tick 后出现指定事件 | `event` |
| `beat` | 当前节拍等于 `beatId` | `beatId` |
| `condition` | 条件从假变真 | `condition` |

`once` 默认 `true`。成功执行发布 `director.cueExecuted`（含 `cueId`、`actions`、`idempotent`）；动作失败发布 `director.cueFailed`（含 `reason`），后续 cue 继续执行。

### Cue 动作

| 动作 | 说明 |
| --- | --- |
| `camera:<cameraIdOrPresetId>` | 切换相机或相机预设。 |
| `environment:<preset>` | 切换环境预设。 |
| `spawn:<entityId>:<kind>:<archetype>` | 生成实体，`kind` 为 `prop`、`npc` 等。 |
| `remove:<entityId>` | 移除实体。 |
| `message:<text>` | 发送叙事消息。 |
| `pause` | 暂停导演时间。 |
| `slowMotion:<scale>` | 设置 0.1–4.0 的时间倍率。 |
| `takeover:<participantId>` | 临时接管参与者。 |
| `setObjective:<objectiveId>` | 设置当前目标。 |
| `enterBeat:<beatId>` | 进入节拍。 |
| `emit:<eventType>` | 保留扩展位。 |

时间 cue 使用 `DirectorTime`（模拟秒），不是墙钟。暂停期间 `DirectorTime` 不增长，cue 不触发。

## 实时导演命令

所有命令需要参与者令牌、幂等键和 owner/director 权限。重复幂等键返回首次结果（`duplicate=true`），不重复产生事件。

| C++ 方法 | 事件 | 说明 |
| --- | --- | --- |
| `Pause` / `Resume` | `director.paused` / `director.resumed` | 暂停或恢复导演时间。 |
| `SetTimeScale` | `director.timeScaleChanged` | 0.1–4.0 倍率。 |
| `SwitchCamera` | `director.cameraChanged` | 相机实体或预设。 |
| `SetEnvironment` | `director.environmentChanged` | 环境 JSON。 |
| `SpawnEntity` / `RemoveEntity` | `director.entitySpawned` / `director.entityRemoved` | 世界实体变更。 |
| `SetObjective` | `narrative.objectiveSet` | 设置当前目标。 |
| `EnterBeat` | `narrative.beatEntered` | 强制进入节拍。 |
| `Message` | `narrative.message` | 叙事消息。 |

错误：`DIRECTOR_KEY_REQUIRED`、`PARTICIPANT_NOT_FOUND`、`FORBIDDEN`、`DIRECTOR_TIMESCALE_INVALID`、`DIRECTOR_CAMERA_NOT_FOUND`、`DIRECTOR_OBJECTIVE_NOT_FOUND`、`DIRECTOR_BEAT_NOT_FOUND`、`REPLAY_READ_ONLY`。

## 角色接管

```cpp
Controller.AcquireTakeover("director-1", "take-1", "npc-guard", "perform manual beat", "manual", Tick);
Controller.ReleaseTakeover("director-1", "release-1", "npc-guard", Tick);
```

- 只能接管 `performer`、`agent`、`npc`；其他角色返回 `TAKEOVER_TARGET_INVALID`。
- 已由其他导演接管返回 `DIRECTOR_TAKEOVER_CONFLICT`，控制权不变。
- 同一导演重复请求返回 `duplicate=true`。
- 接管设置 `bTakeover=true`、`bConnected=false` 和 `ConnectedControllerId`。
- 结束时保留接管结束状态，恢复 `bConnected=true` 和原控制者，写入 `director.takeoverEnded`。
- `GetTakeoverLeases()` 提供审计记录（原控制者、原因、结束条件、开始 tick）。
- 自主 NPC 行为在 `bTakeover` 期间跳过；释放后下一 tick 恢复。

## Take

```cpp
FEpicodeDirectorTake Take;
Take.Id = "take-1";
Take.InSequence = 100;
Take.OutSequence = 220;
Take.InTick = 3000;
Take.OutTick = 6600;
Take.CameraIds = {"camera-main"};
Take.CameraPresetIds = {"close-up"};
Take.ResolutionPreset = "1080p";
Take.Note = "Rooftop reveal";

FEpicodeDirectorCommandResult Result;
Controller.CreateTake("director-1", "take-create", TakeRoot, Take, Result, Tick);
```

- `InSequence < OutSequence`，否则 `TAKE_RANGE_INVALID`。
- 相机实体必须存在且类型为 `camera`；预设必须存在，否则 `TAKE_DEPENDENCY_MISSING`。
- Take 写入 `<TakeRoot>/takes/<id>-v<version>.json`，包含区间、tick、相机、分辨率、节拍、内容摘要和校验和。
- `ReviseTake` 创建 `version + 1`，原版本文件字节不变；`TAKE_VERSION_IMMUTABLE` 防止覆盖。
- `FEpicodeTakeLibrary::LoadLatest` 供渲染器读取最新版本。

## 导演视角

```cpp
Controller.SetView("director-1", "view-ghost", EEpicodeDirectorViewMode::Ghost, SessionRoot, 120);
Controller.SetView("director-1", "view-live", EEpicodeDirectorViewMode::Live, FString(), 0);
```

- `Live` 跟随权威世界。
- `Ghost` / `Resim` 从 `<SessionRoot>` 的检查点和日志构建只读世界，live 世界不变。
- 非 live 视图的修改命令返回 `REPLAY_READ_ONLY`；`SetView` 本身允许切回 live。
- `CreateBranch` 从父会话事件序号创建独立目录，父日志保持不变；只有 owner/director 可调用。
- 分支目录包含 `branch.json`、分叉点检查点和 `startSequence = fork + 1` 的新日志。

## 事件与重演

导演事件写入同一 `FEpicodeSandboxEventStream`：

- `narrative.beatEntered` / `narrative.objectiveCompleted` / `narrative.objectiveSet`；
- `director.cueExecuted` / `director.cueFailed`；
- `director.paused` / `director.resumed` / `director.timeScaleChanged`；
- `director.cameraChanged` / `director.environmentChanged`；
- `director.entitySpawned` / `director.entityRemoved`；
- `director.takeoverStarted` / `director.takeoverEnded`；
- `director.takeCreated`。

事件 payload 的 `changes` 使用与重演相同的权威变更格式，见 `Docs/Replay.md`，因此 ghost/resim 可恢复导演修改后的世界和叙事状态。

## 验证

```bash
./Scripts/Build.sh
./Scripts/Test.sh
```

`Epicode.Director.Beats`、`NarrativeState`、`Cues`、`Control`、`Takeover`、`Take`、`Views` 覆盖节拍校验、目标完成、四类 cue、暂停/倍率、幂等、权限、接管冲突、Take 版本和 ghost 只读。文档示例与测试使用同一字段名和动作语法。
