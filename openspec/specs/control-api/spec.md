# control-api Specification

## Purpose
为外部制作工具、自动化脚本和流水线提供稳定、可鉴权、可观测的 HTTP JSON-RPC 与 WebSocket 控制面，覆盖场景、角色、道具、灯光、环境、导入和渲染操作。

## Requirements

### Requirement: 服务生命周期

系统 SHALL 随编辑器或无界面运行模式启动控制 API，并支持通过配置或运行时命令停止和重启。默认 HTTP 监听 `127.0.0.1:8710`，WebSocket 监听 `127.0.0.1:8711`。任一端口被占用时 SHALL 返回明确错误，不得静默改用其他端口。停止服务 SHALL 在完成在途请求后关闭监听。

#### Scenario: 默认启动

- **WHEN** 引擎正常启动且端口 8710 与 8711 空闲
- **THEN** HTTP 在 `127.0.0.1:8710`、WebSocket 在 `127.0.0.1:8711` 接受连接，启动日志输出两个端点、协议版本和鉴权状态

#### Scenario: 端口冲突

- **WHEN** HTTP 或 WebSocket 端口已被其他进程占用
- **THEN** 控制 API 启动失败，日志和 `system.health` 报告占用端口及错误，制作界面显示不可用状态

#### Scenario: 运行时停止

- **WHEN** 操作者在 API 面板点击停止
- **THEN** 服务停止接受新请求，已接受的请求完成或超时后连接关闭，再次点击可重新启动

### Requirement: HTTP JSON-RPC 传输

系统 SHALL 在 `POST /api/v1/rpc` 接受 JSON-RPC 2.0 请求，支持单个请求和最多一百个请求的批量数组。系统 SHALL 仅接受 `application/json`，并返回 `application/json`。HTTP 层错误使用 4xx/5xx；已路由的 JSON-RPC 错误 SHALL 返回 HTTP 200 和标准 JSON-RPC 错误对象。

#### Scenario: 单个方法调用

- **WHEN** 客户端提交包含 `jsonrpc: "2.0"`、`method: "system.health"`、`id: 1` 的请求
- **THEN** 系统返回 HTTP 200，响应包含相同 `id`、`result` 和 `jsonrpc: "2.0"`

#### Scenario: 批量请求

- **WHEN** 客户端提交包含三个请求的数组，其中一个方法不存在
- **THEN** 系统返回三个响应元素，成功项包含结果，失败项包含方法不存在错误，顺序与请求一致

#### Scenario: 错误内容类型

- **WHEN** 客户端以 `text/plain` 提交请求体
- **THEN** 系统返回 HTTP 415，不执行任何方法

#### Scenario: 通知请求

- **WHEN** 客户端提交不带 `id` 的合法请求
- **THEN** 系统执行该方法但不返回响应体，HTTP 状态为 204

### Requirement: 鉴权

系统 SHALL 默认要求 Bearer Token 鉴权。令牌 SHALL 从环境变量、配置文件引用的文件或首次启动生成的权限为 0600 的本地文件读取。令牌比较 SHALL 使用恒定时间比较，日志与错误响应 SHALL NOT 包含完整令牌。缺失、格式错误或无效令牌 SHALL 返回 HTTP 401。

#### Scenario: 有效令牌

- **WHEN** 客户端在 `Authorization: Bearer <token>` 中提供有效令牌
- **THEN** 请求被处理并返回方法结果

#### Scenario: 缺少令牌

- **WHEN** 客户端未提供 Authorization 头
- **THEN** 系统返回 HTTP 401 和 `WWW-Authenticate: Bearer`，不执行方法

#### Scenario: 令牌日志脱敏

- **WHEN** 一次鉴权请求被记录
- **THEN** 日志只显示令牌前四位与长度，或完全脱敏，不出现完整令牌

#### Scenario: 非本机监听

- **WHEN** 配置把监听地址改为非回环地址但未显式启用远程访问
- **THEN** 服务拒绝启动并提示需要启用远程访问和配置令牌

### Requirement: 方法目录与版本

系统 SHALL 提供 `system.methods` 返回全部可用方法、参数结构、返回结构、是否需要异步作业和引入版本。API 版本 SHALL 出现在响应头或结果元数据中。新增方法 SHALL 保持向后兼容；破坏性变更 SHALL 使用新的 URL 版本。

#### Scenario: 枚举方法

- **WHEN** 客户端调用 `system.methods`
- **THEN** 返回场景、实体、角色、道具、灯光、环境、相机、导入、渲染和系统方法的名称与参数结构

#### Scenario: 未知方法

- **WHEN** 客户端调用不存在的方法 `scene.teleport`
- **THEN** 系统返回 JSON-RPC 错误码 `-32601`，错误消息包含该方法名

#### Scenario: 版本报告

- **WHEN** 客户端调用 `system.health`
- **THEN** 结果包含 `apiVersion`、`engineVersion` 和 `projectVersion`

### Requirement: 参数校验

每个方法 SHALL 在应用前按公开的参数结构校验必需字段、类型、枚举值、数值范围和未知字段策略。校验失败 SHALL 返回 JSON-RPC `-32602`，并包含字段路径和失败原因。参数校验失败 SHALL NOT 修改场景或启动作业。

#### Scenario: 缺少必需字段

- **WHEN** 客户端调用创建灯光但未提供灯光类型
- **THEN** 返回 `-32602`，指出缺失字段 `type`，场景中不新增灯光

#### Scenario: 类型错误

- **WHEN** 客户端把本应为数字的灯光强度传为字符串
- **THEN** 返回 `-32602`，指出字段路径和期望类型

#### Scenario: 未知字段

- **WHEN** 客户端提交方法结构中未定义的字段
- **THEN** 系统按该方法的严格模式拒绝请求或忽略字段，并在方法目录中声明所采用策略

#### Scenario: 范围错误

- **WHEN** 客户端设置负的动画播放速度
- **THEN** 返回 `-32602`，指出允许范围

### Requirement: 错误模型

系统 SHALL 对所有应用层失败返回结构化错误，包含 JSON-RPC 错误码、稳定领域错误码、可读消息、可选详情和是否可重试。系统 SHALL NOT 向客户端暴露堆栈、文件路径或内部对象地址。未处理异常 SHALL 转换为 `-32603` 并记录关联请求标识。

#### Scenario: 领域错误

- **WHEN** 客户端操作不存在的实体
- **THEN** 返回错误码 `-32001` 和领域码 `ENTITY_NOT_FOUND`，详情包含请求的实体标识

#### Scenario: 内部异常

- **WHEN** 方法执行中发生未处理异常
- **THEN** 客户端收到 `-32603` 和通用消息，服务端日志以相同请求标识记录完整异常

#### Scenario: 可重试标记

- **WHEN** 渲染因临时 GPU 资源不足失败
- **THEN** 错误对象包含 `retryable: true` 和建议的重试等待时间

### Requirement: 幂等性

所有修改场景或创建作业的方法 SHALL 接受可选 `idempotencyKey`。相同方法、相同参数和相同键在配置的保留期内 SHALL 返回首次结果，不重复执行。相同键配合不同参数 SHALL 返回 `IDEMPOTENCY_CONFLICT`。成功与失败的终态结果 SHALL 都可重放。

#### Scenario: 重试相同请求

- **WHEN** 客户端以相同幂等键重试一次成功的创建实体请求
- **THEN** 系统返回首次创建的实体标识，场景中只有一个新实体

#### Scenario: 键参数冲突

- **WHEN** 客户端复用已有幂等键但改变请求参数
- **THEN** 系统返回 `IDEMPOTENCY_CONFLICT`，不执行新请求

#### Scenario: 过期后重试

- **WHEN** 请求的幂等键超过保留期后再次使用
- **THEN** 系统按新请求执行，并在结果元数据中标记未命中缓存

### Requirement: 异步作业与进度

耗时超过配置阈值的操作 SHALL 返回作业标识而非阻塞 HTTP 请求。系统 SHALL 提供作业查询、取消和结果获取，并通过 WebSocket 发布状态与进度。作业状态 SHALL 为排队、运行、取消中、成功、失败或已取消之一。

#### Scenario: 渲染作业异步执行

- **WHEN** 客户端提交渲染且预计耗时超过阈值
- **THEN** 系统在一次响应内返回作业标识，后续查询报告状态与百分比，完成时报告产物路径

#### Scenario: 取消作业

- **WHEN** 客户端取消一个运行中的导入或渲染作业
- **THEN** 作业进入取消中并最终进入已取消，部分产物按各能力规则清理

#### Scenario: 查询不存在作业

- **WHEN** 客户端查询未知作业标识
- **THEN** 返回 `JOB_NOT_FOUND` 领域错误

### Requirement: WebSocket 事件通道

系统 SHALL 在 `ws://127.0.0.1:8711/api/v1/ws` 提供 WebSocket 连接。客户端 SHALL 先通过带 Bearer Token 的 HTTP 请求获取短期一次性票据，再以该票据完成 WebSocket 握手。票据 SHALL 在 30 秒后过期且只能使用一次。连接建立后，系统 SHALL 推送实体、场景、导入和渲染事件，并接受客户端订阅、取消订阅和心跳消息。事件 SHALL 带单调递增序号和时间戳。

#### Scenario: 订阅场景事件

- **WHEN** 客户端获取票据、建立 WebSocket 并订阅 `entity.*` 与 `render.*`
- **THEN** 客户端收到匹配过滤器的后续事件，不收到未订阅类别的事件

#### Scenario: 票据过期

- **WHEN** 客户端使用超过 30 秒的票据建立 WebSocket
- **THEN** 握手被拒绝并关闭连接，客户端需重新获取票据

#### Scenario: 票据重复使用

- **WHEN** 客户端使用同一票据建立第二条 WebSocket
- **THEN** 第二次握手被拒绝，已建立的第一条连接不受影响

#### Scenario: 断开重连

- **WHEN** 客户端在收到序号一百后断开并在保留窗口内重新获取票据并重连，请求从一百零一开始
- **THEN** 系统补发保留的事件，序号连续

#### Scenario: 事件缺口

- **WHEN** 客户端请求的起始序号早于保留窗口
- **THEN** 系统发送 `events.gap` 事件，客户端应重新获取场景快照

#### Scenario: 未鉴权握手

- **WHEN** 客户端未提供有效票据建立 WebSocket
- **THEN** 握手被拒绝，连接关闭且不推送任何事件

### Requirement: 流量限制与过载保护

系统 SHALL 限制单次请求体大小、批量请求数量、并发连接数和排队命令数量。超过限制 SHALL 返回 HTTP 413 或 429，或 JSON-RPC 过载错误，并包含重试建议。慢客户端 SHALL NOT 无限累积事件。

#### Scenario: 请求体过大

- **WHEN** 客户端提交超过配置上限的请求体
- **THEN** 系统返回 HTTP 413 并关闭请求，不解析方法

#### Scenario: 命令队列已满

- **WHEN** 待执行命令数达到配置上限
- **THEN** 新请求返回过载错误和 `retryAfterSeconds`，已在队列中的命令继续执行

#### Scenario: 慢 WebSocket 客户端

- **WHEN** 客户端读取事件速度长期低于生产速度
- **THEN** 系统按连接丢弃最旧事件，超过阈值后断开该连接，不影响其他订阅者

### Requirement: 安全边界

控制 API SHALL 默认只监听回环地址，默认关闭 CORS，默认在 Shipping 构建中禁用。启用 CORS 时必须使用显式来源白名单，不得使用通配符配合凭据。文件导入路径 SHALL 限制在配置的允许根目录内，并拒绝符号链接逃逸。

#### Scenario: 默认禁止跨域

- **WHEN** 浏览器从非白名单来源发起跨域请求
- **THEN** 响应不包含允许该来源的 CORS 头，预检请求被拒绝

#### Scenario: 导入路径逃逸

- **WHEN** 导入请求的路径解析后位于允许根目录之外
- **THEN** 系统拒绝请求并返回 `PATH_NOT_ALLOWED`

#### Scenario: Shipping 构建

- **WHEN** 以 Shipping 配置启动且未显式启用控制 API
- **THEN** 控制 API 不监听端口

### Requirement: 可观测性

系统 SHALL 为每个请求记录请求标识、方法、耗时、结果状态和错误码，并提供 `system.stats` 返回请求计数、错误计数、活动连接数、队列深度和作业统计。日志 SHALL NOT 包含完整令牌、二进制请求体或用户私有文件内容。

#### Scenario: 请求日志关联

- **WHEN** 客户端收到带请求标识的错误响应
- **THEN** 操作者能用该标识在日志中找到对应请求的处理记录和耗时

#### Scenario: 统计查询

- **WHEN** 客户端调用 `system.stats`
- **THEN** 返回自启动以来的请求总数、按方法分组的计数、错误数、活动 WebSocket 连接数和队列深度

### Requirement: 方法覆盖范围

控制 API SHALL 暴露与场景编排能力等价的方法，至少覆盖场景生命周期、场景库查询/切换/删除、实体增删改查、变换、角色创建与动画挂接、道具创建与材质物理、灯光创建与更新、环境参数与预设、相机创建与激活、资产导入、渲染作业和系统诊断。界面能完成的场景修改 SHALL 能通过 API 完成。控制 API SHALL 同时覆盖制作包、沙盒规则、导演干预、会话重演、历史分支和 Take 方法。

#### Scenario: 界面与 API 等价

- **WHEN** 操作者在界面中完成一次灯光创建和一次环境预设应用
- **THEN** 对应的 `light.create` 和 `environment.applyPreset` 方法产生相同的场景状态和事件

#### Scenario: 查询场景快照

- **WHEN** 客户端调用场景快照方法
- **THEN** 返回当前场景标识、名称、版本、实体列表、环境、相机和活动相机

#### Scenario: 管理场景库

- **WHEN** 客户端查询场景库、切换场景或删除未打开场景
- **THEN** 对应方法返回场景列表、切换后的当前场景或回收路径，行为与场景管理界面一致

#### Scenario: 增量查询

- **WHEN** 客户端携带上次快照版本请求增量
- **THEN** 系统返回自该版本以来的变更或指示需要完整快照

#### Scenario: 导演与重演方法可用

- **WHEN** 客户端调用 `system.methods`
- **THEN** 返回包含 `production.*`、`sandbox.*`、`director.*`、`replay.*` 和 `take.*` 的方法目录及权限要求

### Requirement: 会话与表演命令覆盖

控制 API SHALL 暴露多人会话和表演命令，至少覆盖 `session.create`、`session.start`、`session.join`、`session.leave`、`session.status`、`session.snapshot`、`session.export`、`session.end`、`session.kick`、`session.setRole`、`session.reconnect`、`session.assignCharacter`、`session.releaseCharacter`、`session.transferCharacter`、`network.status`、`network.players`、`performance.action`、`performance.expression`、`performance.voice` 和 `performance.motionMatching`。方法目录 SHALL 标明每个方法是否修改世界和是否异步；会话角色和所有权在运行时校验，失败返回 `FORBIDDEN`。

#### Scenario: 查询多人方法

- **WHEN** 客户端调用 `system.methods`
- **THEN** 返回上述多人方法的名称、描述、是否修改世界和是否异步，现有单用户方法保持不变

#### Scenario: 导演通过 API 查询会话

- **WHEN** 持有有效令牌的导演客户端调用 `session.status`
- **THEN** 返回会话标识、玩家与角色、角色占用、世界版本和网络统计

#### Scenario: 自动化客户端以表演者身份播放动作

- **WHEN** 已加入会话的自动化客户端以表演者身份调用 `performance.action` 并指定自己的角色
- **THEN** 服务器校验会话和所有权后返回动作结果，事件通道发布对应表演事件

### Requirement: 世界交互命令覆盖

控制 API SHALL 暴露物品、库存、制作和建造命令，至少覆盖 `item.spawn`、`item.pickup`、`item.hold`、`item.release`、`item.drop`、`inventory.get`、`craft.start`、`craft.status`、`craft.cancel`、`build.confirm`、`build.undo` 和 `world.status`。方法目录 SHALL 标明每个方法所需的会话角色、是否修改世界和是否可取消。

#### Scenario: 拾取物品

- **WHEN** 已加入会话的自动化客户端以表演者身份调用 `item.pickup` 并指定附近的物品
- **THEN** 服务器校验距离和所有权后返回持有结果，事件通道发布物品状态变化

#### Scenario: 查询制作进度

- **WHEN** 客户端调用 `craft.status` 查询自己发起的制作
- **THEN** 返回配方、进度、剩余时间和状态，不泄露其他玩家的私有库存

#### Scenario: 确认建造

- **WHEN** 已加入会话的自动化客户端以表演者身份调用 `build.confirm` 并提交预览位置
- **THEN** 服务器校验位置、材料和权限后生成结构，失败时返回对应领域错误

### Requirement: 会话角色错误

控制 API SHALL 为多人命令返回稳定的领域错误码，至少包括 `SESSION_NOT_FOUND`、`SESSION_FULL`、`SESSION_ALREADY_EXISTS`、`SESSION_ALREADY_ENDED`、`FORBIDDEN`、`CHARACTER_ALREADY_OWNED` 和 `SCENE_VERSION_UNSUPPORTED`。`FORBIDDEN` 覆盖未授权会话成员、角色权限不足和私有库存访问；错误 SHALL 包含会话标识、发起玩家标识和请求标识，且 SHALL NOT 泄露其他玩家令牌或私有库存内容。

#### Scenario: 未加入会话

- **WHEN** 客户端调用 `performance.action` 但没有有效会话成员身份
- **THEN** 返回 `FORBIDDEN`，不执行动作，日志记录请求标识

#### Scenario: 角色不匹配

- **WHEN** 表演者调用 `session.end`
- **THEN** 返回 `FORBIDDEN`，会话保持运行

#### Scenario: 所有权冲突

- **WHEN** 两个玩家同时请求控制同一个未分配角色
- **THEN** 先到请求成功，后到请求返回 `CHARACTER_ALREADY_OWNED`，角色只有一个所有者

### Requirement: 会话事件通道

控制 API 的 WebSocket 事件通道 SHALL 支持订阅会话、表演、物品、制作和建造事件。事件 SHALL 带会话标识、单调递增序号、时间戳、事件类型和受影响实体；客户端 SHALL 能按会话和事件类别过滤，并能从指定序号补发。

#### Scenario: 订阅会话事件

- **WHEN** 客户端建立 WebSocket 并订阅 `session.*`、`performance.*` 和 `build.*`
- **THEN** 客户端收到匹配类别的事件，不收到未订阅类别的事件

#### Scenario: 事件补发

- **WHEN** 客户端从保留窗口内的序号重新连接
- **THEN** 系统补发该序号之后的会话事件，序号连续

#### Scenario: 事件缺口

- **WHEN** 客户端请求的序号早于保留窗口
- **THEN** 系统发送需要完整会话快照的指示，客户端重新获取快照

### Requirement: 导演与重演权限绑定

控制 API SHALL 把导演、重演和 Take 命令绑定到参与者令牌和会话角色。只有 owner 或 director SHALL 能执行导演干预、创建分支、启动重演和固化 Take；Viewer、performer 和 agent SHALL 只获得查询或被授权的 ghost 读取能力。

#### Scenario: Director 执行导演命令

- **WHEN** 持有 director 参与者令牌的客户端提交 `director.pause`
- **THEN** 命令被执行并返回新的世界版本和事件序号

#### Scenario: Performer 请求分支

- **WHEN** performer 令牌调用 `replay.branch`
- **THEN** 命令被拒绝并返回 `FORBIDDEN`，不创建分支

#### Scenario: 令牌与会话不匹配

- **WHEN** 参与者令牌属于另一会话
- **THEN** 命令被拒绝并返回 `PARTICIPANT_NOT_BOUND`，不泄露目标会话状态

### Requirement: 沙盒与导演事件流

WebSocket SHALL 支持 `sandbox.*`、`narrative.*`、`director.*`、`replay.*` 和 `take.*` 事件类别，并允许按会话、参与者、节拍、实体和事件类别过滤。客户端 SHALL 能用 `since` 补发；历史不足时 SHALL 返回 `events.gap` 并要求重新快照。

#### Scenario: 订阅导演事件

- **WHEN** 客户端订阅 `director.*` 和 `narrative.*`
- **THEN** 只收到匹配类别的新事件，不收到无关沙盒生成事件

#### Scenario: 按节拍过滤

- **WHEN** 客户端以节拍标识过滤叙事事件
- **THEN** 只收到该节拍及其子事件，其他节拍事件被忽略

#### Scenario: 历史缺口

- **WHEN** 请求的 `since` 早于保留窗口
- **THEN** 系统发送 `events.gap` 并要求客户端获取新快照或使用 ghost 检查点

### Requirement: 重演 API 契约

控制 API SHALL 提供 `replay.start`、`replay.status`、`replay.cancel`、`replay.branch` 和 `replay.ghost`。重演 SHALL 返回请求标识、目标事件序号、状态、精确性标记、检查点和差异摘要；重演不得阻塞实时会话。

#### Scenario: 启动精确重演

- **WHEN** 客户端请求重演到有效事件序号且版本摘要一致
- **THEN** 返回重演作业标识，状态最终为 `succeeded` 且 `exact=true`

#### Scenario: 重演差异

- **WHEN** 重演后的状态摘要与录制不一致
- **THEN** 返回 `REPLAY_DIVERGED`、首个差异事件和字段，状态为 `failed`

#### Scenario: 取消重演

- **WHEN** 客户端取消运行中的重演
- **THEN** 重演停止并返回 `cancelled`，live 会话和日志不受影响

#### Scenario: 创建分支

- **WHEN** 客户端从有效事件序号创建分支
- **THEN** 返回新会话标识、父会话标识和分叉序号，原会话不变

### Requirement: Ghost 与检查点查询

控制 API SHALL 提供只读 ghost 会话和检查点查询，返回事件序号范围、快照摘要、可用相机和叙事状态。Ghost 查询 SHALL 限制响应大小并支持分页；超出保留窗口 SHALL 返回 `REPLAY_HISTORY_UNAVAILABLE`。

#### Scenario: 查询可用范围

- **WHEN** 客户端查询会话重演范围
- **THEN** 返回最早可用检查点、最新事件序号和可用节拍摘要

#### Scenario: 分页读取事件

- **WHEN** 客户端请求超过单页上限的事件范围
- **THEN** 系统返回受限页和下一页游标，不截断为静默丢失

#### Scenario: Ghost 写入被拒绝

- **WHEN** 客户端在 ghost 会话调用修改方法
- **THEN** 返回 `REPLAY_READ_ONLY`，不产生事件

### Requirement: Take API 与版本不可变

控制 API SHALL 提供 Take 创建、校验、查询和渲染方法。Take SHALL 记录版本、事件区间、镜头轨道、分辨率预设和内容摘要；修改已保存 Take SHALL 创建新版本，不得原地覆盖，渲染 SHALL 接受显式 Take 版本。

#### Scenario: 创建 Take

- **WHEN** 导演提交有效事件区间和相机轨道
- **THEN** 返回 Take 标识、版本和内容摘要

#### Scenario: 修改 Take

- **WHEN** 导演修改已保存 Take
- **THEN** 返回新的 Take 版本，旧版本仍可查询和渲染

#### Scenario: 渲染缺失依赖

- **WHEN** 客户端请求渲染的 Take 引用的快照或相机不存在
- **THEN** 返回 `TAKE_DEPENDENCY_MISSING`，不创建渲染作业

### Requirement: 确定性与日志元数据

`system.health`、`system.methods` 和会话快照 SHALL 报告模拟 tick 率、随机种子支持、内容摘要、日志校验状态、重演能力和保留窗口。客户端 SHALL 能据此判断某次重演是否可能精确。

#### Scenario: 健康检查报告重演能力

- **WHEN** 客户端调用 `system.health`
- **THEN** 返回 `replaySupported`、固定 tick 率、日志版本和内容摘要

#### Scenario: 会话不可精确重演

- **WHEN** 会话日志校验失败或版本摘要缺失
- **THEN** 快照中的 `replayable=false` 并附带原因

### Requirement: Agent 控制方法

控制 API SHALL 暴露 Agent 会话与角色控制方法，至少覆盖 `session.heartbeat`、`character.moveTo`、`character.lookAt`、`character.stop` 和 `character.state`。方法目录 SHALL 标明所需参与者类型、是否修改世界、是否异步和所需角色租约。

#### Scenario: 枚举 Agent 方法

- **WHEN** 客户端调用 `system.methods`
- **THEN** 返回 Agent 方法及其参与者类型、修改性、异步性和租约要求，既有方法保持可用

#### Scenario: 查询角色状态

- **WHEN** Agent 调用 `character.state` 查询自己持有租约的角色
- **THEN** 返回位置、朝向、速度、动作、租约和状态版本

#### Scenario: 续租

- **WHEN** Agent 在租约到期前调用 `session.heartbeat`
- **THEN** 返回新的到期时间和状态版本，租约继续有效

### Requirement: Agent 命令关联

Agent 控制命令 SHALL 接受请求标识、幂等键和期望状态版本，并在响应中返回请求标识、结果状态和提交后的状态版本。缺少请求标识 SHALL 按参数错误拒绝；相同幂等键和参数 SHALL 返回首次结果。

#### Scenario: 响应关联

- **WHEN** Agent 提交带 `requestId` 的 `character.moveTo`
- **THEN** 响应包含相同 `requestId`、`ok`、`result` 和 `stateVersion`

#### Scenario: 幂等重试

- **WHEN** Agent 以相同幂等键重试已成功的移动命令
- **THEN** 返回首次结果，不重新开始移动

#### Scenario: 状态版本冲突

- **WHEN** Agent 提交低于当前状态版本的 `expectedStateVersion`
- **THEN** 返回 `STALE_STATE`，不修改角色或世界

### Requirement: Agent 事件过滤

WebSocket 事件通道 SHALL 支持按会话、参与者、角色和事件类别过滤 Agent 事件，并 SHALL 允许从指定序号补发。事件 SHALL 包含关联请求标识（适用时）、会话标识、参与者标识、角色标识、序号和时间戳。

#### Scenario: 按角色订阅

- **WHEN** Agent 订阅自己的角色和 `character.*`、`performance.*` 事件
- **THEN** 只收到该角色和匹配类别的后续事件

#### Scenario: 关联请求

- **WHEN** Agent 发送一条表演或移动命令并随后收到完成事件
- **THEN** 事件包含原命令的请求标识，Agent 可以匹配请求和结果

#### Scenario: 断线补发

- **WHEN** Agent 从保留窗口内的序号重新连接
- **THEN** 系统补发该序号之后的匹配事件，序号连续

### Requirement: 控制 API 参与者绑定

控制 API SHALL 把 Agent 命令绑定到经过认证的会话参与者，并 SHALL NOT 仅依赖请求参数中的玩家标识判断控制权。未绑定会话的请求、参与者不匹配的请求和过期租约的请求 SHALL 在应用前被拒绝。

#### Scenario: 未绑定参与者

- **WHEN** 已认证的 API 调用者提交角色移动命令但没有绑定会话参与者
- **THEN** 返回 `PARTICIPANT_NOT_BOUND`，不执行命令

#### Scenario: 伪造参与者

- **WHEN** API 调用者尝试用另一个 Agent 的参与者标识控制角色
- **THEN** 返回 `FORBIDDEN`，目标角色状态不变

#### Scenario: 查看者调用控制 API

- **WHEN** 绑定为 Viewer 的参与者调用修改角色或世界的 API 方法
- **THEN** 返回 `VIEWER_READ_ONLY`，不执行命令
