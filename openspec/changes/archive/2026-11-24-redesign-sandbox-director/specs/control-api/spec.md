# Spec Delta

## MODIFIED Requirements

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

## ADDED Requirements

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
