# Design

## Context

Epicode 当前以 Unreal Editor 扩展的形式运行：`Scripts/Editor.sh` 启动完整 `UnrealEditor`，`EpicodeStudio` 在其中注册 nomad 页签。场景真相源是 `Saved/Epicode/Scenes/<sceneId>/scene.json`，UE Actor 是投影，所有写操作经过 `FEpicodeCommandRegistry`。现有 Studio 已覆盖场景、大纲、角色、道具、灯光、环境、导入、渲染和 API 九个页签，但默认界面仍是 Unreal 的完整编辑器。

本设计把默认 Editor 变成一个聚焦的 Epicode 工作台：保留 UE 的场景渲染视口、选择、变换 gizmo 和编辑器世界，隐藏通用编辑器外壳；交互编辑只允许角色、物品、灯光、摄像四类实体的放置、位置/旋转/缩放调整、属性调整和从场景删除。主菜单按 Epicode 能力组织，并增加一个对话窗口，通过 DeepSeek Harness SDK 生成受约束的编辑脚本。

核心约束：

- 不修改 `/usr/local/unreal`，不 fork Unreal Editor。
- 场景 JSON 仍是唯一事实源；UI、对话脚本和 API 使用同一套命令。
- 生成式模型不得直接执行任意代码、控制台命令或文件操作。
- 所有 AI 修改必须可预览、可拒绝、可一次撤销、可审计。
- 本地库只读取配置根目录，不上传资产、不自动下载资产。
- Linux x86_64 + UE 5.8.4 为首要目标。

## Goals / Non-Goals

**Goals:**

- 默认启动一个视觉上只有 Epicode 工作台的 Editor 窗口。
- 保留 `SEditorViewport` / `FLevelEditorViewportClient` 级别的场景渲染和 gizmo 交互。
- 把可见交互限定在四类实体和本地库，不暴露通用编辑器功能。
- 提供与 Epicode Studio 能力对应的主菜单和最小属性检查器。
- 提供对话窗口，把自然语言转换成可校验、可预览、可撤销的 JSON 编辑脚本。
- 为 DeepSeek Harness SDK 提供隔离适配层，允许 SDK、sidecar 或 HTTP 后端替换。
- 保持离线可用：对话后端不可用时，手工编辑和本地库仍然工作。

**Non-Goals:**

- 不实现新的渲染器、路径追踪器或视频编码器。
- 不删除 Unreal Engine 的底层模块；只做界面级隐藏和入口限制。
- 不实现完整 Content Browser、资产编辑器、蓝图、材质图、Sequencer、地形、植被、PIE、渲染队列、插件浏览器或项目设置。
- 不做云端资产库、资产市场、自动购买或自动下载。
- 不做模型微调、训练或本地大模型推理。
- 不允许模型输出 Python/Lua/控制台命令并直接执行。
- 不做多用户协同对话或远程共享会话。

## Architecture Overview

```mermaid
flowchart LR
    Launch["Editor.sh --focused"] --> Shell["EpicodeEditorShell<br/>自定义顶层窗口"]
    Shell --> Menu["Epicode 主菜单"]
    Shell --> Viewport["SEditorViewport<br/>FLevelEditorViewportClient"]
    Shell --> LibraryUI["本地库面板"]
    Shell --> Inspector["四类实体检查器"]
    Shell --> Chat["对话面板"]
    Shell --> Status["状态栏"]

    Menu --> Registry["FEpicodeCommandRegistry"]
    LibraryUI --> Library["FEpicodeLocalLibrary"]
    Inspector --> Registry
    Chat --> Assistant["EpicodeAssistant<br/>DeepSeek 适配层"]
    Assistant --> Context["场景/选择/库上下文"]
    Assistant --> Script["JSON 编辑脚本"]
    Script --> Validator["脚本校验器"]
    Validator --> Preview["差异预览/确认"]
    Preview --> Runner["Script Runner"]
    Runner --> Registry
    Registry --> Scene["scene.json"]
    Registry --> Actors["UE Actor 投影"]
    Viewport --> Actors
```

### Module layout

| 模块 | 类型 | 职责 |
| --- | --- | --- |
| `EpicodeEditorShell` | Editor | 聚焦顶层窗口、主菜单、视口宿主、库/检查器/对话/状态栏布局、默认页签隐藏、快捷键限制、旧版兼容开关 |
| `EpicodeAssistant` | Editor | 对话模型、消息历史、流式回复、DeepSeek 适配、上下文构建、脚本生成、预览/确认 UI |
| `EpicodeCore` | Runtime | 现有场景命令、实体投影、事务、撤销；新增编辑脚本校验器和执行器 |
| `EpicodeImport` | Editor | 现有 Interchange 导入；新增本地库索引、扫描、导入、删除和元数据 |
| `EpicodeStudio` | Editor | 旧九页签兼容模式；聚焦模式下只保留必要的底层服务，不注册默认主页签 |

### Launch flow

```text
Editor.sh
  -> parse --focused (default) / --legacy
  -> UnrealEditor <project> -EpicodeFocusedEditor
  -> EpicodeEditorShell StartupModule
  -> after engine init:
       hide stock editor main window
       create Epicode shell window
       attach viewport to current editor world
       restore last scene or show empty state
  -> on exit: close shell, restore/hide stock window, save layout
```

`--legacy` 直接走现有 `UnrealEditor` 启动路径，不创建聚焦外壳，保留完整 Editor 和九页签 Studio。

## Decisions

### D1. 聚焦外壳使用自定义顶层窗口，而不是删除引擎功能

Unreal Editor 的主界面不是单一可替换控件。可行方案有三类：

1. **自定义顶层 `SWindow` + `SEditorViewport` + `FLevelEditorViewportClient`**（首选）。
   - 启动后隐藏 `GEditor` 的 stock main frame，创建 Epicode 自己的 `SWindow`。
   - 视口使用 `SEditorViewport` 承载 `FLevelEditorViewportClient`，复用 UE 的选择、gizmo、网格、视图模式和实时渲染。
   - 菜单、库、检查器、对话和状态栏都是自定义 Slate 控件。
   - 优点：视觉上完全脱离 stock 编辑器；保留编辑器的世界和 gizmo。
   - 风险：需要验证 UE 5.8 的 `FLevelEditorViewportClient` 构造、输入路由、编辑器世界生命周期和退出顺序。

2. **全屏 nomad 页签 + 视口重挂接**（回退）。
   - 注册一个全屏 `EpicodeFocusedEditor` nomad 页签，内部承载 `SLevelViewport` 和自定义控件。
   - 关闭其他页签，隐藏菜单栏/工具栏/状态栏。
   - 优点：实现成本低，复用 Level Editor 现有视口。
   - 风险：stock 菜单/工具栏/页签可能仍可通过快捷键或边缘入口出现，需要额外的输入和菜单拦截。

3. **`-game` + 自定义视口/gizmo**（最后回退）。
   - 不启动 Editor UI，使用自定义 Slate/UMG 视口和自研选择/变换 gizmo。
   - 优点：完全可控。
   - 风险：失去 Editor 的 gizmo、选择、撤销和资产编辑能力，成本最高。不作为首选。

**决策**：先做 D1 方案 1 的 spike；如果 5.8 的视口构造或世界生命周期不可行，退回方案 2。方案 3 只作为明确失败后的备选，不在本变更默认路径。

**PoC 结果（2026-11-15，UE 5.8.4）**：方案 1 可行，采用 `SWindow + SLevelViewport + FLevelEditorViewportClient`。`SLevelViewport` 承载 stock Level Editor 视口客户端，因此选择、变换 gizmo、网格和撤销都复用引擎实现。PoC 发现并处理了四个 5.8 限制：

- `OnEditorInitialized` 早于 `FLevelEditorModule::GetFirstLevelEditor()` 可用；模块用 0.1 秒 ticker 重试，最多 30 秒，不静默回退 stock 界面。
- 独立 `SLevelViewport` 没有 `ParentLayout` 时，`SLevelViewport::IsInForegroundTab()` 返回 false，视口不会持续 tick；聚焦视口子类覆盖 `IsVisible()` 走 `SEditorViewport::IsVisible()`。
- UE 5.8 默认使用新 TRS gizmo，但独立视口没有可用的 active editor gizmo，legacy widget 又因 `AreEditorGizmosAllowed()` 为真而不绘制。聚焦模式在 `OnEditorPreExit` 前临时设置 `GLevelEditorModeTools().SetSupportsViewportITF(false)`，强制 legacy transform widget；退出前恢复原值。该设置只影响当前进程的聚焦模式，不修改 `--legacy`，也不写配置。
- stock 主窗口用 `SWindow::HideWindow()` 隐藏而不是销毁；关闭聚焦窗口时恢复 stock 窗口。`SupportsViewportITF` 必须在 `FEditorDelegates::OnEditorPreExit` 恢复，不能在 `ShutdownModule` 里调用 `GLevelEditorModeTools()`，否则模块卸载顺序会触发 ensure。

PoC 证据：`Saved/Logs/editor-shell-poc.log`、`Saved/Screenshots/EditorShellPoC-translate.png`、`-rotate.png`、`-scale.png`。选择 `PlayerStart0` 后，移动/旋转/缩放 gizmo 均可见；事务移动后 `GEditor->UndoTransaction()` 将位置从 `(154.656, 79.013, 208.000)` 恢复到 `(54.656, 79.013, 208.000)`，`pass=true`；关闭聚焦窗口后 stock 编辑器恢复并进程退出，无残留。

### D2. “删除通用功能”是界面级移除，不是引擎级删除

不 fork 引擎的前提下，无法真正移除 Unreal Editor 的模块和底层功能。实现采用：

- 隐藏 stock main frame、菜单栏、工具栏、主标签、状态栏和默认窗口。
- 关闭/不注册 Content Browser、Details、World Settings、Blueprint Editor、Material Editor、Sequencer、Output Log 等默认页签。
- 拦截并禁用通用快捷键：Content Browser、Blueprint、Sequencer、PIE、Simulate、Build、Source Control、插件/项目设置等。
- 只保留 Epicode 菜单和必要快捷键：选择、移动、旋转、缩放、删除、撤销/重做、保存、聚焦、框选。
- 提供 `--legacy` 维护入口，保留完整 Editor；它不是日常产品界面。
- 在文档和诊断中明确：通用功能仍在底层，只是不可从聚焦界面进入。

### D3. 主菜单按 Epicode Studio 能力组织

| 菜单 | 内容 |
| --- | --- |
| 场景 | 新建、打开、保存、另存为、关闭、最近场景；从本地库导入场景；从本地库删除场景 |
| 角色 | 从本地库放置角色；导入角色到本地库；从本地库删除角色；删除选中角色 |
| 物品 | 从本地库放置物品；导入物品到本地库；从本地库删除物品；删除选中物品 |
| 灯光 | 放置方向光、点光、聚光灯、矩形光；删除选中灯光 |
| 摄像 | 放置相机；设为活动相机；删除选中相机 |
| 本地库 | 场景库、角色库、物品库的浏览/搜索/刷新/定位；导入；删除 |
| 编辑 | 撤销、重做、选择、移动、旋转、缩放、删除、复制变换？ |
| 视图 | Lit/Unlit/Wireframe、网格、显示标志、聚焦选中、框选全部、相机速度 |
| 对话 | 显示/隐藏对话、新建会话、清空、重试、设置 |
| 帮助 | 快捷键、关于、诊断信息 |

不放入主菜单：环境、渲染队列、API 状态、Content Browser、蓝图、材质、Sequencer、地形、植被、PIE、插件、项目设置。旧 Studio 的 Environment/Render/API 页签只在 `--legacy` 保留。

### D4. 只保留四类实体的最小编辑面

允许：

- **放置**：从本地库放置角色/物品；从菜单放置灯光/相机。
- **选择**：视口点选、框选、最小对象列表选择。
- **变换**：移动、旋转、缩放；世界/局部坐标；网格吸附；数值输入。
- **属性**：
  - 角色：骨骼网格、动画、播放/暂停、动画速度、可见性。
  - 物品：静态网格、材质槽覆盖、物理开关、可见性。
  - 灯光：类型、强度、颜色、衰减、阴影开关。
  - 摄像：视场角、焦距、光圈、对焦距离、活动相机。
- **删除**：从当前场景删除；不删除源资产。
- **撤销/重做**：所有编辑通过命令注册表和编辑器事务。

不允许：层级重挂接、分组、复制实体（除非后续明确需要）、环境编辑、材质图、蓝图、动画蓝图、Sequencer、地形、植被、物理约束、导航、PIE、渲染队列、通用资产导入器 UI。

最小对象列表只用于选择和管理当前场景中的四类实体；它不是完整 Outliner，不提供层级拖放、批量重挂接或任意 Actor 编辑。

### D5. 本地库是索引 + 受控导入，不是 Content Browser

本地库由三部分组成：

| 库 | 来源 | 默认根 |
| --- | --- | --- |
| 场景库 | Epicode `scene.json` | `Saved/Epicode/Scenes` |
| 角色库 | 骨骼网格资产 | `/Game/Epicode/Imported/Characters` + 配置根 |
| 物品库 | 静态网格资产 | `/Game/Epicode/Imported/Props` + 配置根 |

索引写入 `Saved/Epicode/Library/index.json`，条目包含：

- `id`、`kind`、`name`、`assetPath`、`sourcePath`、`sourceHash`、`tags`、`thumbnailPath`、`createdAt`、`updatedAt`。
- 场景条目额外包含 `sceneId`、`levelPath`、`entityCount`、`modifiedAt`。
- 索引是缓存，不是真相源；启动时可从资产注册表和场景库重建。

导入：

- 场景：读取 `.json` 场景文件，校验 schema，复制/写入 `Saved/Epicode/Scenes/<sceneId>/scene.json`，或通过 `scene.open` + `scene.saveAs` 导入。
- 角色：走现有 Interchange 导入，目标 `/Game/Epicode/Imported/Characters/<name>`，要求包含骨骼网格；否则拒绝并提示改用物品。
- 物品：走现有 Interchange/OBJ 导入，目标 `/Game/Epicode/Imported/Props/<name>`，要求静态网格。
- 导入期间显示任务进度，不阻塞 UI；失败保留源文件，不留下半成品条目。

删除：

- 场景：调用 `scene.delete` 移入 `Saved/Epicode/Trash/Scenes/<timestamp>/`，不永久删除。
- 角色/物品：默认只移除库索引条目，不删除 `.uasset`；如果用户显式勾选“同时删除资产文件”，二次确认后删除，并检查引用。
- 删除当前打开的场景或正在被场景引用的资产时拒绝，并显示引用原因。

放置：

- 角色/物品从库拖到视口或点击“放置”，在视口中心或鼠标位置生成实体。
- 灯光/相机从菜单放置，使用默认参数。
- 放置后自动选中，并进入移动模式。

### D6. 对话后端使用 Python sidecar，DeepSeek Harness SDK 不直接耦合 UI

已确认 DeepSeek Harness SDK 是 Python 包 `deepseek-harness-sdk`，不是 C++ 库。Unreal Editor 不应直接嵌入 Python 解释器：PythonScriptPlugin 未默认启用，直接把第三方包加载进 Editor 会增加 ABI、GIL、崩溃隔离和打包风险。因此采用 **Python sidecar + C++ 客户端**：

```text
Unreal EpicodeAssistant
  <-> stdio JSONL（默认）或 127.0.0.1 HTTP
  <-> Scripts/Assistant/DeepSeekHarnessSidecar.py
  <-> deepseek-harness-sdk（隔离 venv）
  <-> DeepSeek 服务
```

定义 C++ 侧 `IConversationBackend`：

```cpp
class IConversationBackend
{
public:
    virtual ~IConversationBackend() = default;
    virtual void Send(const FConversationRequest& Request, FOnConversationEvent OnEvent) = 0;
    virtual void Cancel(const FString& RequestId) = 0;
    virtual bool IsAvailable(FString& OutError) const = 0;
};
```

实现：

- `FDeepSeekHarnessSidecarBackend`（默认）：Unreal 启动/复用一个 Python sidecar 子进程，通过 `FPlatformProcess` 管道发送 JSONL 请求，读取流式事件；sidecar 崩溃时只影响对话，不影响视口和手工编辑。
- sidecar 入口：`Scripts/Assistant/DeepSeekHarnessSidecar.py`；依赖：`Scripts/Assistant/requirements.txt`，固定 `deepseek-harness-sdk==<version>`。
- 隔离环境：优先使用 `Saved/Epicode/Assistant/venv` 或 `Scripts/Assistant/.venv`，不把 venv 提交仓库；安装失败时显示 pip 输出摘要和修复命令。
- SDK 来源：公开 PyPI 当前查不到 `deepseek-harness-sdk`，需要用户提供私有索引 URL、凭据或 wheel。实现任务在拿到来源前保持待定，不把包二进制提交仓库。
- sidecar 协议（JSONL）：
  - Unreal -> sidecar：`{"id":1,"op":"chat","messages":[...],"context":{...}}`、`{"id":1,"op":"cancel"}`、`{"op":"ping"}`、`{"op":"shutdown"}`。
  - sidecar -> Unreal：`{"id":1,"event":"delta","text":"..."}`、`{"event":"tool","name":"...","args":{...}}`、`{"id":1,"event":"done","usage":{...}}`、`{"id":1,"event":"error","code":"...","message":"..."}`。
  - 每行一个 JSON 对象，UTF-8；单行上限 1 MB；未知事件忽略并记录一次警告。
- 可选 HTTP 模式：sidecar 可监听 `127.0.0.1:8720`，提供 `/health`、`/v1/chat`（SSE）、`/v1/cancel`，使用 `Saved/Epicode/assistant-token`（0600）做本地鉴权。默认仍用 stdio，避免端口冲突和防火墙问题。
- `FOpenAICompatibleChatBackend` 作为降级：当 sidecar/SDK 不可用时，可直接调用 DeepSeek 的 OpenAI 兼容 HTTP API；它也实现同一接口，便于对照测试。
- `FMockConversationBackend`：自动化测试使用，不依赖网络和 SDK。

配置：

```ini
[/Script/EpicodeAssistant.EpicodeAssistantSettings]
bEnabled=true
Backend=deepseek-harness-sidecar
SidecarCommand=python3
SidecarScript=Scripts/Assistant/DeepSeekHarnessSidecar.py
SidecarMode=stdio
Endpoint=
Model=deepseek-chat
ApiKeyFile=Saved/Epicode/deepseek.env
MaxContextTokens=32000
MaxRepairAttempts=2
RequestTimeoutSeconds=120
SidecarIdleSeconds=300
```

密钥只从环境变量或 `Saved/Epicode/deepseek.env`（0600）读取；sidecar 继承受控环境，不把密钥写入命令行参数。日志、错误、审计只记录后端名、模型名和耗时，不记录密钥或完整 prompt 中的敏感内容。

### D7. 对话只生成编辑脚本，不执行任意代码

模型输出必须是单个 JSON 对象，包在 ```json 代码块或纯 JSON 中：

```json
{
  "schemaVersion": 1,
  "summary": "在主街放置一名角色并把相机对准他",
  "requiresConfirmation": true,
  "operations": [
    {
      "clientId": "hero",
      "method": "character.spawn",
      "params": {
        "name": "Hero",
        "skeletalMesh": "/Game/Epicode/Imported/Characters/Hero/Hero"
      }
    },
    {
      "clientId": "hero-transform",
      "method": "entity.setTransform",
      "params": {
        "id": "$hero",
        "transform": {
          "location": [0, 0, 100],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        }
      }
    },
    {
      "clientId": "camera",
      "method": "camera.create",
      "params": {
        "name": "ShotCam",
        "fieldOfView": 35
      }
    }
  ]
}
```

规则：

- `schemaVersion` 必须匹配支持版本。
- `operations` 最多 50 条；总影响实体最多 100 个。
- `method` 必须在白名单内：`character.spawn`、`character.bind`、`character.play`、`character.pause`、`character.setSpeed`、`character.setMaterial`、`prop.spawn`、`prop.create`、`prop.setMaterial`、`prop.resetMaterial`、`prop.setPhysics`、`light.create`、`light.update`、`light.delete`、`camera.create`、`camera.update`、`camera.activate`、`camera.delete`、`entity.setTransform`、`entity.setVisible`、`entity.rename`、`entity.delete`、`entity.duplicate`、`scene.save`。
- 禁止 `scene.transaction` 嵌套、`import.*`、`render.*`、`system.*`、`director.*`、`sandbox.*`、任意控制台/Python/Lua/文件/网络命令。
- `$clientId` 只能引用同一脚本中更早的 `clientId`；解析失败不应用。
- 资产路径必须以 `/Game/` 开头，且存在或出现在本地库索引中；外部路径拒绝。
- 删除、批量删除、覆盖已有实体、修改活动相机等操作默认 `requiresConfirmation=true`。
- 空操作、纯问答、无法解析为脚本时，对话只显示文本，不修改场景。

### D8. 脚本校验、预览、应用和撤销是单事务闭环

流程：

```text
用户消息
  -> 后端流式回复
  -> 提取 JSON 脚本
  -> schema 校验
  -> 方法白名单
  -> 参数类型/范围校验
  -> 资产存在性/路径校验
  -> $clientId 引用解析
  -> 生成差异预览（新建/修改/删除/警告）
  -> 用户在对话窗口点击“应用”
  -> scene.transaction 单事务执行
  -> 失败：回滚，显示领域错误
  -> 成功：一次撤销恢复全部修改，写审计日志
```

实现：

- `script.validate`：只校验，不修改场景，返回规范化脚本、影响实体、警告和错误。
- `script.apply`：重新校验后，在一个 `scene.transaction` 中执行；每个 operation 结果写入 `clientId -> entityId` 映射。
- 预览面板显示：新建实体数量、修改实体及字段、删除实体、资产路径、潜在不可逆操作。
- 破坏性操作必须二次确认；用户拒绝时不执行。
- 应用成功后写入 `Saved/Epicode/Chat/audit.jsonl`：时间、会话 ID、模型、后端、用户消息摘要、脚本、结果、耗时、是否确认；不记录密钥。
- 撤销使用现有编辑器事务；一次 `Ctrl+Z` 恢复整个脚本，而不是逐条恢复。
- 如果执行中某条失败，事务回滚，已创建实体不残留。

### D9. 上下文构建要小而准

发送给模型的内容：

- 当前场景摘要：`sceneId`、名称、`levelPath`、实体数量、四类实体列表（ID、名称、类型、变换、关键属性）。
- 当前选择：实体 ID、类型、名称、变换、属性。
- 本地库摘要：场景列表、角色列表、物品列表的名称、路径、标签；不发送缩略图二进制。
- 命令 schema：允许的方法、参数类型、单位、坐标约定、示例。
- 约束：不修改环境/渲染/网络/文件；不删除库资产；不创建不在库中的资产；最多操作数。
- 用户消息和最近 N 轮对话。

不发送：

- API 令牌、DeepSeek 密钥、文件系统路径（除库路径的 `assetPath`）、完整 `.uasset`/贴图/网格二进制。
- 未选择的私有数据、其他会话数据。

上下文超出预算时，优先保留当前场景/选择/命令 schema，裁剪历史对话和库列表；裁剪发生在本地，不把超限内容发给模型。

### D10. 旧版兼容和迁移

- `Editor.sh` 默认加 `-EpicodeFocusedEditor`；`--legacy` 不加。
- 聚焦模式下 `EpicodeStudio` 默认不注册九页签，只保留底层服务；`--legacy` 注册现有页签。
- 旧 Studio 的 Environment/Render/API 不作为聚焦菜单项；需要时通过 `--legacy` 使用。
- 主 specs 的 `production-ui` 用 MODIFIED 记录默认模式切换；旧需求保留为 legacy 行为。
- 本变更不删除旧代码；先保证聚焦模式稳定，再在后续变更归档旧 UI。

## 2.x 聚焦工作台实施结果

2026-11-15 已完成 2.1–2.9（含 2.3 场景文件导入和 2.4 角色/物品本地库）。实现仍坚持“界面级隐藏、复用命令注册表、保留 `--legacy`”的约束。

### 布局与面板

- 根控件使用垂直 `SVerticalBox`：顶部菜单栏、中间 `SSplitter` 三栏、底部状态栏。
- 左栏包含对象列表、搜索框、类型过滤按钮、资源库列表、本地库场景列表和新场景名输入；右栏是对话占位、输入框和禁用的发送按钮。
- 左右栏折叠通过 `SSplitter::Slot` 的动态 `Value`/`MinSize` 加 `SBox::Visibility` 实现；中间视口使用 `Value = 1 - left - right` 自动扩展。
- 面板宽度、折叠状态、最近场景 ID 和视口镜头写入 `Saved/Epicode/Editor/layout.json`。使用 `FJsonSerializer` 读写，槽位 `OnSlotResized` 标脏，`Tick` 节流 0.5 秒保存，析构时再保存一次。
- 视口镜头通过 `FLevelEditorViewportClient::GetViewLocation/GetViewRotation` 读取，启动时 `SetViewLocation/SetViewRotation` 恢复；相机速度使用 UE 5.8 的 `FEditorViewportCameraSpeedSettings` 新 API，避免已废弃的整数设置接口。

### 菜单与对象

- 菜单固定为场景、角色、物品、灯光、摄像、本地库、编辑、视图、对话、帮助；不注册环境、渲染、API、Content Browser、蓝图、材质、Sequencer、地形、植被、PIE、插件和项目设置入口。
- 场景菜单：新建、打开、最近场景、保存、另存为、关闭；最近场景按 `layout.json` 的顺序优先，场景删除走 `scene.delete` 回收站并二次确认。
- 灯光菜单：点光、聚光、方向光、矩形光，调用 `light.create`；摄像菜单创建/激活/删除相机。
- 角色/物品菜单：从本地库放置、从文件导入到本地库、从本地库删除（只移除索引）、删除选中实体。放置调用 `character.spawn`/`prop.spawn`，自动选中并切到移动模式；删除索引写入 `Saved/Epicode/Editor/asset-library.json`，不删除 `.uasset`，并可从本地库菜单恢复。
- 对象列表通过 `entity.list`/`FEpicodeSceneSubsystem::GetDocument` 的数据展示，支持名称搜索和类型过滤；选择写回 `GEditor` 与 `SEditorViewport`，视口选择每 0.25 秒同步回列表。
- 所有写操作走 `FEpicodeEngineSubsystem::ExecuteCommand`，不直接改 Actor；命令成功后刷新模型、列表和状态栏。

### 场景文件导入与本地库资产

- 场景菜单“从文件导入场景...”用 `IDesktopPlatform::OpenFileDialog` 选择 `*.json`，先经 `FEpicodeSceneLibrary::Load` 预检并补全空 `sceneId`/`name`，再写入场景库；重复 `sceneId` 询问覆盖，导入后标记最近场景并询问是否打开。
- 角色/物品菜单“导入角色/物品到本地库...”用文件对话框选择 FBX/GLTF/GLB 或 FBX/OBJ/GLTF/GLB，调用 `import.file`（`autoPlace=false`）；导入后用 `UPackage::SavePackage` 把 Interchange 生成的所有资产包写入 `Content/Epicode/Imported/{character,prop}/`，避免只存在于内存、重启后库为空。
- Interchange 同步导入即使 `autoPlace=false` 仍会为当前场景创建实体；导入完成后立即用返回的 `entityIds` 调用 `entity.delete` 清理这些实体，若导入前场景是干净的再 `scene.save` 恢复干净标记，保证“导入到库”不会意外改场景。
- 资源库通过 `FAssetRegistryModule` 递归扫描 `UEpicodeSettings::ImportContentRoot`（默认 `/Game/Epicode/Imported`），`USkeletalMesh` 标为角色、`UStaticMesh` 标为物品，按名称排序。列表项单击放置，菜单也可按类型展开资产子菜单。
- “从本地库删除角色/物品”只把资产路径加入 `Saved/Epicode/Editor/asset-library.json` 的 `hidden` 数组并刷新列表；资产文件和引用它的场景实体保持不变；“本地库 > 恢复隐藏资源”清空该数组。

### 快捷键与拦截

- 通过 `SLevelViewport::GetCommandList()` 的子命令列表覆盖通用命令：`Q/W/E/R`（`FEditorViewportCommands`）、`F`（`FocusViewportToSelection`）、`Delete`（`FGenericCommands`）、`Ctrl+Z`/`Ctrl+Shift+Z`/`Ctrl+Y`（Undo/Redo）、`Ctrl+S`（`FLevelEditorCommands::Save`）。
- `Delete` 映射到 `entity.delete`，避免 stock `edactDeleteSelected` 绕过 Epicode 文档；`Ctrl+S` 映射到 `scene.save`。
- `FLevelViewportCommands::FocusViewportToSelectedActors` 在创建早期可能为空，已改为 `FEditorViewportCommands::FocusViewportToSelection`，并在 `MapAction` 前检查命令指针，避免 `EnsureFailed`。
- 快捷键 smoke 直接调用 `FUICommandList::ProcessCommandBindings` 验证绑定，不依赖真实键盘事件；结果记录在 `Saved/Logs/editor-shell-workbench.log`。

### 状态栏与空状态

- 状态栏显示当前场景、选择实体、变换模式、库场景数、对话后端状态、`revision`、保存状态和最近一条命令结果。
- 无场景时在视口中央显示新建/打开入口；空场景对象列表提示创建灯光或相机；库为空、对话不可用、视口初始化失败均有文本提示；视口失败页提供“重试”按钮和 `--legacy` 提示。`CreateViewport()` 已从 `Construct` 拆出，重试时把新 `SLevelViewport` 塞回 `SBox` 并重新绑定快捷键/镜头。

### 验证证据

- 构建：`engine/Scripts/Build.sh`，`Result: Succeeded`；插件二进制 sha256 `21367bdb69b32fa68a3d2244fdff8f399bbe9bd1a742ed825614f841fbe377de`，大小 777032 bytes。
- 工作台截图：`Saved/Screenshots/EditorShellWorkbench.png`（1600x868，sha256 `23b705b0...abc4a76`）、折叠布局 `EditorShellCollapsed.png`（`efbcce73...5656cf`）、菜单 `EditorShellMenu.png`（`82f3fd58...3353247`）、菜单 popup `EditorShellMenuPopup.png`（137x194，`ae19adcb...da84b9fa`）。
- 菜单实际打开：日志 `FocusedEditor menu smoke: label=场景 open=true`；Slate 主窗口截图不含 popup，使用 `WriteActiveWindowScreenshot` 抓取 137x194 菜单窗口。
- 场景文件导入：`-EpicodeFocusedEditorSceneImportSmoke=/tmp/epicode-import-test/scene.json` 日志 `FocusedEditor scene import smoke: success=true`，截图 `EditorShellSceneImport.png`（1600x868，`0a5d6f17...0395d10`）；场景库出现 `Imported Scene Test`。
- 本地库资产：`-EpicodeFocusedEditorAssetLibrarySmoke=.../cube.fbx -EpicodeFocusedEditorAssetLibraryCharacterSmoke=.../skeletal.fbx` 日志依次 `assets=2`、`placed=true entities=2`、`characterPlaced=true entities=3`、`hidden=true assets=1`、`restored=true assets=2`；截图 `EditorShellAssetLibrary.png`（1600x868，`aa2dd9ea...8e77ab7`）。
- 资产持久化：首次导入后 `Content/Epicode/Imported` 生成 16 个 `.uasset`（约 932K）；重启后 workbench smoke 资源库仍显示 `cube`（物品）和 `skeletal`（角色）共 2 项，截图即上面的工作台截图。
- 快捷键：W/E/R/Q/F 均 `handled=true`；Delete 后实体数 4 -> 3，Ctrl+Z 后 3 -> 4；Ctrl+S `handled=true`；无 `EnsureFailed`。
- 布局恢复：把 `layout.json` 改为左右折叠、宽度 0.30/0.15 后重启，日志 `layout loaded: left=false right=false leftWidth=0.300 rightWidth=0.150 recent=2 camera=true`；截图 `EditorShellLayoutLoad.png`（`d70d99c7...c25b53f7`）。
- 视口重试：`-EpicodeFocusedEditorViewportRetrySmoke` 先注入创建失败，1 秒后重试；日志 `viewport retry smoke: ready=true`，截图 `EditorShellViewportRetry.png`（`4d17a5d2...3995e9c1`）。
- 证据全文：`engine/Saved/Logs/editor-shell-workbench.log`。

### 已知限制

- 本地库删除只移除索引，不删除 `.uasset`；显式删除资产文件、当前/已保存场景引用检查留给任务 4.7。
- 放置目前用视口中心前方 400cm；拖放到视口和鼠标交点放置留给任务 4.8。
- 资产导入是同步调用，没有进度条、取消和失败清理；大文件会阻塞界面，留给任务 4.6。
- 场景文件导入支持 `scene.json` 预检、覆盖确认和打开；完整 schema 校验、另存副本留给任务 4.5。
- 资源库暂无缩略图、搜索和标签过滤，留给任务 4.4/后续增强。
- 对话面板无后端，发送按钮禁用；任务 5.x 接入。
- 视口重试已实现并做故障注入测试；真实驱动失败仍只能由用户手动点击重试。

## Data Model

### Library index

```json
{
  "schemaVersion": 1,
  "updatedAt": "2026-11-14T17:00:00Z",
  "entries": [
    {
      "id": "char-hero-01",
      "kind": "character",
      "name": "Hero",
      "assetPath": "/Game/Epicode/Imported/Characters/Hero/Hero",
      "sourcePath": "/home/user/assets/hero.fbx",
      "sourceHash": "sha1:...",
      "tags": ["human", "lead"],
      "thumbnailPath": "Saved/Epicode/Library/Thumbs/char-hero-01.png",
      "createdAt": "2026-11-14T17:00:00Z",
      "updatedAt": "2026-11-14T17:00:00Z"
    }
  ]
}
```

### Conversation record

```json
{
  "schemaVersion": 1,
  "conversationId": "01A1...",
  "backend": "deepseek-harness",
  "model": "deepseek-chat",
  "createdAt": "2026-11-14T17:00:00Z",
  "messages": [
    {"role": "user", "content": "在主街放一个角色"},
    {"role": "assistant", "content": "...", "script": {"schemaVersion": 1, "operations": []}}
  ]
}
```

## Risks / Trade-offs

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 自定义顶层窗口/视口在 UE 5.8 不稳定 | 聚焦模式无法启动 | 先做 spike；失败退回全屏 nomad 页签；保留 `--legacy` |
| 隐藏 stock UI 不彻底 | 通用功能仍可进入 | 拦截快捷键、关闭页签、菜单白名单；验收检查常见入口 |
| DeepSeek Harness SDK 无 C++ 绑定 | 无法直接集成 | sidecar/HTTP 适配；OpenAI 兼容后端降级；Mock 测试 |
| 模型生成非法脚本 | 场景损坏 | 严格 schema/白名单/引用/资产校验；预览+确认；单事务回滚 |
| 模型幻觉资产路径 | 放置失败 | 只允许本地库索引中的资产；路径白名单；失败不应用 |
| 上下文过大 | 费用/延迟/超限 | 本地裁剪；只发摘要；库列表分页/检索 |
| 本地库删除误删资产 | 数据丢失 | 默认只删索引；场景进回收站；资产删除二次确认+引用检查 |
| 旧 Studio 与聚焦模式行为分叉 | 维护成本 | 共享命令注册表；legacy 只保留 UI；同一套测试命令 |

## Migration Plan

1. 先加 `-EpicodeFocusedEditor` 开关和 `Editor.sh --legacy`，默认仍可回退。
2. 实现聚焦外壳 PoC，验证视口、选择、gizmo、撤销、关闭流程。
3. 实现菜单、四类实体检查器、本地库和最小对象列表。
4. 实现对话面板和 Mock 后端；再接入 DeepSeek Harness SDK/sidecar。
5. 实现 `script.validate`/`script.apply`，先用手写脚本测试，再开 AI 生成。
6. 把 `Editor.sh` 默认切到聚焦模式；旧 Studio 仅 `--legacy`。
7. 补齐自动化测试、手工验收、文档和日志；评审后再归档旧 production-ui 需求。

## Testing Strategy

- **Shell 启动**：`Epicode.EditorShell.Launch` 验证默认聚焦模式、`--legacy`、stock 页签隐藏、退出后无残留窗口。
- **视口交互**：`Epicode.EditorShell.Viewport` 验证选择、移动/旋转/缩放、聚焦、框选、视图模式。
- **范围限制**：`Epicode.EditorShell.Scope` 验证 Content Browser/Blueprint/Sequencer/PIE 快捷键不可用，只有四类实体菜单。
- **本地库**：`Epicode.Library.Index`、`Import`、`Delete`、`Place` 覆盖扫描、导入、删除、冲突、路径越界和资产引用检查。
- **对话**：`Epicode.Assistant.Chat`、`BackendFailure`、`ContextBudget` 使用 Mock 后端验证流式、取消、重试、超限裁剪。
- **脚本**：`Epicode.Script.Validate`、`Apply`、`Undo`、`Reference`、`Safety` 覆盖非法方法、非法路径、循环引用、超限、部分失败回滚和一次撤销。
- **响应性**：`Epicode.EditorShell.Responsiveness` 验证菜单、库刷新、对话流式更新不阻塞 UI 超过 100 ms。
- **手工验收**：打开聚焦模式，放置/移动/改属性/删除四类实体，导入/删除库条目，对话生成脚本并预览/应用/撤销；记录截图和日志。

## Resolved Decisions

- **DeepSeek Harness SDK**：已确认是 Python 包 `deepseek-harness-sdk`，通过 Python sidecar 接入；SDK 版本、私有索引/凭据或 wheel 在实现任务 5.3 中固定。公开 PyPI 当前查不到该包，拿到安装来源前不开始真实后端集成。
- **聚焦外壳**：先做自定义 `SWindow` + `SEditorViewport` + `FLevelEditorViewportClient` PoC；失败回退全屏 nomad 页签，再失败才考虑 `-game` 自定义 gizmo。PoC 是任务 1.x 的硬门槛，不是产品可选项。
- **缩略图**：首版本地库使用文字列表和可选缩略图字段，不阻塞导入/删除/放置；缩略图生成作为后续增强。
- **场景导入**：首版只支持 Epicode `scene.json`。`.umap` 需要额外的关卡包装和引用迁移，放到后续变更，不在本变更伪装成已支持。
- **对话历史**：默认保存到 `Saved/Epicode/Chat/`，本地持久化，支持新建/清空/删除；不自动上传旧会话。
- **本地库删除**：默认只删索引，不删 `.uasset`。删除资产文件必须显式勾选、二次确认并检查当前/已保存场景引用。
- **通用 Unreal 功能**：不 fork 引擎，只做界面级隐藏和入口限制；`--legacy` 保留完整 Editor 作为维护通道。
