# Spec Delta

## ADDED Requirements

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
