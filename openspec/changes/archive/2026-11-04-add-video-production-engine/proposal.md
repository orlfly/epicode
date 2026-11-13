# Proposal

## Why

当前制作视频内容需要在 Unreal Editor 中手工完成资产导入、场景搭建、角色与灯光调整以及渲染排队，缺少一个面向视频制作的统一工具。将这套流程产品化为一个基于 Unreal Engine 5.8 的 C++ 引擎，可以让操作者通过可视化界面完成素材导入与场景编排，同时让外部系统通过稳定 API 批量控制场景、角色、道具、灯光和环境，并直接输出成片。

## What Changes

- 在 `engine/` 下新增 Unreal Engine 5.8.4 C++ 项目 `EpicodeVideo`，使用 `/usr/local/unreal` 作为引擎安装路径，并提供 Linux 构建、启动、无界面运行和测试脚本。
- 新增分层插件：核心领域模型、资产导入、控制 API、制作界面、视频渲染。
- 新增资产导入能力：通过界面文件选择器和拖放导入场景包、角色、道具，支持 FBX、glTF、OBJ 以及引擎可用的 USD 管线；自动创建资产并放置到当前场景，导入前校验、导入后可撤销。
- 新增场景编排能力：统一的场景实体模型与命令总线，提供场景库列表、切换和删除，管理场景、角色、道具、灯光、环境的创建、查询、变换、层级、可见性、材质覆盖、预设和保存加载。
- 新增控制 API：基于 HTTP 的 JSON-RPC 2.0 接口与 WebSocket 事件通道，覆盖场景、角色、道具、灯光、环境、相机与渲染命令；具备鉴权、参数校验、幂等键、结构化错误和进度通知。
- 新增制作界面：C++ Slate 编辑器工作区，包含场景管理、角色管理、道具管理、灯光管理、环境管理和导入面板，以及场景大纲、属性检查器、渲染队列和 API 状态，并与 API 共用同一套领域命令。操作者可以在界面中切换、导入和删除场景，添加、删除和导入角色，并通过各专项管理界面管理道具、灯光和环境。
- 新增视频渲染能力：集成 Movie Render Pipeline，支持相机/序列选择、渲染预设、图像序列与 MP4 输出、进度查询、取消和结果产物登记。
- 新增自动化测试与验收脚本：领域命令单元测试、API 合约测试、导入夹具测试、场景持久化往返测试和端到端渲染冒烟测试。

## Capabilities

### New Capabilities

- `engine-foundation`: UE 5.8 项目与插件骨架、模块边界、`/usr/local/unreal` 配置、Linux 构建/运行/无界面/测试脚本和产物布局。
- `asset-import`: 在界面中导入场景包、角色和道具，格式识别、导入选项、校验、资产落盘、场景放置与错误反馈。
- `scene-authoring`: 场景、角色、道具、灯光、环境、相机的领域模型、场景库切换删除、命令语义、层级与变换、持久化、撤销重做和事件。
- `control-api`: HTTP JSON-RPC 与 WebSocket 控制面，方法命名、鉴权、校验、幂等、错误码、事件与版本化。
- `production-ui`: C++ Slate 制作工作区，场景、角色、道具、灯光、环境五个专项管理入口和导入面板，大纲、检查器、渲染和 API 状态交互，以及无阻塞长任务反馈。
- `video-rendering`: Movie Render Pipeline 渲染作业，相机/序列绑定、预设、输出格式、进度、取消和产物管理。

### Modified Capabilities

无。仓库当前没有任何已存在的 spec。

## Impact

- **新增代码**：全部位于 `engine/`，包括 `.uproject`、`Source/`、`Plugins/`、`Config/`、`Scripts/`、`Tests/` 和 `Docs/`。
- **引擎依赖**：`/usr/local/unreal`（UE 5.8.4）内置模块 `HTTPServer`、`WebSocketNetworking`、`Json`、`Interchange`、`MovieRenderPipeline`、`LevelSequence`、`EditorScriptingUtilities` 等；不修改引擎安装目录。
- **运行时接口**：新增本地 HTTP 端口（默认 `127.0.0.1:8710`）和 WebSocket 端点（默认 `127.0.0.1:8711`）；新增 Bearer Token 鉴权配置和短期 WebSocket 票据。
- **平台**：首要目标为 Linux x86_64 + Vulkan；Windows/macOS 打包不在首版范围。
- **数据**：导入资产写入项目 `Content/Epicode/`；场景与渲染产物写入可配置的 `Saved/Epicode/` 目录。
- **不影响**：`agent/` 目录与现有 OpenSpec 内容；仓库中尚无既有运行时代码可被破坏。
- **明确不做**：非线性剪辑时间线、云端渲染农场、多人协同编辑、自研光栅化/路径追踪器、浏览器版制作界面。
