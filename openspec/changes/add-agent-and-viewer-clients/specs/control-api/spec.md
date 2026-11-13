# Spec Delta

## ADDED Requirements

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
