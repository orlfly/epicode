# 场景模型

## 真相源

场景 JSON 是唯一真相源，Actor 是投影。任何修改先落文档，再同步 Actor 与事件。UI 和 API 调用同一个命令注册表，因此行为一致。

保存位置：

```
Saved/Epicode/Scenes/<sceneId>/scene.json
```

场景列表通过扫描 `Saved/Epicode/Scenes/` 下的子目录生成，没有单独的索引文件。`Saved/Epicode/Presets/environment.json` 保存环境预设覆盖。

场景名称不是唯一键，`sceneId` 才是。聚焦工作台遇到同名场景时会在显示名称后追加 `#短sceneId`，菜单和本地库使用同一份消歧后的名称。自动化测试使用 `Saved/Epicode/Tests/Scenes/` 下的临时场景根，不写入用户场景库。

## 文档结构

```json
{
  "schemaVersion": 2,
  "sceneId": "8f3c...",
  "name": "Demo",
  "levelPath": "/Game/Epicode/Scenes/Demo",
  "activeCameraId": "camera-guid",
  "runtime": {
    "agent": {
      "controllableCharacterIds": ["entity-guid"],
      "spawnPoints": [
        {
          "id": "agent-spawn-a",
          "transform": {
            "location": [0, 0, 0],
            "rotation": [0, 0, 0],
            "scale": [1, 1, 1]
          }
        }
      ],
      "allowedActions": ["wave", "bow"]
    },
    "viewer": {
      "cameraIds": ["camera-guid"],
      "defaultCameraId": "camera-guid",
      "spawnTransform": {
        "location": [0, 0, 100],
        "rotation": [0, 0, 0],
        "scale": [1, 1, 1]
      },
      "showOverlay": true
    }
  },
  "environment": {
    "timeOfDay": 15.0,
    "preset": "clear",
    "fogDensity": 0.02,
    "cloudCoverage": 0.3,
    "exposureCompensation": 0.0,
    "bloomIntensity": 0.675,
    "lut": ""
  },
  "entities": [
    {
      "id": "entity-guid",
      "type": "character",
      "name": "Hero",
      "parentId": "parent-guid",
      "visible": true,
      "tags": ["main"],
      "transform": {
        "location": [0, 0, 0],
        "rotation": [0, 0, 0],
        "scale": [1, 1, 1]
      },
      "character": {
        "mesh": "/Game/Characters/Hero",
        "animation": "/Game/Animations/Idle",
        "speed": 1.0,
        "playing": false
      }
    }
  ]
}
```

`type` 取值：`group`、`character`、`prop`、`light`、`camera`。类型负载放在同名小写键下，其他键缺失时为空对象。

`runtime` 为可选对象。`runtime.agent.controllableCharacterIds` 必须引用角色实体；`spawnPoints` 的 `id` 必须唯一且变换有限；`allowedActions` 不能包含空字符串。`runtime.viewer.cameraIds` 必须引用相机实体，`defaultCameraId` 必须包含在 `cameraIds` 中。未知的 `runtime`、`runtime.agent`、`runtime.viewer` 子字段会保留，便于后续版本扩展。

## 不变量

- `sceneId`、`name` 必填；`schemaVersion` 不得高于构建支持的版本。
- 实体 `id` 必须为 GUID 且唯一；`name` 非空且同父级下唯一。
- `parentId` 必须指向存在的实体，且父子链不能成环。
- `transform` 的三个数组必须各含三个有限数值。
- 删除实体级联删除所有后代；删除相机后清空 `activeCameraId`。
- 场景必须存在默认相机；激活相机时相机实体必须存在。
- 运行时元数据引用的角色和相机必须存在且类型正确；默认查看相机必须出现在查看相机列表中。

## 版本迁移

`FEpicodeSceneMigration` 按版本号顺序迁移旧文档，当前支持 `0 -> 1 -> 2`：

- 缺失 `schemaVersion` 或值为 `0` 的文档视为版本 0。
- 旧字段 `actors` 会转换为 `entities`。
- 缺失的 `environment` 填充默认环境，缺失的 `activeCameraId` 填充空字符串。
- 迁移到版本 2 时写入空 `runtime` 对象；旧场景行为保持不变，Agent 可控角色列表为空，Viewer 默认相机回退到活动相机。
- 迁移后 `schemaVersion` 写为 2；高于构建支持版本的文档直接拒绝打开。

## 运行时元数据命令

- `scene.runtime.get` 返回当前 `runtime`、`levelPath` 和场景版本。
- `scene.runtime.set` 接受 `{runtime, levelPath?, validateForRuntime?}`，在事务内替换运行时元数据。
- `validateForRuntime=true` 时要求 `levelPath` 非空，并校验可控角色、出生点、查看相机和默认相机引用。
- 校验失败时返回 `INVALID_RUNTIME`，文档保持原值。

## 变换

`location` 单位为厘米，`rotation` 为 `[Pitch, Yaw, Roll]`，`scale` 为三轴缩放。`entity.setTransform` 默认按局部坐标写入；`parentId` 改变时可通过 `keepWorldTransform` 保持世界变换。

## 快照

`scene.snapshot` 返回：

```json
{
  "revision": 12,
  "document": {"schemaVersion": 2, "sceneId": "...", "entities": [], "runtime": {}},
  "entities": [{"id":"...","type":"character","name":"Hero","worldTransform":{}}],
  "environment": {},
  "activeCameraId": "...",
  "dirty": true
}
```

`revision` 每次文档修改递增。Studio 通过轮询 revision 刷新列表，避免重复重建 UI。

传入 `{"since": <eventSequence>}` 时额外返回 `eventSequence`、`since`、`incremental`、`events` 和 `unchanged`。事件序号等于当前值时 `unchanged=true`；序号落后但仍在事件历史内返回增量事件；落后超过历史容量时 `fullSnapshotRequired=true`，客户端应使用完整快照。`since` 大于当前序号返回 `SNAPSHOT_VERSION_AHEAD`。

## 场景库

- `scene.new`：创建默认场景；执行 `scene.save` 后进入场景库。
- `scene.open`：按场景 ID 从场景库打开；未保存的当前场景会先被替换，需自行保存。
- `scene.switch`：当前场景有未保存修改时拒绝切换；先 `scene.save` 再切换。
- `scene.save`：写入 `<sceneId>/scene.json`；渲染工作进程会据此生成临时关卡，不在保存时直接覆盖 `levelPath`。
- `scene.saveAs`：复制到新名称/路径，更新场景 ID。
- `scene.delete`：从库中移除，不删除外部引用的资产。
- `scene.list`：返回场景摘要（ID、名称、路径、修改时间）。

## 事务

`scene.transaction` 接受 `{commands:[{method,params}]}`，按顺序执行。任一操作失败时回滚文档到事务前快照，并返回失败操作。适合批量摆放、一次性导入多个实体等场景。

写命令在 `Source=studio` 或默认可撤销上下文中会包裹 `GEditor` 事务。`EpicodeStudio` 通过 `IEpicodeTransactionSink` 在事务开始时记录场景 JSON 快照，在撤销/重做后恢复文档并重建 Actor 投影。一次导入（资产导入 + 实体创建）也作为单次事务记录，撤销会移除该次导入创建的实体并保留资产。

## 环境

`environment.set` 接受部分字段，未提供字段保留原值；`environment.applyPreset` 将预设值写入文档。内置预设：

- `clear`：晴天，低雾
- `sunset`：低太阳角，暖色
- `night`：低曝光，高雾
- `overcast`：阴天，高云量，柔光

`environment.capture` 从当前关卡捕获光照/雾参数并写回文档。

## 相机

`camera.create` 创建相机实体；新相机不会自动激活，需要再调用 `camera.activate`。`camera.update` 修改 FOV、宽高比、后期参数。`camera.activate` 切换活动相机；`scene.snapshot` 和渲染都以活动相机为准。

`fieldOfView` 优先于 `focalLength`。只给 `focalLength` 时按 36mm 传感器换算：`FOV = 2 * atan(36 / (2 * focalLength))`，50mm 约等于 39.6°。删除最后一台相机时自动创建 `Default Camera`。

## 作者平面与运行时平面边界

- 作者平面场景位于 `Saved/Epicode/Scenes/<sceneId>/scene.json`，由编辑器、Studio 和 `scene.*` 命令维护。
- 多人会话从作者平面只读装载，装载时校验 schema 版本、场景标识和文档完整性。
- 运行时状态位于 `Saved/Epicode/Sessions/<sessionId>/session.json` 与 `events.jsonl`，不写回作者场景。
- `session.export` 只写会话目录；会话结束不会覆盖原场景 JSON。
- 角色分配、所有权、断线释放和世界版本属于运行时状态，不进入作者场景文档。
- 运行时世界表现是场景投影：服务器从场景实体摘要生成道具、灯光和相机 Actor，从 `EpicodeWorldState` 生成物品与建造物 Actor，从 `environment` 生成复制环境 Actor；这些 Actor 不回写场景。
- `levelPath` 只决定运行时关卡；同一场景 JSON 可以投影到不同关卡，但 Viewer 必须和服务器进入同一 `levelPath`。
- 需要把运行时结果带回作者平面时，应显式导出会话快照，再由编辑器侧命令选择性应用，而不是让服务器直接改场景文件。
