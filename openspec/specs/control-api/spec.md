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

控制 API SHALL 暴露与场景编排能力等价的方法，至少覆盖场景生命周期、场景库查询/切换/删除、实体增删改查、变换、角色创建与动画挂接、道具创建与材质物理、灯光创建与更新、环境参数与预设、相机创建与激活、资产导入、渲染作业和系统诊断。界面能完成的场景修改 SHALL 能通过 API 完成。

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
