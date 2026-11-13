# Spec Delta

## Purpose

定义从制作包装载的高自由度权威沙盒世界，使玩家、Agent、NPC 和导演共享可交互、可持久化、可复现的运行时状态。

## ADDED Requirements

### Requirement: 制作包装载与旧场景兼容

系统 SHALL 从制作包 `production.json` 装载沙盒会话，读取场景引用、演员阵容、世界规则和初始状态。制作包引用的场景、角色和规则 SHALL 在装载前校验；校验失败 SHALL 原子拒绝会话。只有 `scene.json` 的旧场景 SHALL 使用默认沙盒规则运行。

#### Scenario: 装载有效制作包

- **WHEN** 客户端以有效制作包创建沙盒会话
- **THEN** 会话包含场景实体、初始演员、交互点、区域和世界规则，并发布 `sandbox.loaded` 事件

#### Scenario: 制作包引用缺失

- **WHEN** 制作包引用的场景、角色或规则不存在
- **THEN** 会话创建失败并返回 `PRODUCTION_REFERENCE_NOT_FOUND`，不创建部分世界

#### Scenario: 旧场景回退

- **WHEN** 客户端只用既有 `scene.json` 创建会话
- **THEN** 系统使用默认沙盒规则和空叙事图，现有表演者、Agent 和 Viewer 流程保持可用

### Requirement: 交互点与可操作物

系统 SHALL 定义可交互对象、可用动词、前置条件、目标选择和结果。交互 SHALL 由服务器验证参与者、距离、朝向、对象状态和冷却；未知动词或不满足条件 SHALL 返回领域错误且不修改世界。

#### Scenario: 有效交互

- **WHEN** 参与者对范围内的可操作物执行已允许动词
- **THEN** 服务器应用结果、更新对象状态并发布带参与者、对象和动词的交互事件

#### Scenario: 距离不足

- **WHEN** 参与者距离交互点超过规则上限
- **THEN** 交互被拒绝并返回 `INTERACTION_OUT_OF_RANGE`，对象状态不变

#### Scenario: 未知动词

- **WHEN** 客户端对可操作物提交规则未声明的动词
- **THEN** 交互被拒绝并返回 `INTERACTION_NOT_ALLOWED`，不执行默认动作

### Requirement: 区域、生成器与重生

系统 SHALL 支持区域、生成器、生成表、预算、冷却和重生点。生成 SHALL 受全局和区域上限约束，并使用会话种子；达到上限时 SHALL 延迟或拒绝并发布事件，不得静默超限。

#### Scenario: 在预算内生成

- **WHEN** 生成器触发且全局与区域预算均未耗尽
- **THEN** 系统按生成表和会话种子生成实体，并发布 `sandbox.spawned` 事件

#### Scenario: 达到上限

- **WHEN** 区域实体数达到上限
- **THEN** 生成请求被延迟或拒绝并返回 `SPAWN_BUDGET_EXCEEDED`，现有实体不被移除

#### Scenario: 相同种子生成

- **WHEN** 两个会话使用相同制作包、种子和生成事件
- **THEN** 生成的实体类型、位置和顺序一致

### Requirement: NPC 与 Agent 行为

系统 SHALL 为 NPC 提供可配置行为档案，包括目标选择、感知、巡逻、交互和可选的战斗或躲避行为。NPC 与 Agent SHALL 使用相同权威动作和交互接口；导演接管或暂停 SHALL 覆盖自主行为并可恢复。

#### Scenario: NPC 自主行为

- **WHEN** NPC 感知到符合其目标条件的实体
- **THEN** NPC 通过权威动作接口执行行为，并发布可观察的行为事件

#### Scenario: Agent 与 NPC 行为等价

- **WHEN** Agent 和 NPC 使用相同行为档案操作同一类对象
- **THEN** 两者产生相同的服务器校验结果和事件字段

#### Scenario: 导演恢复自主

- **WHEN** 导演结束对 NPC 的接管
- **THEN** NPC 从接管结束状态恢复自主行为，接管期间的修改保留

### Requirement: 资源与制作循环

系统 SHALL 支持资源节点、采集、库存、消耗、制作和产出，并把资源变化记录为世界事件。资源数量 SHALL 受节点容量和配方校验；并发采集 SHALL 串行化，不得产生负库存。

#### Scenario: 采集并制作

- **WHEN** 参与者采集资源并提交有效配方
- **THEN** 节点和库存按规则变化，制作任务开始并发布资源与制作事件

#### Scenario: 材料不足

- **WHEN** 参与者提交材料不足的配方
- **THEN** 制作被拒绝并返回 `INSUFFICIENT_RESOURCES`，库存不变

#### Scenario: 并发采集

- **WHEN** 两个参与者同时采集同一剩余一份资源的节点
- **THEN** 只有一个采集成功，另一个收到资源已耗尽错误，库存不出现负数

### Requirement: 持久化、重连与恢复

系统 SHALL 持久化沙盒世界状态、实体生命周期、参与者状态和导演修改到会话快照。断线重连或服务器重启恢复 SHALL 从最近快照和事件日志重建；无法重建 SHALL 拒绝恢复并保留诊断。

#### Scenario: 重连恢复

- **WHEN** 参与者断线后在重连宽限期内重新加入
- **THEN** 参与者恢复到最近权威位置、库存和角色状态，世界实体不重复生成

#### Scenario: 服务器重启恢复

- **WHEN** 服务器使用同一会话目录重新启动
- **THEN** 系统从最近快照和后续事件重建世界，并报告恢复的事件序号

#### Scenario: 快照损坏

- **WHEN** 最近快照或事件日志校验失败
- **THEN** 恢复被拒绝并返回 `SESSION_RESTORE_FAILED`，不加载部分世界

### Requirement: 沙盒参与者权限

系统 SHALL 在沙盒层强制 owner、director、performer、agent 和 viewer 的权限。Viewer 只能观察和接收事件；performer 与 Agent 只能操作自身角色和允许的交互；director 可执行导演操作；越权 SHALL 返回稳定错误码。

#### Scenario: Viewer 只读

- **WHEN** Viewer 提交交互、生成或导演命令
- **THEN** 命令被拒绝并返回 `VIEWER_READ_ONLY`，世界状态不变

#### Scenario: 表演者操作自身角色

- **WHEN** 表演者操作自己占有的角色和范围内可操作物
- **THEN** 命令按沙盒规则执行，返回新的世界版本

#### Scenario: 导演越权管理

- **WHEN** director 尝试踢出 owner 或转移所有权
- **THEN** 命令被拒绝并返回 `FORBIDDEN`，会话和所有权不变

### Requirement: 世界规则校验与运行预算

系统 SHALL 校验世界规则中的生成率、实体数量、资源上限、交互距离和内容引用，拒绝越界、缺失引用和路径逃逸。运行中 SHALL 对实体和事件预算限流，超限 SHALL 降级并报告。

#### Scenario: 非法生成率

- **WHEN** 制作包声明超过配置上限的生成率
- **THEN** 制作包校验失败并指出规则路径，会话不启动

#### Scenario: 内容路径逃逸

- **WHEN** 世界规则引用允许根目录之外的内容路径
- **THEN** 校验失败并返回 `PATH_NOT_ALLOWED`，不加载该内容

#### Scenario: 运行预算超限

- **WHEN** 会话实体数达到运行预算
- **THEN** 新的非必要生成被拒绝并发布 `sandbox.budgetExceeded`，已有实体和参与者不受影响
