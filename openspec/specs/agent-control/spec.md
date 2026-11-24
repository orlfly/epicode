# agent-control Specification

## Purpose
让无界面的智能体通过控制 API 加入会话、持有角色租约、发送结构化控制命令，并获得可判断成功、失败、完成和受阻的反馈。

## Requirements

### Requirement: Agent 参与者身份

系统 SHALL 允许 Agent 以无 Unreal 客户端、无渲染界面的参与者身份加入会话，并分配稳定的 Agent 标识。Agent 加入后 SHALL NOT 自动占有角色，只有显式申请角色后才进入可控制状态。

#### Scenario: 无客户端加入

- **WHEN** Agent 通过控制 API 加入一个活动会话并声明参与者类型为 Agent
- **THEN** 服务器创建 Agent 参与者记录并返回参与者标识，不要求启动游戏客户端或渲染进程

#### Scenario: 加入后未分配角色

- **WHEN** Agent 加入会话但没有申请角色
- **THEN** Agent 可以查询会话和观察世界，但角色控制命令返回 `CHARACTER_NOT_ASSIGNED`

#### Scenario: 重复 Agent 标识

- **WHEN** 第二个 Agent 使用已被活动参与者占用的 Agent 标识加入
- **THEN** 服务器拒绝加入并返回 `AGENT_ALREADY_JOINED`，原参与者状态不变

### Requirement: 角色租约

系统 SHALL 为 Agent 控制的角色提供独占租约，包含租约标识、控制者、角色标识、获取时间、到期时间和状态版本。租约 SHALL 通过心跳续期，并可在释放、超时、踢出或会话结束时终止。

#### Scenario: 获取角色租约

- **WHEN** Agent 申请一个未被占用的角色
- **THEN** 服务器授予租约并返回角色标识、租约标识、到期时间和当前状态版本

#### Scenario: 租约冲突

- **WHEN** 两个 Agent 同时申请同一个未被占用的角色
- **THEN** 只有一个请求获得租约，另一个返回 `CHARACTER_ALREADY_OWNED`

#### Scenario: 心跳续期

- **WHEN** Agent 在租约到期前发送心跳
- **THEN** 服务器延长到期时间并返回新的到期时间和状态版本

#### Scenario: 租约过期

- **WHEN** Agent 超过租约期限没有发送心跳
- **THEN** 服务器终止租约、停止该 Agent 的移动命令并发布 `agent.lease.expired` 事件

#### Scenario: 主动释放

- **WHEN** Agent 释放自己的角色租约
- **THEN** 服务器终止租约，角色进入可分配状态，后续控制命令返回 `NOT_CONTROLLER`

### Requirement: 命令关联与幂等

每条 Agent 控制命令 SHALL 携带请求标识，并可携带幂等键和期望状态版本。系统 SHALL 在响应中回传请求标识和提交后的状态版本，并 SHALL 拒绝基于过期状态版本的修改命令。

#### Scenario: 成功响应关联

- **WHEN** Agent 发送一条带请求标识的移动命令
- **THEN** 响应包含相同请求标识、`ok` 状态、结果对象和新的状态版本

#### Scenario: 幂等重试

- **WHEN** Agent 使用相同幂等键和相同参数重试一条已成功的命令
- **THEN** 系统返回首次结果，不重复移动、拾取或播放动作

#### Scenario: 过期版本

- **WHEN** Agent 使用低于当前状态版本的期望版本提交修改命令
- **THEN** 服务器返回 `STALE_STATE`，世界和角色状态保持不变

### Requirement: 高层角色移动

系统 SHALL 为 Agent 提供 `character.moveTo`、`character.lookAt` 和 `character.stop` 高层控制，由服务器侧导航和角色移动组件执行。移动结果 SHALL 至少区分已接受、已完成、受阻和失败。

#### Scenario: 移动到目标

- **WHEN** Agent 请求角色移动到一个可达位置
- **THEN** 服务器接受命令并返回目标位置和预计完成信息，角色位置继续由服务器权威复制

#### Scenario: 导航不可用

- **WHEN** Agent 请求移动到目标但场景没有可用导航数据
- **THEN** 服务器返回 `NAVIGATION_UNAVAILABLE`，角色位置不变

#### Scenario: 移动受阻

- **WHEN** 角色在移动过程中被碰撞或不可达区域阻挡
- **THEN** 服务器停止移动并发布 `character.move.blocked` 事件，包含原因和最后位置

#### Scenario: 停止移动

- **WHEN** Agent 在移动过程中请求停止
- **THEN** 服务器取消当前移动并发布 `character.move.stopped` 事件

### Requirement: Agent 观察结果

系统 SHALL 为 Agent 提供角色和世界观察结果，至少包含位置、朝向、速度、当前动作、表情、持有物、租约状态、状态版本和可见世界实体摘要。

#### Scenario: 查询角色状态

- **WHEN** Agent 查询自己持有租约的角色
- **THEN** 返回角色位置、朝向、速度、当前动作、租约和状态版本

#### Scenario: 查询世界状态

- **WHEN** Agent 查询会话世界
- **THEN** 返回物品、建造物、其他角色摘要和世界版本，不泄露其他 Agent 的私有库存

#### Scenario: 查询他人角色

- **WHEN** Agent 查询不属于自己租约的角色
- **THEN** 返回公开观察字段，不返回租约标识和私有控制信息

### Requirement: Agent 反馈事件

系统 SHALL 通过事件通道向 Agent 发布命令接受、命令拒绝、动作开始、动作完成、移动受阻、租约变化和世界变化事件。事件 SHALL 包含会话标识、序号、时间戳、事件类型、参与者标识、角色标识和可选的请求标识。

#### Scenario: 动作完成反馈

- **WHEN** Agent 请求的表演动作完成
- **THEN** Agent 收到带原请求标识的 `performance.action.completed` 事件

#### Scenario: 命令拒绝反馈

- **WHEN** Agent 的控制命令因权限、租约或参数失败
- **THEN** Agent 收到同步错误，并可收到对应 `command.rejected` 事件

#### Scenario: 事件序号连续

- **WHEN** Agent 连续收到多个反馈事件
- **THEN** 事件序号严格递增，断开重连后可从保留窗口内补发

### Requirement: Agent 错误与审计

系统 SHALL 为 Agent 控制返回稳定领域错误码，并记录带 Agent 标识、请求标识、命令、角色、结果和耗时的审计记录。审计记录 SHALL NOT 包含完整令牌或私有库存内容。

#### Scenario: 非控制者

- **WHEN** Agent 对自己没有租约的角色发送移动命令
- **THEN** 服务器返回 `NOT_CONTROLLER`，角色状态不变，审计记录该请求

#### Scenario: 租约过期

- **WHEN** Agent 在租约过期后发送控制命令
- **THEN** 服务器返回 `LEASE_EXPIRED`，并提示重新申请角色

#### Scenario: 过载保护

- **WHEN** 单个 Agent 的命令速率超过配置上限
- **THEN** 服务器返回 `RATE_LIMITED` 和重试等待时间，不执行超限命令
