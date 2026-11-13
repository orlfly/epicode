# 导入

## 支持格式

静态网格与骨骼网格通过 Interchange 导入：

- `.fbx`
- `.gltf` / `.glb`
- `.obj`
- `.usd` / `.usda` / `.usdc` / `.usdz`（作为场景导入）

`.umap` 和 `.uasset` 不作为外部资产导入；`.umap` 通过 `scene.open` 或场景库打开。

## 判定规则

分类器先看扩展名，再看文件名：

- 含 `char`、`character`、`hero`、`avatar`、`rig`、`skeleton` 等词：角色
- 含 `prop`、`static`、`mesh`、`environment` 等词：道具
- USD 默认按场景处理，文件名含角色关键词时按角色处理
- 无明确信号：默认道具

可用 `import.classify` 预览判定，或显式传 `kind` 覆盖。

## 提交流程

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "method":"import.submit",
    "params":{
      "filePath":"/data/assets/hero.fbx",
      "kind":"character",
      "entityName":"Hero",
      "destinationPath":"/Game/Epicode/Imported"
    }
  }'
```

同步调用使用 `import.file`（`import.run` 是它的别名）；异步提交返回 `jobId`，随后轮询：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"method":"import.status","params":{"jobId":"..."}}'
```

状态：`queued`、`running`、`succeeded`、`failed`、`cancelled`。

## 请求字段

| 字段 | 说明 |
| --- | --- |
| `filePath` | 源文件绝对路径，必填 |
| `kind` | `scene`、`character`、`prop`；缺省按扩展名和文件名分类 |
| `entityName` | 新实体名；缺省用文件名 |
| `destinationPath` | 内容目录，默认 `/Game/Epicode/Imported`，必须在 `/Game/` 下 |
| `transform` | 新实体局部变换 |
| `autoPlace` | 是否自动放置到当前场景，默认 `true` |
| `replaceExisting` | 目标资产已存在时是否覆盖，默认 `false` |
| `importMaterials` | 是否导入材质，默认 `true` |
| `scale` | 统一缩放，默认 `1.0`，必须大于 0 |
| `rotationOffset` | 旋转偏移，单位度 |

## 校验

- 文件必须存在、可读、非目录。
- 大小不超过 `Config/DefaultEpicode.ini` 的 `MaxImportBytes`，默认 2 GiB。
- `destination` 必须是 `/Game/` 下的合法内容路径。
- 导入队列串行执行，一次只运行一个任务。

## 结果

`import.status` 返回 `assets`（对象路径）、`entityIds`、`warnings`、`errorCode`、`errorMessage`。失败时不会留下半成品实体。

`kind=character` 时只有导入结果包含骨骼网格才会创建角色实体；如果源文件只有静态网格，返回 `IMPORT_CHARACTER_NO_SKELETON` 并提示改用 `kind=prop`。

## 测试夹具

`engine/Tests/Fixtures/` 包含 `cube.gltf`、`quad.obj`、`cube.fbx`（静态网格）和 `skeletal.fbx`（带骨骼与动画）。自动化测试 `Epicode.Import.Character` 使用 `skeletal.fbx` 验证角色导入，并使用 `cube.fbx` 验证缺少骨架时的失败路径。

## 限制

- 导入索引持久化在 `Saved/Epicode/imports.json`，以 SHA-1 内容哈希、类型、目标目录、缩放、旋转和材质选项为键；相同内容与选项会复用已有资产并给出警告。
- 导入在编辑器进程内同步调用 Interchange，通过队列串行化；大量导入会阻塞该队列。
- 取消对排队中的导入立即生效；已经开始执行的同步 Interchange 导入无法中断，会完成资产导入，但导入返回后本次创建的实体会被删除，已完成的资产保留，作业状态记录为 `cancelled`。
- 不解析材质重定向或骨骼重定向表；需要时通过 `character.setMaterial`、`prop.setMaterial` 后处理。
