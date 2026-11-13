# Tasks

## 1. 项目骨架与构建基础

- [x] 1.1 创建 `engine/EpicodeVideo.uproject`、`Source/EpicodeVideo.Target.cs`、`Source/EpicodeVideoEditor.Target.cs` 和主游戏模块 `Source/EpicodeVideo/`，声明 UE 5.8 项目并启用 `MovieRenderPipeline`、`WebSocketNetworking`、`USDImporter`、`EditorScriptingUtilities`、`SequencerScripting` 和 `LevelSequenceEditor`，验证 `Scripts/env.sh --check` 能识别项目文件与 `/usr/local/unreal`
- [x] 1.2 创建 `EpicodeCore`、`EpicodeImport`、`EpicodeControlAPI`、`EpicodeStudio`、`EpicodeRender` 五个插件的 `.uplugin`、`Build.cs` 和模块启动文件，按 `design.md` 的模块类型与依赖表配置，验证每个插件目录包含可被 UnrealBuildTool 发现的模块描述
- [x] 1.3 编写 `Config/DefaultEngine.ini`、`DefaultGame.ini`、`DefaultEditor.ini` 和 `DefaultEpicode.ini`，设置默认地图、渲染后端、插件开关和 Epicode 配置节，验证编辑器以 `/usr/local/unreal` 启动时加载全部五个模块且日志列出模块名称
- [x] 1.4 实现 `UEpicodeSettings`、环境变量覆盖、场景回收目录解析和令牌文件生成，首次启动生成 32 字节随机令牌并以 POSIX `0600` 写入 `Saved/Epicode/api-token`，验证自动化测试覆盖默认值、`EPICODE_API_HTTP_PORT` 覆盖、`SceneTrashRoot` 和令牌权限
- [x] 1.5 实现启动诊断 `FEpicodeDiagnostics`，检查引擎版本不低于 5.8、必需插件已加载、渲染后端可用和输出目录可写，验证自动化测试覆盖版本不匹配、目录不可写和正常状态
- [x] 1.6 编写 `Scripts/env.sh`、`Build.sh`、`Editor.sh`、`Headless.sh`、`Test.sh`、`Clean.sh`，默认 `UE_ROOT=/usr/local/unreal`，验证 `Build.sh` 构建 `EpicodeVideoEditor Linux Development` 成功，且 `UE_ROOT` 指向无效目录时在构建前失败
- [x] 1.7 编写 `Docs/Operations.md`，记录构建、启动、无界面运行、测试、清理和 `UE_ROOT` 覆盖方法，验证文档中的每条命令可原样执行
- [x] 1.8 增加 `engine/README.md`，说明项目定位、目录结构、快速开始和首版限制，验证链接到 `Docs/Operations.md`、`Docs/API.md`、`Docs/SceneModel.md`、`Docs/Import.md`、`Docs/Studio.md` 和 `Docs/Rendering.md` 的文件路径均存在

## 2. 核心领域、场景与命令

- [x] 2.1 实现 `FEpicodeEntityId`、实体类型枚举、变换和 `FEpicodeEntityRecord`，拒绝 NaN、无穷大、未知类型和重复标识，验证 `Epicode.Core.Record` 自动化测试覆盖序列化往返与非法变换
- [x] 2.2 实现 `FEpicodeSceneDocument` 的 `schemaVersion`、场景元数据、实体数组、环境和活动相机序列化，加入 `FEpicodeSceneMigration` 链，验证 `Epicode.Core.SceneDocument` 测试覆盖保存往返、未来版本拒绝和旧版本迁移
- [x] 2.3 实现场景树操作：创建、删除、复制、重命名、重挂接、级联删除和同级重名检查，验证 `Epicode.Core.Hierarchy` 测试覆盖父子循环拒绝、复制生成新标识和世界变换保持
- [x] 2.4 实现 `FEpicodeCommandRegistry`、参数校验辅助、`FEpicodeCommandResult`、命令上下文和 `IEpicodeTransactionSink` 接口，验证 `Epicode.Core.CommandRegistry` 测试覆盖缺失字段、类型错误、未知方法和失败不改状态
- [x] 2.5 实现 `FEpicodeEventBus`、单调序号、批量事件、订阅者队列和不可变 `EpicodeSceneSnapshot` 原子交换，验证 `Epicode.Core.Events` 测试覆盖事件顺序、慢订阅者丢弃和快照读取不观察中间状态
- [x] 2.6 实现 Actor 投影：`AEpicodeGroupActor`、`AEpicodeCharacterActor`、`AEpicodePropActor`、`UEpicodeEntityLinkComponent`、灯光 Actor 工厂和相机 Actor 工厂，验证 `Epicode.Core.Projection` 测试覆盖创建、变换、父子挂接、可见性和销毁
- [x] 2.7 实现角色动画控制、道具材质槽覆盖、物理开关、骨骼插槽挂接和解除挂接，验证 `Epicode.Core.Character` 与 `Epicode.Core.Prop` 测试覆盖播放、跳转、材质恢复和挂接后跟随
- [x] 2.8 实现 `FEpicodeEnvironmentController`，管理天空大气、太阳、指数高度雾、体积云和后处理体积，提供晴天、阴天、黄昏、夜晚四个预设，验证 `Epicode.Core.Environment` 测试覆盖时间设定、预设原子应用和参数越界
- [x] 2.9 实现相机创建、激活、删除、焦距与视场角换算、最后一台相机删除时自动创建默认相机，验证 `Epicode.Core.Camera` 测试覆盖活动相机切换和无相机保护
- [x] 2.10 实现场景生命周期：新建、打开、保存、另存为、关闭、未保存状态和确认流程，Editor 持久化钩子同时保存关卡与场景 JSON，验证 `Epicode.Core.SceneLifecycle` 测试覆盖保存往返、写入失败保留旧文件和未保存关闭阻止
- [x] 2.11 实现 `EpicodeStudio` 中的 `IEpicodeTransactionSink` 适配器，接入 `GEditor` 事务、撤销、重做和撤销后投影重建，验证 `Epicode.Core.Undo` 测试覆盖创建、删除、变换和环境预设的一次撤销
- [x] 2.12 注册实体、角色、道具、灯光、环境、相机和 `scene.transaction` 命令处理器，批量命令预检后原子应用并在异常时恢复记录快照，验证 `Epicode.Core.Commands` 测试覆盖单命令失败无副作用和批量回滚
- [x] 2.13 实现 `FEpicodeSceneLibrary` 和 `scene.list`、`scene.switch`、`scene.delete` 命令，扫描场景目录、标记当前场景和损坏条目，切换走未保存保护，删除移动到 `Saved/Epicode/Trash/Scenes/` 并拒绝删除当前场景，验证 `Epicode.Core.SceneLibrary` 测试覆盖列表、切换、删除、当前场景拒绝和损坏条目
- [x] 2.14 实现 `character.spawn` 和 `prop.spawn` 命令，从现有骨骼网格、动画和静态网格资产创建实体且不复制资产，校验资产类型并保留源资产，验证 `Epicode.Core.AssetSpawn` 测试覆盖添加角色、添加道具、类型不匹配和删除后资产仍存在
- [x] 2.15 编写 `Docs/SceneModel.md`，记录场景 JSON 结构、实体类型、场景库、坐标约定、命令列表和撤销语义，验证文档中的示例 JSON 能被 `scene.open` 解析且场景库示例能被 `scene.list` 返回

## 3. 资产导入

- [x] 3.1 实现 `FEpicodeImportClassifier` 和 `FEpicodeImportCapabilities`，按扩展名与源内容分类场景、角色、道具，并探测 FBX、glTF、OBJ、USD、`.umap`、`.uasset` 能力，验证 `Epicode.Import.Classify` 测试覆盖骨骼 FBX、单网格 glTF、OBJ、未知扩展名和 USD 不可用
- [x] 3.2 实现 Interchange 导入后端，配置 `FImportAssetParameters` 的自动化模式、目标内容路径、导入关卡和完成回调，验证导入 `Tests/Fixtures/cube.gltf` 后生成静态网格和材质资产
- [ ] 3.3 实现 OBJ 回退工厂、`.umap` 加载和 `.uasset` 资产解析路径，验证 `Tests/Fixtures/quad.obj` 导入成功且 `Tests/Fixtures/simple.umap` 能作为当前场景打开
- [ ] 3.4 实现导入选项：目标目录、统一缩放、旋转偏移、碰撞、材质导入、自动放置、组合多网格和类型覆盖，拒绝非法目录和超出大小上限的文件，验证 `Epicode.Import.Options` 测试覆盖目标目录、自动放置关闭和多网格组合
- [x] 3.5 实现角色导入：骨骼、骨骼网格、动画、材质和物理资产识别，创建角色实体并绑定骨骼网格，缺少骨架时按角色导入失败并提示改用道具，验证 `Epicode.Import.Character` 测试覆盖带动画角色和缺少骨架
- [x] 3.6 实现导入索引 `Saved/Epicode/imports.json`，以 SHA-1 内容哈希记录源路径、选项和生成资产，相同内容复用资产，选项变化要求覆盖或副本，验证 `Epicode.Import.Reuse` 测试覆盖重复导入、内容变化和选项冲突
- [ ] 3.7 实现导入任务队列、异步进度、协作式取消和清理，取消后删除未完成资产与实体并保留已完成文件，验证 `Epicode.Import.Cancel` 测试覆盖取消进行中任务和关闭编辑器时的清理
- [x] 3.8 将资产导入和实体创建纳入单次编辑器事务，验证一次撤销移除该次导入创建的全部实体且保留资产
- [x] 3.9 向 `EpicodeCore` 命令注册表注册 `import.file`、`import.capabilities`、`import.cancel`，验证命令注册表测试能枚举三个方法并拒绝允许根目录之外的路径
- [x] 3.10 增加 glTF、OBJ 和 FBX 夹具及导入夹具说明，验证 `Scripts/Test.sh` 中导入测试全部通过
- [x] 3.11 编写 `Docs/Import.md`，记录支持格式、分类规则、导入选项、重复导入和取消行为，验证文档中的示例导入命令可执行

## 4. 控制 API

- [x] 4.1 实现 JSON-RPC 2.0 信封解析、单个与批量请求、通知请求、标准错误码和 `data.domainCode` 映射，验证 `Epicode.API.JsonRpc` 测试覆盖解析错误、无效请求、批量顺序和通知无响应
- [x] 4.2 实现 HTTP 服务启动、停止、端口冲突处理、`POST /api/v1/rpc` 路由、内容类型校验、请求体上限和 Bearer Token 鉴权，验证 `Epicode.API.Http` 集成测试覆盖有效令牌、401、415、413 和端口冲突
- [x] 4.3 实现常量时间令牌比较、令牌文件读取、CORS 默认关闭、速率限制和请求标识，验证 `Epicode.API.Security` 测试覆盖令牌日志脱敏、非回环监听拒绝和请求过载
- [x] 4.4 实现命令分发：HTTP 线程投递游戏线程、超时、结果封装和异常到 `-32603` 的转换，验证 `Epicode.API.Dispatch` 测试覆盖正常调用、超时和内部异常不泄露堆栈
- [x] 4.5 实现 `FEpicodeIdempotencyStore` 的持久化、24 小时保留、一万条上限和相同键不同参数冲突，验证 `Epicode.API.Idempotency` 测试覆盖重放、冲突和过期
- [x] 4.6 实现 `system.health`、`system.methods`、`system.stats`，返回引擎版本、项目版本、API 版本、HTTP/WS 端点、方法结构和运行统计，验证 `Epicode.API.System` 测试覆盖版本字段、方法枚举和统计计数
- [x] 4.7 实现 WebSocket 服务、`POST /api/v1/ws-ticket`、30 秒单次票据、订阅、取消订阅、心跳、事件队列、重连补发和 `events.gap`，验证 `Epicode.API.WebSocket` 测试覆盖票据过期、重复使用、订阅过滤、序号连续和缺口
- [x] 4.8 实现场景快照查询和自上次版本的增量查询，验证 `Epicode.API.Snapshot` 测试覆盖完整快照、无变化、增量变更和过期版本要求完整快照
- [x] 4.9 注册并验证场景、场景库、从资产创建角色/道具、实体、灯光、环境、相机、导入和渲染方法的完整覆盖，验证 `Epicode.API.Contract` 测试确认每个 spec 场景至少有一个方法或作业状态对应
- [x] 4.10 实现 API 启动诊断和制作界面可见状态所需的 `system.health` 降级字段，验证端口占用、令牌缺失和输出目录不可写时健康状态与日志一致
- [x] 4.11 编写 `Docs/API.md`，记录鉴权、JSON-RPC 示例、批量请求、错误码、幂等键、WebSocket 票据和订阅示例，验证文档中的 curl 与 WebSocket 示例在 `Scripts/Smoke.sh` 中可执行

## 5. 制作界面

- [ ] 5.1 注册 `FGlobalTabmanager` nomad 页签、Epicode 菜单和工具栏入口，创建可停靠的 `SEpicodeWorkspace`，左侧管理栏包含场景、角色、道具、灯光、环境和导入六个入口，底部包含渲染和 API 页签，验证 `Epicode.Studio.Workspace` 自动化测试能打开页签、切换六个入口且重复打开只聚焦已有实例
- [ ] 5.2 实现 `FEpicodeStudioViewModel`，订阅事件总线、维护当前场景快照、场景库、管理列表、选择集和面板刷新标记，验证 `Epicode.Studio.ViewModel` 测试覆盖实体增删改后的增量刷新、管理列表与大纲选择同步和选中实体属性变化
- [ ] 5.3 实现导入面板：多选文件对话框、拖放、类型覆盖、导入选项、任务队列、进度、取消、失败详情和结果定位，验证通过面板导入 `cube.gltf` 后队列显示完成且能定位实体
- [ ] 5.4 实现场景大纲：树形层级、名称搜索、类型过滤、多选、展开折叠、重命名、删除、复制、显示隐藏和拖放重挂接，验证 `Epicode.Studio.Outliner` 测试覆盖搜索过滤、多选删除和拖放层级
- [ ] 5.5 实现属性检查器：按类型显示变换、动画、材质、碰撞、灯光、环境和相机字段，非法输入在控件旁报错且不修改场景，外部 API 修改后刷新但不覆盖未提交输入，验证 `Epicode.Studio.Inspector` 测试覆盖单类型、混合类型、非法输入和外部同步
- [ ] 5.6 实现场景管理界面：场景列表、当前场景标记、新建、导入、切换、删除、刷新和定位，切换走保存/放弃/取消保护，删除走二次确认和回收目录，验证 `Epicode.Studio.SceneManager` 测试覆盖列表、切换、导入、删除和未保存保护
- [ ] 5.7 实现角色管理界面：当前场景角色列表、骨骼网格资产选择、添加角色、导入角色、删除角色、搜索过滤和选中定位，删除只移除实体并支持撤销，验证 `Epicode.Studio.CharacterManager` 测试覆盖添加、导入、删除和筛选
- [ ] 5.8 实现道具管理界面：当前场景道具列表、静态网格资产选择、添加道具、导入道具、删除道具、可见性切换、材质入口和搜索过滤，验证 `Epicode.Studio.PropManager` 测试覆盖添加、删除、可见性和撤销
- [ ] 5.9 实现灯光管理界面：灯光列表、类型和强度显示、创建方向光/点光/聚光/矩形光、删除、快速开关、强度调整、类型过滤和选中定位，验证 `Epicode.Studio.LightManager` 测试覆盖创建四种类型、删除和过滤
- [ ] 5.10 实现环境管理界面：时间、太阳、天空、雾、云、曝光、泛光和 LUT 控件，晴天/阴天/黄昏/夜晚预设，实时预览、应用、恢复默认和当前预设显示，验证 `Epicode.Studio.EnvironmentManager` 测试覆盖预设应用、时间调整、非法输入和恢复默认
- [ ] 5.11 实现渲染面板：相机或序列选择、帧范围、分辨率、帧率、格式、目录、预设、提交、取消、重试、进度和打开输出目录，验证提交三帧渲染后队列状态、进度和完成操作可用
- [ ] 5.12 实现 API 面板：HTTP/WS 状态、端口、版本、鉴权状态、连接数、请求统计、最近日志、令牌掩码、启动停止和测试请求，验证服务运行、端口冲突和 `system.health` 测试请求三条路径
- [ ] 5.13 实现长任务响应性：导入、渲染、场景切换和 HTTP 请求不阻塞界面超过 100 毫秒，关闭工作区后任务继续，重新打开恢复进度，验证 `Epicode.Studio.Responsiveness` 测试或可重复的手工验收记录
- [ ] 5.14 实现空状态和错误恢复：无场景、场景库为空、无选择、管理列表为空、空队列、API 未启动和面板初始化失败时显示明确操作入口，验证每种状态在自动化或手工验收中可重现
- [x] 5.15 编写 `Docs/Studio.md`，记录工作区打开方式、六个管理入口、场景切换/导入/删除、角色添加/删除/导入、道具/灯光/环境管理、渲染提交和 API 诊断，验证文档步骤在编辑器内逐条可执行

## 6. 视频渲染

- [x] 6.1 实现渲染作业模型、状态机、预设、校验和队列，默认并发一，拒绝非法帧范围、无相机、输出不可写和磁盘不足，验证 `Epicode.Render.Job` 测试覆盖六种状态和四类校验失败
- [x] 6.2 实现场景快照写入 `request.json`、工作进程启动参数、`-EpicodeRenderWorker=` 检测、世界等待 ticker 和启动超时，验证工作进程能在无界面 Editor 中读到请求并写初始 `progress.json`
- [x] 6.3 实现工作进程场景重建：清空默认 Actor、按场景 JSON 创建实体、环境和相机，验证 `Epicode.Render.Rebuild` 测试比较重建后的实体数量、标识、变换和活动相机
- [x] 6.4 实现 MRQ 队列与设置：相机或关卡序列绑定、输出设置、抗锯齿、时间采样、运动模糊、路径追踪开关和预设快照，验证 `Epicode.Render.MRQ` 测试提交三帧 PNG 渲染并生成输出
- [ ] 6.5 实现自定义 `UEpicodeMoviePipelineExecutor`，在状态和帧进度变化时写 `progress.json`，完成或失败写 `result.json`，验证父进程轮询能收到百分比、当前帧和终态
- [x] 6.6 实现父进程作业监控、取消、重试、超时和强制终止，取消后不发布最终目录，验证 `Epicode.Render.Cancel` 与 `Epicode.Render.Timeout` 测试覆盖排队取消、运行中取消和取消超时
- [x] 6.7 实现 PNG、EXR、JPEG 和 MP4 输出，图像序列按帧号补零命名，MP4 使用配置帧率，MP4 编码器缺失时给出可操作错误，验证各格式至少一次输出成功且 MP4 时长与帧数匹配
- [x] 6.8 实现产物验证和原子发布：校验帧数、每帧分辨率、文件非空、MP4 时长，写入 `job.json` 和校验和，验证 `Epicode.Render.Verify` 测试覆盖成功、帧数不符和分辨率不符
- [x] 6.9 实现无界面渲染、GPU 能力诊断和失败分类，GPU 设备丢失、显存不足、磁盘写满和资产缺失均返回可重试标记和诊断，验证 `Epicode.Render.Diagnostics` 测试与 `Scripts/Smoke.sh` 无界面三帧渲染
- [x] 6.10 编写 `Docs/Rendering.md`，记录渲染流程、预设参数、输出目录、作业状态、取消、重试、无界面渲染和故障排查，验证文档中的三帧渲染命令可执行

## 7. 集成验收

- [x] 7.1 编写 `Scripts/Smoke.sh`，启动无界面服务、获取令牌、调用健康检查、导入 glTF 和 OBJ、创建两个场景并切换和删除、添加角色和道具、创建灯光相机、应用环境预设、提交三帧 PNG、验证输出并退出，验证脚本返回零且输出目录包含三帧和 `job.json`
- [x] 7.2 运行 `Scripts/Test.sh` 执行全部 `Epicode.*` 自动化测试，验证 Core、Import、API、Studio、Render 测试组全部通过且无跳过项未说明
- [ ] 7.3 执行 UI 响应性验收：导入大文件、提交渲染、编辑场景和测试 API 请求同时进行，确认界面可交互且任务状态正确，记录结果为验收证据
- [x] 7.4 执行安全检查：确认默认只监听回环、令牌文件权限 0600、日志无完整令牌、路径逃逸被拒绝、CORS 默认关闭和 Shipping 不启动 API，记录检查结果
- [x] 7.5 执行干净构建验收：删除 `engine/Binaries`、`engine/Intermediate`、`engine/Saved` 后重新运行 `Scripts/Build.sh`、`Scripts/Test.sh` 和 `Scripts/Smoke.sh`，确认全部通过
- [x] 7.6 检查所有文档与实现一致：`Docs/API.md` 的方法列表与 `system.methods` 一致，`Docs/Operations.md` 的脚本参数与 `--help` 一致，`Docs/Rendering.md` 的预设与实现一致，`Docs/Studio.md` 的六个管理入口与工作区一致
- [x] 7.7 汇总验收证据到 `Docs/Acceptance.md`，包含构建日志路径、测试结果、冒烟渲染产物路径、安全检查和已知限制，验证每个 spec 能力都有对应证据
- [ ] 7.8 逐项验收界面管理能力：场景切换、导入和删除，角色添加、删除和导入，道具、灯光和环境管理界面，确认界面操作结果与 API 查询一致，记录截图或自动化日志作为验收证据

## Workflow follow-up

- 在项目评审满足后归档该 change。
- 归档后验证主 specs 的 Purpose、需求和场景都已生成，并确认 `engine/` 仍可从干净状态构建。
