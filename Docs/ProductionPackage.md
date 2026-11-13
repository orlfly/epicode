# 制作包（Production Package）

制作包是新的作者根。`scene.json` 继续只描述静态布景、环境、相机和基础实体；`production.json` 负责“谁在这个世界里、能做什么、故事如何推进、从哪个镜头看”。

## 存储

默认根目录：

```text
Saved/Epicode/Productions/<productionId>/production.json
```

- `productionId` 是目录名，也是制作包稳定标识。
- 保存先写同目录临时文件，再替换 `production.json`；替换失败时恢复旧文件，不会留下半截 JSON。
- 删除默认移动到 `Saved/Epicode/Trash/Productions/`，不直接抹除。
- 当前打开的制作包不能删除，返回 `PRODUCTION_IN_USE`。

示例文件见 [`Docs/Examples/Production/production.json`](Examples/Production/production.json) 与 [`Docs/Examples/Scenes/`](Examples/Scenes/)。

## 字段

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `schemaVersion` | number | 当前为 1；高于构建支持的版本会被拒绝。 |
| `productionId` | string | 稳定标识，默认 GUID；旧版缺失时自动生成。 |
| `name` | string | 制作包显示名。 |
| `sceneId` | string | 必须指向已保存场景。 |
| `cast` | array | 演员槽位：`slot`、`entityId`、`participantKind`（`performer`、`agent`、`viewer`、`npc`）。 |
| `worldRules` | object | 交互点、区域、生成器、生成表、资源、NPC 档案和运行预算。 |
| `narrative` | object | 入口节拍、变量、节拍节点和导演 cue。 |
| `cameraPresets` | array | 镜头预设：`id`、`name`、`cameraId`、`settings`。 |
| `extensions` | object | 工具或插件扩展；原样保留。 |

未知顶层字段也会原样保留，保存时不会被静默删除。演员项、镜头预设和规则对象中的未知字段同样保留。

### 世界规则示例

```json
{
  "worldRules": {
    "interactionPoints": [
      {
        "id": "door-control",
        "entityId": "33333333-3333-4333-8333-333333333333",
        "verbs": ["use"],
        "maxDistance": 180,
        "cooldownSeconds": 2,
        "conditions": ["hasKeycard"],
        "outcomes": ["setVariable:alarmLevel=1"]
      }
    ],
    "zones": [{"id": "roof", "name": "Roof", "maxEntities": 40}],
    "spawners": [{"id": "guard-spawn", "zoneId": "roof", "spawnTableId": "guards", "intervalSeconds": 30, "maxAlive": 4}],
    "spawnTables": [{"id": "guards", "entries": [{"entityType": "Character", "assetPath": "/Game/Epicode/NPC/Guard", "weight": 1}]}],
    "budgets": {"maxEntities": 200, "maxEventsPerSecond": 500, "maxSpawnPerSecond": 10}
  }
}
```

### 叙事示例

```json
{
  "narrative": {
    "entryBeatId": "beat-arrival",
    "variables": {"alarmLevel": 0, "hasKeycard": false},
    "beats": [
      {"id": "beat-arrival", "entryConditions": ["true"], "exitConditions": ["alarmLevel >= 1"], "objectives": [], "cues": [], "next": ["beat-alarm"]},
      {"id": "beat-alarm", "entryConditions": ["alarmLevel >= 1"], "objectives": ["objective-alarm"], "cues": ["cue-lights"], "next": []}
    ],
    "objectives": [{"id": "objective-alarm", "description": "Confirm the alarm", "condition": "alarmLevel >= 1"}],
    "cues": [{"id": "cue-lights", "trigger": "beat", "beatId": "beat-alarm", "actions": ["environment.lights"], "once": true}]
  }
}
```

节拍必须从入口可达；所有 `next`、`objectives`、`cues`、cue 目标和条件变量必须能解析。规则标识在制作包内唯一。

## 校验

文档级校验（`Validate`）：

- `schemaVersion`、`productionId`、`name`、`sceneId` 必填。
- `worldRules` 与 `narrative` 必须是对象。
- 演员槽位非空且不区分大小写唯一；同一实体不能占用多个槽位。
- `participantKind` 必须是 `performer`、`agent`、`viewer` 或 `npc`。
- 镜头预设 `id` 唯一，`cameraId` 非空。

绑定校验（`FEpicodeProductionValidator::ValidateBinding`）：

| 错误码 | 含义 |
| --- | --- |
| `SCENE_NOT_FOUND` | `sceneId` 在场景根目录下不存在。 |
| `SCENE_VERSION_UNSUPPORTED` | 场景版本高于构建支持版本。 |
| `SCENE_INVALID` | 场景文件不可读或不是合法场景。 |
| `CAST_ENTITY_TYPE_INVALID` | 演员引用实体不是 `Character`。 |
| `PRODUCTION_REFERENCE_NOT_FOUND` | 演员或规则引用的实体不存在。 |
| `DUPLICATE_CAST_SLOT` | 演员槽位重复。 |
| `DUPLICATE_CAST_ENTITY` | 同一实体占用多个演员槽位。 |
| `DUPLICATE_RULE_ID` | 规则、节拍或 cue 标识重复。 |
| `PATH_NOT_ALLOWED` | 内容路径逃逸允许根目录。 |
| `PRODUCTION_BUDGET_EXCEEDED` | 预算超过配置上限。 |

校验失败返回路径（例如 `cast[0].entityId`），不写入部分结果。

## 版本与迁移

- 加载当前版本：按原样读取。
- 加载受支持旧版本：补齐缺失字段，内存中标记 `pendingUpgrade`，下次保存写入当前版本。
- 高于当前版本：返回 `PRODUCTION_VERSION_UNSUPPORTED`，不执行部分迁移。
- 仅 `scene.json` 的旧工作流：`FEpicodeProductionLegacy::MakeDefaultView` 生成仅存在于会话中的默认制作包视图，不写盘；世界规则使用默认值，叙事图为空。

## C++ 接口

- `FEpicodeProductionDocument`：数据模型、JSON 往返、默认值、文档校验。
- `FEpicodeProductionLibrary`：创建、打开、保存、另存为、列出、删除、回收目录和原子写。
- `FEpicodeProductionValidator`：文档校验、场景绑定校验、路径白名单。
- `FEpicodeProductionEditSession`：内存编辑、验证、撤销/重做、保存和重新加载；任一修改失败回滚内存，保存失败保留磁盘旧文件。
- `FEpicodeProductionLegacy::MakeDefaultView`：旧场景默认视图。
