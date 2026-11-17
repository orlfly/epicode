# Epicode 使用说明

本文面向使用 Epicode 完成场景搭建、制作包编排、互动沙盒、导演调度、重演、资产导入和视频渲染的用户。内容以当前实现为准，设计细节见文末的文档索引。

Epicode 是 Unreal Engine 5.8 编辑器扩展。`scene.json` 是静态布景、环境、相机和基础实体的真相源；`production.json` 是演员、世界规则、叙事和镜头预设的作者根；运行中的沙盒世界是权威状态，UE Actor 只是投影。命令表达意图，事件记录事实。Studio 面板、HTTP API、WebSocket、导演和渲染工作进程共用同一套命令注册表，因此界面操作和脚本调用的校验、错误码与事件一致。

## 1. 环境准备

### 1.1 系统要求

- Linux x86_64
- Unreal Engine 5.8.4，默认安装路径 `/usr/local/unreal`
- `dotnet`、`clang`、`curl`、`python3`
- 可选：`ffmpeg`，用于 MP4 编码
- 实际渲染需要可用的 GPU 或离屏渲染环境

引擎安装目录只读。不要在其中创建工程文件。所有脚本通过 `Scripts/env.sh` 解析路径，可用 `UE_ROOT` 指向其他引擎安装：

```bash
cd /path/to/epicode/engine
UE_ROOT=/opt/UnrealEngine ./Scripts/env.sh --check
```

### 1.2 目录说明

```text
epicode/
  Docs/                       使用、设计、API、验收文档
  openspec/                   规格与变更归档
  engine/
    EpicodeVideo.uproject     工程文件
    Config/DefaultEpicode.ini 端口、路径、导入和渲染配置
    Plugins/                  八个功能插件：Core、Import、Sandbox、Director、
                              ControlAPI、Studio、Render、Network
    Scripts/                  构建、启动、测试、冒烟脚本
    Saved/                    运行期数据、场景、日志、渲染产物
```

`Saved/` 是生成目录，删除后场景库、制作包、Take、重演 journal、令牌、导入索引和渲染记录都会丢失。需要保留工作成果时备份整个 `engine/Saved/Epicode/`。

### 1.3 首次构建

```bash
cd engine
./Scripts/env.sh --check
./Scripts/Build.sh
```

构建日志写入 `engine/Saved/Logs/build.log`。默认构建目标为 `EpicodeVideoEditor Linux Development`，可用环境变量覆盖：

```bash
CONFIGURATION=Debug ./Scripts/Build.sh
TARGET=EpicodeVideoEditor ./Scripts/Build.sh
```

## 2. 启动

### 2.1 带界面的编辑器

```bash
cd engine
./Scripts/Editor.sh
```

`Editor.sh` 默认启动聚焦工作台：顶部 Epicode 菜单、中间实时视口、左侧对象列表/本地库、右侧实体检查器和对话占位、底部状态栏；stock Unreal 主窗口、主标签、工具栏和 Content Browser 被隐藏。

当前聚焦工作台已支持：

- 左右面板折叠和拖动调整宽度，布局、最近场景和视口镜头保存到 `Saved/Epicode/Editor/layout.json`，重启恢复。
- 场景新建/打开/保存/另存为/关闭/最近场景/删除到回收站；场景菜单可从 `scene.json` 文件导入到场景库，校验 schema 和实体引用，重复 ID 可选择覆盖、另存副本或取消，导入后在场景库高亮选中。
- 本地库索引写入 `Saved/Epicode/Library/index.json`；场景列表显示名称、短 sceneId、实体数和修改时间，悬停可查看完整 sceneId、levelPath 和可用状态。
- 资源库/场景库支持搜索和标签过滤；资源库可按全部/角色/物品分类，标题行“刷新”重新扫描索引，每条右侧“定位”在文件管理器中打开目录。
- 四类灯光创建、相机创建/激活/删除、删除选中实体、对象搜索与类型过滤、视口与列表双向选择。
- 右栏检查器按选中类型显示属性：角色动画/速度/可见性、物品材质/物理/可见性、灯光强度/颜色/衰减/阴影、摄像 FOV/焦距/光圈/对焦/活动相机。
- 检查器可数值编辑位置/旋转/缩放，切换局部/世界坐标并启用网格吸附（位置按网格、旋转按 15 度）。
- 角色/物品菜单可从 FBX/GLTF/GLB 或 FBX/OBJ/GLTF/GLB 导入资产到本地库；导入会提交 `import.submit` 任务，左栏资源库下方显示文件名、阶段、百分比、进度条和“取消导入”按钮，成功后保存 `.uasset` 到 `Content/Epicode/Imported/`，重启后仍可见。
- 导入成功、失败或取消都会清理本次自动创建的场景实体；失败时状态栏和对话区显示错误，取消只对尚未开始的排队任务立即生效，已经进入 Interchange 同步调用的任务会在完成后清理。
- 左栏“资源库”按类型列出已导入的骨骼网格和静态网格，单击或从菜单选择即可放置到当前场景；放置后自动选中并进入移动模式。
- 索引会保留上次记录的资产；资产文件从内容目录消失时显示“缺失”并禁用放置，但仍可从删除菜单移除索引。
- “从本地库删除角色/物品”默认只移除索引，不删除资产文件；需要恢复时使用“本地库 > 恢复隐藏资源”。
- 菜单中每个资产还有“删除文件”项：删除前检查当前场景和所有已保存场景的网格引用，命中时列出场景/实体并拒绝删除；无引用时删除 `.uasset` 并同步移除索引。
- `Q/W/E/R/F/Delete/Ctrl+Z/Ctrl+Shift+Z/Ctrl+Y/Ctrl+S` 快捷键；`Delete` 和 `Ctrl+S` 走 Epicode 命令，不绕过场景文档。
- 状态栏显示场景、选择、变换模式、库/对话状态、revision 和保存状态。
- 无场景、空场景、库为空、对话不可用和视口初始化失败均有空状态；视口失败页可点击“重试”，仍失败时使用 `--legacy`。

本地库拖放和缩略图仍在任务 4.x 完善；对话后端在任务 5.x 接入。详细验收见 `engine/Saved/Logs/editor-shell-workbench.log`。

需要旧版完整 Editor 和九页签 Studio 时使用：

```bash
cd engine
./Scripts/Editor.sh --legacy
```

打开旧版编辑器后，通过以下任一入口打开 Studio：

- 菜单 `Window > Epicode Studio`
- Level Editor 工具栏中的 `Epicode Studio` 按钮

面板可以停靠到任意位置。顶部是九个页签，底部是日志窗格，右上角显示当前场景、未保存标记和文档 revision。导演和重演工作区是独立页签，入口为 `Window > Epicode Director Workspace`。

### 2.2 无界面服务

```bash
cd engine
./Scripts/Headless.sh
```

默认监听：

- HTTP：`127.0.0.1:8710`
- WebSocket：`127.0.0.1:8711`

无界面模式使用 `-nullrhi`，适合 API、场景管理和任务提交，不能在该进程内完成实际像素渲染。渲染任务会另行启动工作进程。

停止服务使用 `Ctrl+C`，或关闭启动它的终端。同一时间只能有一个进程绑定默认端口，第二个编辑器实例会报端口占用。

### 2.3 令牌与端口

首次启动会生成 `Saved/Epicode/api-token`，权限为 `0600`。除健康检查外，HTTP 请求需要令牌：

```bash
cd engine
TOKEN=$(cat Saved/Epicode/api-token)
curl -sS -H "Authorization: Bearer ${TOKEN}" \
  http://127.0.0.1:8710/api/v1/health
```

也可以用 `X-Epicode-Token: <token>` 请求头。令牌不会写入日志。删除令牌文件并重启编辑器会重新生成。

常用环境变量：

| 变量 | 作用 |
| --- | --- |
| `EPICODE_API_HOST` | 监听地址，默认 `127.0.0.1` |
| `EPICODE_API_HTTP_PORT` | HTTP 端口，默认 `8710` |
| `EPICODE_API_WS_PORT` | WebSocket 端口，默认 `8711` |
| `EPICODE_MAX_RPS` | 每来源每秒请求上限，默认 `200` |
| `EPICODE_ALLOW_REMOTE` | 是否允许非回环访问 |
| `EPICODE_OUTPUT_DIR` | 渲染输出根目录覆盖 |
| `EPICODE_API_TOKEN` | 直接指定令牌，适合 CI，避免写入磁盘 |

配置项位于 `engine/Config/DefaultEpicode.ini`。默认只允许本机访问，CORS 关闭。不要在不可信网络中直接开启远程访问。

### 2.4 导演、重演与沙盒

导演客户端以 `-EpicodeDirector` 启动，并注入制作包和会话标识：

```bash
cd engine
./Scripts/Director.sh --server 127.0.0.1:7777 \
  --production <productionId> --session <sceneId>
```

启动日志写入 `Saved/Logs/director.log`，包含 `role=Director`、`production=<id>`、`session=<id>`；引擎日志包含 `Epicode process role: Director`。未知参数、非法服务器地址或分辨率返回退出码 2。

验证重演和完整沙盒工作流：

```bash
./Scripts/ReplaySmoke.sh
./Scripts/SandboxSmoke.sh
```

`ReplaySmoke.sh` 覆盖 journal、快照、ghost、resim、分支、保留清理和作业队列，日志为 `Saved/Logs/replay-smoke.log`。`SandboxSmoke.sh` 生成场景与制作包，验证制作包、交互、采集、制作、生成、NPC、导演节拍和 cue，日志为 `Saved/Logs/sandbox-smoke.log`。

## 3. Studio 快速上手

### 3.1 新建和保存场景

1. 打开 `Scenes` 页签。
2. 在 `New scene` 输入场景名，点击 `New`。
3. 编辑完成后点击 `Save`，场景写入 `Saved/Epicode/Scenes/<sceneId>/scene.json`。
4. 需要另存时，在 `Save-as name` 输入新名称，点击 `Save As`。
5. 已保存场景显示在列表中，点击 `Open` 打开，点击 `Delete` 删除。当前打开的场景不能删除。

`Close` 关闭当前场景。关闭前请先保存，未保存修改不会自动提交。

### 3.2 创建实体

Studio 提供角色、道具和灯光管理器。相机通过 API 创建，场景创建时会自动带一台默认相机。

角色：

1. 打开 `Characters`。
2. 在 `Name` 输入名称。
3. 用 `Skeletal mesh` 资产选择器选择骨骼网格。
4. 可选填写 `Animation`，格式为 `/Game/Path/Anim_Idle`。
5. 点击 `Spawn character`。

道具：

1. 打开 `Props`。
2. 输入名称，用 `Static mesh` 选择静态网格。
3. 点击 `Spawn prop`。
4. 生成后在列表中使用 `Set material`，配合 `Material` 选择器和 `Material slot` 设置材质。

灯光：

1. 打开 `Lights`。
2. 输入名称、类型和强度。类型支持 `directional`、`point`、`spot`、`rect`。
3. 点击 `Create light`。
4. 灯光颜色等未在界面暴露的字段通过 `light.create` 或 `light.update` API 设置。

相机：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"camera.create","params":{"name":"Shot Camera","fieldOfView":50}}'
```

创建后调用 `camera.activate` 激活，渲染和 `scene.snapshot` 都以活动相机为准。也可以直接在 Outliner 检查器中修改已有相机的视场角。

### 3.3 Outliner 和检查器

`Outliner` 是场景实体的统一列表。

- 按 `parentId` 计算缩进，显示层级。
- 搜索框按名称、类型或实体 ID 过滤，`Filter` 执行过滤，`Clear` 清空。
- 勾选复选框进行多选，`Delete selected` 批量删除，`Clear selection` 清空选择。
- 有子实体的行显示 `+` / `-`，点击展开或折叠。
- 拖动一行到另一行可以重挂接，默认保持世界变换。
- 每行提供 `Hide` / `Show`、`Duplicate`、`Delete`。
- 点击行名选中实体，检查器显示该实体的字段。

检查器支持：

| 实体类型 | 可编辑内容 |
| --- | --- |
| 所有实体 | 名称、父级 ID、Location、Rotation、Scale、可见性、删除 |
| 角色 | 动画路径、播放动画 |
| 道具 | 材质、材质槽、模拟物理 |
| 灯光 | 灯光类型、强度、更新灯光 |
| 相机 | 视场角、更新相机 |

变换字段使用三个逗号分隔的数值：

- Location 和 Scale 使用 `x, y, z`
- Rotation 使用 `pitch, yaw, roll`
- Location 单位为厘米，Rotation 单位为度

非法输入只写入底部日志，不会调用命令。输入框获得焦点时，外部刷新不会覆盖尚未提交的内容。

### 3.4 环境

打开 `Environment`，可以使用四个预设：

- `Clear`：晴天，低雾
- `Overcast`：阴天，高云量，柔光
- `Sunset`：低太阳角，暖色
- `Night`：低曝光，高雾

也可以手动填写时间、雾密度、云量、曝光和泛光，然后点击 `Apply environment`。未填写的字段保留当前值。

API 预设名称为 `clear`、`overcast`、`sunset`、`night`：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"environment.applyPreset","params":{"preset":"sunset"}}'
```

### 3.5 导入资产

打开 `Import`，有两种选择文件的方式：

- 点击 `Browse...`，在文件对话框中多选。
- 从文件管理器把文件拖到导入任务区域。

支持 `.fbx`、`.gltf`、`.glb`、`.obj`、`.usd`、`.usda`、`.usdc`、`.usdz`。`.umap` 和 `.uasset` 不作为外部资产导入。

操作步骤：

1. 选择文件后，第一个文件会显示在 `File` 输入框。
2. 在 `Kind` 填写 `prop`、`character` 或 `scene`。Studio 默认是 `prop`，不会自动改判。
3. 按需勾选 `Auto place` 和 `Replace existing`。
4. 点击 `Submit import`。多选时会为每个文件提交一个任务。
5. 在任务列表查看状态、进度和失败详情。
6. 任务结束后，`Reveal` 会在内容浏览器中定位导入产物。
7. 需要取消时点击 `Cancel`。

导入文件必须位于允许的导入根目录下。默认是工程目录和 `/tmp/epicode-imports`，可通过 `DefaultEpicode.ini` 的 `ImportRoots` 增加。文件对话框从工程目录开始，选择其他目录前请先确认根目录配置。

导入任务状态为 `queued`、`running`、`succeeded`、`failed`、`cancelled`。相同内容和相同导入选项会复用 `Saved/Epicode/imports.json` 中记录的资产，并返回复用警告。

### 3.6 渲染

打开 `Render`，点击 `Submit 3-frame PNG` 可以快速提交一个固定任务：

- 格式：PNG
- 分辨率：320 x 180
- 帧范围：0 到 3，共 3 帧
- 不包含音频

任务列表显示作业 ID、状态、阶段和进度。排队中或运行中的任务可以点击 `Cancel`。点击 `Refresh` 重新查询任务。

需要自定义格式、分辨率、帧率、帧范围或相机时，使用 `scene.render` API，详见第 6 节。

Take 渲染、多镜头轨道和逐轨道输出使用 `take.render`，详见 6.5。

### 3.7 API 页签

`API` 页签显示：

- 健康状态
- 渲染后端
- 已注册命令数
- 当前场景
- 消息数
- HTTP / WebSocket 地址
- 令牌文件路径

点击 `Run health check` 重新执行 `system.health`。这里不会显示令牌内容。

### 3.8 日志和撤销

底部日志窗格记录命令结果、错误和导入/渲染提示。右上角状态栏显示场景名、未保存标记和 revision。

Studio 的写操作通过命令注册表执行，并接入编辑器事务。可以使用编辑器的撤销/重做恢复场景文档和 Actor 投影，包括创建、变换、删除、环境预设和一次完整导入。导入撤销会移除该次导入创建的实体，但保留已经导入的资产。

### 3.9 沙盒与导演工作区

`Window > Epicode Director Workspace` 打开可停靠的导演工作区，包含五个页签：

| 页签 | 内容 |
| --- | --- |
| `Overview` | 当前制作包、会话、参与者、沙盒摘要、预算和重演状态 |
| `Timeline` | 事件时间线、播放头、缩放、过滤和 ghost/resim 区间 |
| `Beats` | 叙事节拍图：入口、当前节拍、条件、下一节拍和目标 |
| `Cues` | cue 列表、触发类型、动作和最近执行结果 |
| `Controls` | 暂停/恢复、倍率、目标、节拍、视角、接管和 Take 选择 |

时间线支持 0.25 到 8.0 倍缩放，过滤按事件类型、参与者和时间范围执行。ghost/resim 只预览历史世界，除 `director.setView` 外的写命令返回 `REPLAY_READ_ONLY`。分支在独立目录创建，父 journal 不被修改。

导演控制只对 owner 和 director 开放；Viewer 只读，表演者调用导演写命令返回 `FORBIDDEN`。Take 选择受 `LatestTakeVersion` 限制，已发布版本不可覆盖。控制面板的响应预算为 100ms，由 `Epicode.Studio.DirectorResponsiveness` 自动化验证。

Studio 的多人面板操作本进程会话；连接专用服务器时使用 `Scripts/Server.sh`、`Scripts/Client.sh`、`Scripts/Director.sh` 和控制 API。沙盒状态以 `sandbox.state` 返回的世界摘要和事件序号为准。

## 4. HTTP API 快速使用

完整端点、错误码和 JSON-RPC 细节见 [API.md](API.md)。本节只列出日常最常用的调用方式。

以下示例默认在仓库根目录执行。如果当前目录是 `engine/`，把路径中的 `engine/` 前缀去掉。

### 4.1 健康检查

健康检查不需要令牌：

```bash
curl -sS http://127.0.0.1:8710/api/v1/health
```

### 4.2 自定义命令信封

```bash
TOKEN=$(cat engine/Saved/Epicode/api-token)

curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"scene.new","params":{"name":"Demo"}}'
```

成功响应包含 `ok`、`result`、`affected`、`warnings`、`eventSequence`。失败响应包含 `error.code`、`error.message`、`error.detail` 和 `error.retryable`。

### 4.3 常用命令

| 目的 | 方法 | 关键参数 |
| --- | --- | --- |
| 新建场景 | `scene.new` | `name` |
| 列出场景 | `scene.list` | 无 |
| 保存场景 | `scene.save` | 无 |
| 另存场景 | `scene.saveAs` | `name` |
| 打开场景 | `scene.open` | `sceneId` |
| 场景快照 | `scene.snapshot` | 可选 `since` |
| 创建组 | `entity.create` | `type: "group"`、`name`、可选 `parentId` |
| 创建角色 | `character.spawn` | `skeletalMesh`、`name`、可选 `animation` |
| 创建道具 | `prop.spawn` | `staticMesh`、`name` |
| 创建灯光 | `light.create` | `lightType`、`name`、`intensity` |
| 创建相机 | `camera.create` | `name`、`fieldOfView` |
| 激活相机 | `camera.activate` | `id` |
| 修改变换 | `entity.setTransform` | `id`、`transform` |
| 重挂接 | `entity.reparent` | `id`、`parentId`、`keepWorldTransform` |
| 显示/隐藏 | `entity.setVisible` | `id`、`visible` |
| 删除实体 | `entity.delete` | `id`、可选 `cascade` |
| 应用环境预设 | `environment.applyPreset` | `preset` |
| 提交导入 | `import.submit` | `filePath`、可选 `kind` 等 |
| 查询导入 | `import.status` | `jobId` |
| 提交渲染 | `scene.render` | `format`、`width`、`height`、`startFrame`、`endFrame` |
| 查询渲染 | `render.status` | `jobId` |

`system.methods` 返回当前构建实际注册的全部方法，是命令列表的权威来源。

### 4.4 JSON-RPC 和批量

JSON-RPC 2.0：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/rpc \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"system.health","params":{}}'
```

简化批量：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/batch \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"commands":[{"method":"scene.list","params":{}},{"method":"system.health","params":{}}]}'
```

批量中的命令按顺序执行，单条失败不会自动回滚其他命令。需要原子性时使用 `scene.transaction`。

### 4.5 幂等写入

写操作可以带 `Idempotency-Key` 请求头：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -H "Idempotency-Key: my-scene-create-001" \
  -d '{"method":"scene.new","params":{"name":"Demo"}}'
```

相同键和相同参数在 24 小时内返回缓存结果，响应中 `idempotentReplay=true`。相同键不同参数返回 409 `IDEMPOTENCY_CONFLICT`。缓存持久化在 `Saved/Epicode/idempotency.json`。

### 4.6 WebSocket

先申请一次性票据：

```bash
TICKET=$(curl -sS -X POST http://127.0.0.1:8710/api/v1/ws-ticket \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" -d '{}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["ticket"])')
```

连接：

```text
ws://127.0.0.1:8711/api/v1/ws?ticket=<TICKET>&since=<可选事件序号>
```

票据 30 秒内有效且只能使用一次。连接后发送 `{"type":"ping"}` 保持连接，发送 `{"id":"1","method":"entity.list","params":{}}` 执行命令。`since` 会补发该序号之后的事件，最多 256 条。

### 4.7 制作包、沙盒与导演

```bash
# 列出并打开制作包
curl -sS -X POST "$BASE/api/v1/commands/production.list" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"production.list","params":{}}'

curl -sS -X POST "$BASE/api/v1/commands/production.open" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"production.open","params":{"productionId":"prod-1"}}'

# 创建制作包（sceneId 必须指向已保存场景）
curl -sS -X POST "$BASE/api/v1/commands/production.create" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"production.create","params":{"productionId":"prod-1","sceneId":"<sceneId>","name":"Demo Production"}}'

# 校验制作包与场景绑定
curl -sS -X POST "$BASE/api/v1/commands/production.validate" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"production.validate","params":{"productionId":"prod-1"}}'

# 读取沙盒状态和预算
curl -sS -X POST "$BASE/api/v1/commands/sandbox.state" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"sandbox.state","params":{}}'

# 导演暂停与恢复（owner/director）
curl -sS -X POST "$BASE/api/v1/commands/director.pause" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"director.pause","params":{"actorId":"owner-1","idempotencyKey":"pause-1"}}'

# 打开 ghost 并创建分支
curl -sS -X POST "$BASE/api/v1/commands/replay.ghost" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"replay.ghost","params":{"root":"Saved/Epicode/Sessions/session-1","startSequence":1,"count":50}}'

curl -sS -X POST "$BASE/api/v1/commands/replay.branch" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"replay.branch","params":{"parentRoot":"Saved/Epicode/Sessions/session-1","branchRoot":"Saved/Epicode/Sessions/branch-1","branchId":"branch-1","forkSequence":120}}'

# 提交确定性重演
curl -sS -X POST "$BASE/api/v1/commands/replay.job" \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"replay.job","params":{"root":"Saved/Epicode/Sessions/session-1","targetSequence":120}}'
```

所有写请求仍遵循 `Idempotency-Key` 头和命令上下文幂等规则。Viewer 令牌调用 `sandbox.spawn` 或 `director.pause` 会返回 `VIEWER_READ_ONLY`；表演者调用导演方法返回 `FORBIDDEN`。完整字段见 `Docs/ControlAPI.md`。

## 5. 导入工作流

### 5.1 同步和异步

- `import.submit` 创建异步任务，立即返回 `jobId`，随后用 `import.status` 轮询。
- `import.file` 同步执行，`import.run` 是它的别名。
- `import.list` 返回当前进程中的任务列表。
- `import.cancel` 取消任务。

异步提交示例：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "method":"import.submit",
    "params":{
      "filePath":"/absolute/path/hero.fbx",
      "kind":"character",
      "entityName":"Hero",
      "destinationPath":"/Game/Epicode/Imported"
    }
  }'
```

### 5.2 参数

| 字段 | 说明 |
| --- | --- |
| `filePath` | 源文件绝对路径，必填 |
| `kind` | `scene`、`character`、`prop`；省略时按扩展名和文件名分类 |
| `entityName` | 新实体名，缺省用文件名 |
| `destinationPath` | 内容目录，默认 `/Game/Epicode/Imported`，必须在 `/Game/` 下 |
| `transform` | 新实体局部变换 |
| `autoPlace` | 是否自动放置到当前场景，默认 `true` |
| `replaceExisting` | 目标资产已存在时是否覆盖，默认 `false` |
| `importMaterials` | 是否导入材质，默认 `true` |
| `scale` | 统一缩放，默认 `1.0`，必须大于 0 |
| `rotationOffset` | 旋转偏移，单位度 |

分类器会参考文件名中的 `char`、`character`、`hero`、`avatar`、`rig`、`skeleton`、`prop`、`static`、`mesh`、`environment` 等词。USD 默认按场景处理，文件名含角色关键词时按角色处理，无明确信号时默认道具。

### 5.3 结果和失败

`import.status` 返回 `state`、`progress`、`assets`、`entityIds`、`warnings`、`errorCode`、`errorMessage`。

常见失败：

- 文件不存在、不可读或是目录
- 文件不在允许的导入根目录
- 文件超过 `MaxImportBytes`，默认 2 GiB
- `destinationPath` 不是 `/Game/` 下的合法路径
- `kind=character` 但源文件没有骨骼网格，返回 `IMPORT_CHARACTER_NO_SKELETON`

`kind=character` 只会在导入结果包含骨骼网格时创建角色实体。只有静态网格时请改用 `kind=prop`。

### 5.4 取消和复用

- 排队中的任务会立即取消。
- 已经开始执行的同步 Interchange 导入无法中断，会完成资产导入。任务返回后会删除本次创建的实体，已完成的资产保留，状态记录为 `cancelled`。
- 导入队列串行执行，大量导入会占用该队列。
- 相同内容哈希和相同选项会复用已有资产，并在 `warnings` 中说明。

## 6. 渲染工作流

### 6.1 提交渲染

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "method":"scene.render",
    "params":{
      "format":"png",
      "width":320,
      "height":180,
      "frameRate":24,
      "startFrame":0,
      "endFrame":3,
      "overwrite":true,
      "includeAudio":false
    }
  }'
```

### 6.2 参数

| 字段 | 说明 |
| --- | --- |
| `sceneId` | 可选，缺省使用当前场景 |
| `cameraId` | 可选，缺省使用活动相机 |
| `outputDirectory` | 输出目录，为空时使用 `Saved/Epicode/Renders/<jobId>` |
| `format` | `png`、`exr`、`jpg`、`mp4` |
| `codec` | MP4 编码，默认 `h264` |
| `width`、`height` | 16 到 8192 |
| `frameRate` | 1 到 240 |
| `startFrame`、`endFrame` | `endFrame` 为排他上界，必须大于 `startFrame` |
| `quality` | MP4 CRF，16 到 51 |
| `overwrite` | 是否覆盖已有输出 |
| `includeAudio` | 当前版本忽略，MP4 无音轨 |

PNG、EXR、JPG 输出帧序列。MP4 先生成 PNG 帧序列，再调用系统 `ffmpeg` 编码为 H.264。缺少 `ffmpeg` 时返回 `RENDER_MP4_ENCODE_FAILED`。

### 6.3 轮询状态

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"render.status","params":{"jobId":"<jobId>"}}'
```

关注 `state`、`stage`、`progress`、`outputDirectory`、`outputFiles`、`renderedFrames`、`durationSeconds`、`errorCode`、`errorMessage`。实际响应中工作进程结果位于 `result.result`，完整字段以 [Rendering.md](Rendering.md) 为准。

任务目录包含：

```text
Saved/Epicode/RenderJobs/<jobId>/
  request.json
  meta.json
  worker.log
  progress.json
  result.json
  job.json
```

`progress.json` 每秒刷新。任务成功前会校验产物数量、文件非空和 PNG/JPG 分辨率，并把 SHA-1 写入 `result.json` 和 `job.json`。

### 6.4 取消、重试和超时

- `render.cancel` 取消排队或运行中的任务，运行中任务会尝试终止工作进程。
- `render.retry` 用原请求重新提交已结束任务。
- 默认一次只运行一个任务，`RenderConcurrency=1`，后续任务进入 `queued`。
- 运行超过 `RenderTimeoutSeconds` 的任务会被终止并标记 `RENDER_TIMEOUT`，默认 1800 秒。
- 已写出的帧不会在取消时自动删除。

渲染需要可用的渲染后端。`-nullrhi` 的无界面 API 进程不能完成实际像素输出。失败时先查看任务目录下的 `worker.log`，再查看 `result.json` 中的 `diagnostics` 和 `retryable`。

### 6.5 Take 渲染

Take 是不可变版本，`take.render` 读取 Take 固化的区间、内容摘要和镜头轨道，并为每个有效轨道提交独立作业：

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "method":"take.render",
    "params":{
      "takeId":"take-042",
      "takeVersion":3,
      "expectedContentDigest":"<take.contentDigest>",
      "outputDirectory":"Saved/Epicode/Renders/take-042-v3",
      "tracks":[
        {"cameraId":"<camera-a-guid>","startSequence":10,"endSequence":20},
        {"cameraId":"<camera-b-guid>","startSequence":20,"endSequence":30}
      ]
    }
  }'
```

成功结果包含首个作业的 `jobId`、`tracks[]`（每轨的 `jobId`、帧范围和输出目录）、`jobs[]` 和 `manifestPath`。清单写入 `<outputDirectory>/take-render.json`，记录 Take 校验和、内容摘要、轨道命名模板和作业标识。每个轨道输出到 `<outputDirectory>/track_<序号>_<cameraId>`，可用 `render.status` 或 `render.cancel` 分别查询和取消。

常见错误：`TAKE_NOT_FOUND`、`TAKE_CORRUPT`、`TAKE_DIGEST_MISSING`、`TAKE_DIGEST_MISMATCH`、`TAKE_CAMERA_MISSING`、`TAKE_TRACK_OVERLAP`、`TAKE_TRACK_OUT_OF_RANGE` 和 `TAKE_OUTPUT_UNWRITABLE`。完整字段见 [Rendering.md](Rendering.md)。

## 7. 文件和数据位置

| 路径 | 内容 |
| --- | --- |
| `engine/Saved/Epicode/Scenes/<sceneId>/scene.json` | 场景文档 |
| `engine/Saved/Epicode/Scenes/` | 场景库，按子目录扫描 |
| `engine/Saved/Epicode/Productions/<productionId>/production.json` | 制作包：演员、世界规则、叙事和镜头预设 |
| `engine/Saved/Epicode/Takes/<takeId>-v<version>.json` | 不可变 Take 版本 |
| `engine/Saved/Epicode/Replays/<sessionId>/journal.jsonl` | 重演事件 journal |
| `engine/Saved/Epicode/Replays/<sessionId>/replay.json` | journal 头和内容摘要 |
| `engine/Saved/Epicode/Presets/environment.json` | 环境预设覆盖 |
| `engine/Saved/Epicode/imports.json` | 导入索引和内容哈希 |
| `engine/Saved/Epicode/idempotency.json` | 幂等请求缓存 |
| `engine/Saved/Epicode/RenderJobs/<jobId>/` | 渲染任务目录 |
| `engine/Saved/Epicode/Renders/` | 默认渲染输出 |
| `engine/Saved/Epicode/api-token` | API 令牌 |
| `engine/Saved/Logs/` | 构建、测试、冒烟和编辑器日志 |

`scene.json` 是静态布景的真相源，`production.json` 是演员、规则和叙事的作者根。直接修改 JSON 后需要重新打开场景或制作包才能生效。不要在编辑器打开场景时手动改文件，否则保存会覆盖外部修改。运行时沙盒状态和事件在 `Saved/Epicode/Sessions/`、`Saved/Epicode/Replays/` 下，不回写作者文件。

备份工作成果时至少保留 `Saved/Epicode/Scenes/`、`Saved/Epicode/Productions/`、`Saved/Epicode/Takes/`、`Saved/Epicode/Replays/`、`Saved/Epicode/Presets/`、`Saved/Epicode/imports.json` 和渲染输出目录。

## 8. 脚本和验证

| 脚本 | 用途 |
| --- | --- |
| `Scripts/env.sh --check` | 检查引擎路径和工程文件 |
| `Scripts/Build.sh` | 构建编辑器目标 |
| `Scripts/Editor.sh` | 启动带界面的编辑器 |
| `Scripts/Headless.sh` | 启动无界面 API 服务 |
| `Scripts/Test.sh` | 运行 `Epicode.*` 自动化测试 |
| `Scripts/Smoke.sh` | 启动服务并执行 API、场景、导入冒烟 |
| `Scripts/Server.sh` | 启动游戏服务器和控制 API，支持场景、端口和额外 Unreal 参数 |
| `Scripts/Client.sh` | 启动表演者客户端并连接服务器 |
| `Scripts/Director.sh` | 以 Director 角色启动客户端并注入制作包 |
| `Scripts/Viewer.sh` | 启动只读 Viewer，支持相机模式和令牌 |
| `Scripts/AgentSmoke.sh` | 验证纯 HTTP Agent 加入、租约、移动和释放 |
| `Scripts/ViewerSmoke.sh` | 验证 Viewer 注册、相机、世界快照和会话结束 |
| `Scripts/NetworkSmoke.sh` | 验证双客户端、复制、表演、世界事件和重连 |
| `Scripts/SandboxSmoke.sh` | 验证制作包、沙盒、NPC、资源和导演工作流 |
| `Scripts/Replay.sh` | 运行 `Epicode.Replay` 自动化重演验证 |
| `Scripts/ReplaySmoke.sh` | 验证 journal、ghost、resim、分支和清理 |
| `Scripts/Clean.sh` | 删除工程生成物，不触碰引擎安装 |

冒烟脚本常用变量：

```bash
# 跳过导入
EPICODE_SMOKE_IMPORT=0 ./Scripts/Smoke.sh

# 包含三帧 PNG 渲染
EPICODE_SMOKE_RENDER=1 ./Scripts/Smoke.sh

# 指定格式和帧数
EPICODE_SMOKE_RENDER=1 EPICODE_SMOKE_FORMAT=mp4 EPICODE_SMOKE_END_FRAME=3 ./Scripts/Smoke.sh

# 验证重试和取消
EPICODE_SMOKE_RENDER=1 EPICODE_SMOKE_RETRY=1 ./Scripts/Smoke.sh
EPICODE_SMOKE_RENDER=1 EPICODE_SMOKE_CANCEL=1 ./Scripts/Smoke.sh
```

测试日志写入 `Saved/Logs/test.log`，冒烟编辑器日志写入 `Saved/Logs/smoke-editor.log`。沙盒、重演和多人冒烟分别写入 `Saved/Logs/sandbox-smoke.log`、`Saved/Logs/replay-smoke.log`、`Saved/Logs/network-smoke.log`、`Saved/Logs/agent-smoke.log` 和 `Saved/Logs/viewer-smoke.log`。

## 9. 常见任务示例

以下示例沿用 9.1 中的 `TOKEN`、`BASE` 和 `ENGINE_DIR`，默认在仓库根目录执行。

### 9.1 新建场景并保存

```bash
cd /path/to/epicode
TOKEN=$(cat engine/Saved/Epicode/api-token)
BASE=http://127.0.0.1:8710/api/v1/command
ENGINE_DIR="$(pwd)/engine"

curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"scene.new","params":{"name":"Demo"}}'

curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"scene.save","params":{}}'
```

### 9.2 创建道具并设置变换

```bash
curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "method":"prop.spawn",
    "params":{
      "staticMesh":"/Engine/EngineMeshes/Cube.Cube",
      "name":"Cube",
      "transform":{"location":[0,0,100],"rotation":[0,0,0],"scale":[1,1,1]}
    }
  }'
```

### 9.3 批量创建并保证原子性

```bash
curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "method":"scene.transaction",
    "params":{
      "commands":[
        {"method":"entity.create","params":{"type":"group","name":"Set"}},
        {"method":"light.create","params":{"lightType":"point","name":"Key","intensity":5}},
        {"method":"camera.create","params":{"name":"Shot","fieldOfView":50}}
      ]
    }
  }'
```

任一命令失败时，事务会回滚到执行前的文档快照。

### 9.4 导入并轮询

```bash
JOB=$(curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "method":"import.submit",
    "params":{
      "filePath":"'"${ENGINE_DIR}"'/Tests/Fixtures/cube.gltf",
      "kind":"prop",
      "entityName":"Imported Cube"
    }
  }' | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["jobId"])')

curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d "{\"method\":\"import.status\",\"params\":{\"jobId\":\"${JOB}\"}}"
```

### 9.5 提交渲染并轮询

```bash
JOB=$(curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"method":"scene.render","params":{"format":"png","width":320,"height":180,"startFrame":0,"endFrame":3}}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["jobId"])')

curl -sS -X POST "${BASE}" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d "{\"method\":\"render.status\",\"params\":{\"jobId\":\"${JOB}\"}}"
```

## 10. 故障排查

### 401 UNAUTHORIZED

确认请求头是 `Authorization: Bearer <token>`，令牌来自 `Saved/Epicode/api-token`。如果设置了 `EPICODE_API_TOKEN`，使用该值而不是文件。

### Unable to bind HTTP port

端口被其他进程占用。关闭旧编辑器实例，或设置：

```bash
EPICODE_API_HTTP_PORT=18710 EPICODE_API_WS_PORT=18711 ./Scripts/Headless.sh
```

### NO_SCENE

当前没有打开场景。先调用 `scene.new` 或 `scene.open`，再执行实体、导入或渲染命令。

### 导入被拒绝

检查文件是否存在、是否在 `ImportRoots` 下、是否超过 `MaxImportBytes`、`destinationPath` 是否以 `/Game/` 开头。Studio 的 `Browse...` 从工程目录开始，选择外部目录前先修改配置。

### 角色导入失败

`kind=character` 要求源文件包含骨骼网格。只有静态网格时改用 `kind=prop`，或先导入骨骼资产再调用 `character.spawn`。

### 渲染失败

查看 `Saved/Epicode/RenderJobs/<jobId>/worker.log` 和 `result.json`。常见原因：

- 没有可用 GPU 或离屏渲染环境
- 活动相机不存在
- 输出目录不可写或磁盘已满
- MP4 缺少 `ffmpeg`
- 任务超过 `RenderTimeoutSeconds`

### Studio 列表没有更新

Studio 按场景 revision 轮询刷新。外部 API 修改场景后，切换页签、点击 `Filter` / `Refresh`，或等待下一次 Tick。导入和渲染任务由服务层持有，重新打开面板后会重新查询。

### 场景打开很慢

打开场景需要加载编辑器关卡并重建 Actor，切换时界面可能短暂阻塞。场景较大时请等待日志完成后再操作。

## 11. 多人会话

多人会话从已保存场景启动，运行时状态只写 `Saved/Epicode/Sessions/<sessionId>/`，不会修改作者场景。启动服务器和客户端：

编辑器内也可从 `Window > Epicode Multiplayer` 打开多人面板，输入场景和玩家名后创建或加入本进程会话；服务器地址输入框当前仅预留，面板尚未连接专用服务器。玩家列表、角色分配、表演者 HUD 和导演控制台都在同一面板中查看。

```bash
cd engine
./Scripts/Server.sh --port 7777 --scene <sceneId>
./Scripts/Client.sh --server 127.0.0.1:7777 --name Alice
./Scripts/Client.sh --server 127.0.0.1:7777 --name Bob
./Scripts/Director.sh --server 127.0.0.1:7777 --production <productionId> --session <sceneId>
```

服务器按场景 ID 在 `Saved/Epicode/Productions/` 下选择制作包。找到制作包时，演员槽位、世界规则和叙事进入沙盒会话；找不到时使用仅场景的 legacy 视图，`sandbox.state.legacy=true`。Owner 和 Director 可以执行导演写命令，Performer 和 Agent 可交互、采集和制作，Viewer 只读。

纯 HTTP Agent（不启动 Unreal）：

```bash
./Scripts/AgentSmoke.sh
python3 Scripts/AgentClient.py \
  --base http://127.0.0.1:8710 \
  --name Agent \
  --character <characterEntityId> \
  --target 300,200,0
```

Agent 首次 `session.join` 使用 `participantKind=agent`，从结果读取 `participantToken`，后续请求带 `X-Epicode-Participant`。命令信封、幂等、`expectedStateVersion`、WebSocket 过滤和错误码见 `Docs/ControlAPI.md`，最小循环见 `Docs/AgentClient.md`。

只读 Viewer：

```bash
./Scripts/Viewer.sh --server 127.0.0.1:7777 --name Viewer --camera free --res 1280x720
./Scripts/ViewerSmoke.sh
```

Viewer 使用 `Saved/Epicode/viewer-token` 或 `--token`，不占表演者槽位、不生成角色。按键 `1`/`2`/`3`/`4` 切换自由、跟随、固定和预置相机，`O` 切换信息叠加。详见 `Docs/ViewerClient.md`。

自动化客户端可通过 `session.create`、`session.join`、`session.snapshot` 和 `session.export` 管理会话。角色权限、所有权、重连和错误码见 [多人协同基线](Multiplayer.md)。

当前运行时 UE 角色复制层已实现并通过双客户端冒烟；服务器和编辑器/Headless 进程都启动 HTTP/WS 控制面。Studio 面板仍操作本进程会话，服务器地址输入尚未接入 HTTP 客户端。

## 12. 已知限制

- Studio 的 Render 页签只提供固定 320 x 180、三帧 PNG 的快捷提交。完整格式、分辨率、帧范围和相机选择通过 `scene.render` API。
- Take 渲染通过 `take.render` 按轨道拆分作业；当前不生成单文件切镜序列，每条轨道有独立作业、目录和清单记录。
- Studio 没有独立的相机创建页签。相机通过 API 创建，Outliner 检查器可以修改已有相机的视场角。
- Studio 列表使用 revision 轮询，不订阅 WebSocket 事件。
- 运行中的同步 Interchange 导入无法中断。取消后会删除本次创建的实体，但保留已经导入的资产。
- `scene.render` 只输出活动相机；多相机使用 `take.render` 的逐轨道作业。两者都不支持字幕和音频混音，`includeAudio` 被忽略。
- 重演冒烟通过自动化覆盖 journal 和重演引擎，实时服务器 journal 导出仍需后续接入。
- 无 GPU 环境只验证 Take 提交、轨道和清单，不比较实际像素。
- MP4 依赖系统 `ffmpeg`。引擎自带 MP4 编码器在 Linux 上可能不可用。
- 不解析材质重定向或骨骼重定向表，需要时通过 `character.setMaterial`、`prop.setMaterial` 后处理。
- `.umap` 不通过资产导入器导入，使用场景库或编辑器打开。
- 场景保存只写 JSON，渲染工作进程按 JSON 重建临时关卡。

## 13. 相关文档

- [工程 README](../engine/README.md)
- [Studio 面板](Studio.md)
- [场景模型](SceneModel.md)
- [导入](Import.md)
- [渲染](Rendering.md)
- [多人协同基线](Multiplayer.md)
- [HTTP/JSON-RPC/WebSocket API](API.md)
- [控制 API](ControlAPI.md)
- [制作包](ProductionPackage.md)
- [沙盒世界](SandboxWorld.md)
- [导演](Director.md)
- [重演](Replay.md)
- [架构](Architecture.md)
- [运维手册](Operations.md)
- [验收记录](Acceptance.md)
- [Studio 验收记录](StudioAcceptance.md)
