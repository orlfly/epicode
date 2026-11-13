# 沙盒世界（Sandbox World）

沙盒是世界真相源。`scene.json` 提供静态布景，`production.json` 提供演员、规则和叙事，`FEpicodeSandboxWorld` 保存运行时状态。UE Actor 只是投影，不是权威状态。

## 状态模型

`FEpicodeSandboxWorld`：

| 字段 | 说明 |
| --- | --- |
| `worldId` | 世界实例稳定标识。 |
| `sessionId` | 会话标识；重启恢复和重演按会话目录组织。 |
| `productionId` / `sceneId` | 来源制作包和场景。 |
| `tick` | 权威模拟 tick，只在固定 tick 调度器中前进。 |
| `seed` | 会话随机种子；0 表示由会话标识派生并记录。 |
| `entities` | 实体表，键为稳定实体标识；序列化按标识排序。 |
| `participants` | 参与者表，键为演员槽位。 |
| `narrative` | 当前节拍、目标、变量、完成事件和导演备注。 |

### 实体

`FEpicodeSandboxEntity` 包含 `id`、`kind`、`archetypeId`、位置/朝向、可见性、所有者、库存和扩展 `data`。实体 `kind`：

`character`、`npc`、`prop`、`item`、`build`、`resource`、`trigger`、`light`、`camera`。

库存以物品稳定标识为键，数量不允许为负；`AddItem` 在结果为负时返回 `INVENTORY_NEGATIVE`，不修改原库存。

### 参与者

`FEpicodeSandboxParticipant` 包含槽位 `id`、`kind`、绑定的角色实体、连接状态、接管标记和控制器标识。`kind` 为 `performer`、`agent`、`viewer`、`director` 或 `npc`。

### 叙事状态

`FEpicodeSandboxNarrativeState` 保存 `currentBeatId`、`currentObjectiveId`、`note`、变量、目标完成状态和已完成事件。变量值支持布尔、整数、浮点和字符串；序列化和状态摘要都按变量名排序。

## 固定 tick

`FEpicodeSandboxSimulationClock` 默认 30 Hz：

- 累积真实时间 `Accumulator`，每满 `1/TickRate` 推进一步。
- 单次 `Advance` 有最大 tick 数，避免卡顿后一次补太多帧。
- `tick` 单调递增，不因输入积压回退。
- 队列命令按 `tick`、类别、入队序号稳定排序。

类别顺序固定为：`input`、`rule`、`projection`、`system`。输入先于规则，规则先于投影，系统事件最后。

## 确定性随机流

`FEpicodeSandboxRandomStreams` 按域拆分随机流：

- 每个域使用 `baseSeed ^ crc32(lower(domain))` 派生独立 `FRandomStream`。
- `loot`、`npc`、`spawner` 等域互不消耗对方序列。
- 同一制作包、种子和调用顺序产生相同结果。
- 每次抽取记录 `tick`、`domain`、`kind` 和 `value`，可通过 `GetDrawsJson()` 写入诊断或日志。

不要用全局 `FMath::Rand` 或时间戳作为模拟输入；时间、随机和输入都必须进入事件日志。

## 事件与摘要

`FEpicodeSandboxEvent` 字段：

| 字段 | 说明 |
| --- | --- |
| `sequence` | 从 1 开始连续递增；缺口拒绝加载。 |
| `tick` | 事件发生的权威 tick。 |
| `type` | 领域事件类型，例如 `sandbox.loaded`、`sandbox.started`。 |
| `actorId` / `targetId` | 发起者和目标。 |
| `payload` | 事件载荷对象。 |

`FEpicodeSandboxEventStream` 只追加，校验序号连续。`FEpicodeSandboxDigest` 提供：

- `ComputeWorldDigest`：对世界规范化字符串做 SHA1。
- `ComputeEventDigest`：按事件顺序拼接规范字符串后做 SHA1。
- `ComputeCombinedDigest`：世界摘要和事件摘要组合。

规范化字符串对实体、参与者、库存、变量和目标键排序，避免容器迭代顺序影响摘要。

## 投影边界

`FEpicodeSandboxProjection::Build` 从权威世界生成 `upsert` 指令，只包含可见且可投影实体。`trigger` 不投影。

投影层负责：

- 生成/更新/销毁 UE Actor、道具、建造物和 Viewer 表现。
- 网络复制和相机跟随。
- 不向核心写回位置或状态。

核心头文件和实现不得包含 `GameFramework/Actor.h`、`AActor`、`Widgets/` 或 Slate 控件；`Epicode.Sandbox.ProjectionBoundary` 会自动检查这条边界。

## 规则与玩法

`FEpicodeSandboxRules::Parse` 把 `production.json` 的 `worldRules` 解析为强类型规则，并在 `Validate` 中检查标识唯一、引用存在、预算、生成率和内容路径。

### 交互点

```json
{
  "interactionPoints": [
    {
      "id": "door-control",
      "entityId": "33333333-3333-4333-8333-333333333333",
      "verbs": ["use"],
      "maxDistance": 180,
      "cooldownSeconds": 2,
      "conditions": ["hasKeycard == true", "alarmLevel < 3"],
      "outcomes": ["setVariable:alarmLevel=1", "consumeItem:keycard=1", "setEntityVisible:door=false"]
    }
  ]
}
```

条件支持 `true`/`false`、裸变量真值、`==`、`!=`、`>`、`>=`、`<`、`<=`，变量必须已在 `narrative.variables` 声明。结果动作：

| 动作 | 含义 |
| --- | --- |
| `setVariable:<name>=<value>` | 设置叙事变量。 |
| `addItem:<itemId>=<count>` | 增加角色库存。 |
| `consumeItem:<itemId>=<count>` | 消耗角色库存，不足时失败。 |
| `setEntityVisible:<entityId>=<bool>` | 修改实体可见性。 |
| `setOwner:<entityId>=<ownerId>` | 修改实体所有者。 |
| `emit:<eventType>` | 保留动作，由导演层扩展。 |

交互按参与者权限、距离、朝向/冷却、条件和结果顺序校验；结果应用失败会回滚世界副本。

### 区域、生成器与生成表

```json
{
  "zones": [{"id": "roof", "name": "Roof", "maxEntities": 5, "center": [0, 0, 0], "extent": [100, 100, 10]}],
  "spawnTables": [{"id": "loot", "entries": [{"entityType": "Prop", "archetypeId": "LootCrate", "weight": 1}]}],
  "spawners": [{"id": "loot-spawner", "zoneId": "roof", "spawnTableId": "loot", "intervalSeconds": 1, "maxAlive": 1}]
}
```

生成使用 `spawner:<id>` 随机域；相同种子、制作包和触发 tick 生成相同实体。全局实体预算、区域上限和 `maxAlive` 任一耗尽时写入 `sandbox.spawnBudgetExceeded`，不静默超限。

### NPC

```json
{
  "npcProfiles": [
    {
      "id": "guard",
      "behavior": "patrol",
      "perceptionRadius": 300,
      "speed": 100,
      "targetTag": "hero",
      "patrolPoints": [{"location": [0, 0, 0]}, {"location": [200, 0, 0]}]
    }
  ]
}
```

NPC 实体通过 `data.npcProfileId` 选择档案。感知半径内选择最近目标并追踪；目标丢失写入 `sandbox.npcTargetLost`，随后按巡逻点继续；没有档案时使用 idle 降级，不移动。

### 资源与制作

```json
{
  "resources": [{"id": "metal-node", "entityId": "node-1", "itemId": "metal", "capacity": 2, "respawnSeconds": 30}],
  "recipes": [{"id": "make-key", "inputs": {"metal": 2}, "outputs": {"key": 1}}]
}
```

采集在服务器串行扣减节点余量并写入角色库存；余量不足返回 `RESOURCE_DEPLETED`，不产生负库存。制作先校验全部输入，再原子扣减和产出；材料不足返回 `INSUFFICIENT_RESOURCES`。资源耗尽后按 `respawnSeconds` 重生并写入 `sandbox.resourceRespawned`。

## 权限矩阵

| 动作 | owner | director | performer | agent | npc | viewer |
| --- | --- | --- | --- | --- | --- | --- |
| 观察/读取 | 是 | 是 | 是 | 是 | 是 | 是 |
| 交互/采集/制作 | 是 | 是 | 是 | 是 | 是 | 否 |
| 生成/导演/接管 | 是 | 是 | 否 | 否 | 否 | 否 |
| 所有权/成员管理 | 是 | 否 | 否 | 否 | 否 | 否 |

Viewer 提交修改返回 `VIEWER_READ_ONLY`；导演转移所有权或踢出 owner 返回 `FORBIDDEN`。

## 预算

`FEpicodeSandboxGameplay::GetBudgetReport` 返回实体数、事件数、生成上限和每个生成器的存活数。超过实体预算返回 `SPAWN_BUDGET_EXCEEDED`；事件预算按 tick 限流，超限返回 `EVENT_BUDGET_EXCEEDED` 并回滚交互结果。

## 持久化

`FEpicodeSandboxPersistence` 把世界、事件流和组合摘要写入 `<sessionRoot>/snapshot.json`。恢复时校验摘要和事件序号；文件缺失、JSON 损坏或摘要不匹配返回 `SESSION_RESTORE_FAILED`，不加载部分世界。`FEpicodeSandboxSession::SaveSnapshot` / `RestoreSnapshot` 供重连和服务器重启使用。

## 生命周期

`FEpicodeSandboxSession` 状态：

```text
Uninitialized -> Ready -> Running -> Ready -> Closed
                    \-> Failed
```

- `Load`：校验制作包、场景、`sceneId` 匹配和演员实体类型；全部通过后才替换世界，失败不产生部分世界。
- `LoadLegacy`：只有 `scene.json` 时生成默认制作包视图，不写盘。
- `Start` / `Stop`：写入 `sandbox.started` / `sandbox.stopped`。
- `Tick`：只在 `Running` 时推进；每个 tick 更新 `world.tick`。
- `Reset`：从同一制作包和场景重建世界，清空 tick 和事件，写入 `sandbox.loaded` 和 `sandbox.reset`。
- `Close`：幂等；重复关闭返回成功且不重复写事件。
- `GetDigest`：组合世界和事件摘要，用于重演比较。

## 错误

| 错误码 | 含义 |
| --- | --- |
| `SCENE_INVALID` | 场景文档校验失败。 |
| `SCENE_MISMATCH` | 制作包 `sceneId` 与场景不一致。 |
| `PRODUCTION_REFERENCE_NOT_FOUND` | 演员或规则引用实体不存在。 |
| `CAST_ENTITY_TYPE_INVALID` | 演员引用实体不是角色。 |
| `CAST_PARTICIPANT_INVALID` | 演员参与类型无效。 |
| `ENTITY_EXISTS` / `ENTITY_NOT_FOUND` | 实体重复添加或删除不存在实体。 |
| `PARTICIPANT_EXISTS` | 参与者槽位重复。 |
| `INVENTORY_NEGATIVE` | 库存变更会产生负数。 |
| `SESSION_NOT_LOADED` | 未装载就重置。 |
| `SESSION_RUNNING` | 运行中重置。 |
| `SESSION_CLOSED` | 已关闭后启动。 |
| `INTERACTION_NOT_FOUND` | 实体或交互点不存在。 |
| `INTERACTION_OUT_OF_RANGE` | 交互超出距离。 |
| `INTERACTION_NOT_ALLOWED` | 动词未声明。 |
| `INTERACTION_CONDITION_FAILED` | 条件不满足。 |
| `INTERACTION_COOLDOWN` | 交互冷却中。 |
| `VIEWER_READ_ONLY` | Viewer 提交修改。 |
| `FORBIDDEN` | 权限不足。 |
| `SPAWN_BUDGET_EXCEEDED` | 实体/区域/生成器预算耗尽。 |
| `EVENT_BUDGET_EXCEEDED` | 事件预算耗尽。 |
| `RESOURCE_NOT_FOUND` / `RESOURCE_DEPLETED` / `RESOURCE_OUT_OF_RANGE` | 资源节点错误。 |
| `INSUFFICIENT_RESOURCES` | 库存或配方材料不足。 |
| `RECIPE_NOT_FOUND` | 配方不存在。 |
| `SESSION_RESTORE_FAILED` | 快照损坏或摘要不匹配。 |
