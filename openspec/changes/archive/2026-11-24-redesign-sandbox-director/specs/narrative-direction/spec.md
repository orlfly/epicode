# Spec Delta

## Purpose

定义叙事导演层，使导演能在保留演员自主性的沙盒中编排节拍、实时干预、标记镜头，并从任意历史点建立可重演的分支。

## ADDED Requirements

### Requirement: 叙事节拍图

系统 SHALL 支持制作包中的有向节拍图，节点包含标识、进入条件、退出条件、目标、导演 cue 和可选分支。节拍图 SHALL 在装载前校验标识唯一、引用存在、入口可达且条件可求值；无效图 SHALL 拒绝启动会话。

#### Scenario: 进入下一节拍

- **WHEN** 当前节拍的退出条件在权威世界中满足
- **THEN** 系统进入满足条件的后继节拍并发布带事件序号的 `narrative.beatEntered` 事件

#### Scenario: 条件不满足

- **WHEN** 节拍的进入条件不满足
- **THEN** 该节拍保持未激活，不执行其 cue 或目标

#### Scenario: 无效节拍图

- **WHEN** 制作包包含重复节拍标识或不可达入口
- **THEN** 制作包校验失败并指出节拍路径，会话不启动

### Requirement: 导演 cue 调度

系统 SHALL 支持按时间、事件、节拍或条件触发导演 cue。cue 动作 SHALL 包括镜头切换、灯光/环境变化、生成或移除实体、发送叙事消息、暂停、慢放和角色接管；每个 cue SHALL 记录执行结果和是否幂等。

#### Scenario: 时间 cue 触发

- **WHEN** 会话时间到达 cue 的计划时间
- **THEN** cue 只执行一次并发布 `director.cueExecuted` 事件

#### Scenario: 条件 cue 触发

- **WHEN** 条件 cue 的表达式从假变为真
- **THEN** cue 执行一次，直到条件重置后才可再次触发

#### Scenario: cue 动作失败

- **WHEN** cue 引用的角色、相机或实体不存在
- **THEN** cue 记录失败并发布 `director.cueFailed`，后续 cue 不受影响

### Requirement: 实时导演控制

系统 SHALL 允许有权限的导演暂停/恢复、调整时间倍率、切换镜头、修改环境、生成/移除实体、设置目标、触发节拍和发送叙事消息。所有导演操作 SHALL 经过服务器校验、写入事件日志，并受幂等键保护。

#### Scenario: 暂停与恢复

- **WHEN** 导演暂停会话后恢复
- **THEN** 模拟时间在暂停期间停止，恢复后从同一 tick 继续，事件序号保持单调

#### Scenario: 慢放

- **WHEN** 导演把时间倍率设为慢放值
- **THEN** 权威模拟按该倍率推进，持续时间和事件时间戳仍可重演

#### Scenario: 重复导演请求

- **WHEN** 客户端以相同幂等键重复提交导演命令
- **THEN** 系统返回首次结果，不重复切换镜头或生成实体

### Requirement: 角色接管与自主恢复

系统 SHALL 允许导演临时接管表演者、Agent 或 NPC 的控制权，并在接管期间暂停其自主决策。接管 SHALL 记录原控制者、原因和结束条件；结束接管 SHALL 恢复到接管结束时的权威状态，并按规则恢复自主行为。

#### Scenario: 接管 NPC

- **WHEN** 导演接管一个自主 NPC
- **THEN** NPC 的自主决策停止，导演输入成为权威动作，其他参与者收到接管事件

#### Scenario: 接管冲突

- **WHEN** 导演尝试接管已被另一导演接管的角色
- **THEN** 命令被拒绝并返回 `DIRECTOR_TAKEOVER_CONFLICT`，控制权不变

#### Scenario: 结束接管

- **WHEN** 导演结束接管且角色仍有效
- **THEN** 角色保留接管结束状态并恢复原自主策略，恢复事件写入日志

### Requirement: 目标、条件与叙事状态

系统 SHALL 维护会话级叙事状态，包括当前节拍、目标、已完成事件、变量和导演备注。条件表达式 SHALL 只能读取已声明变量和权威事件；修改 SHALL 通过导演命令或节拍动作完成，并产生可重演事件。

#### Scenario: 目标完成

- **WHEN** 权威世界满足目标完成条件
- **THEN** 目标标记为完成并发布 `narrative.objectiveCompleted` 事件

#### Scenario: 未声明变量

- **WHEN** 条件表达式引用未声明变量
- **THEN** 制作包校验失败并指出变量名，会话不启动

#### Scenario: 导演设置目标

- **WHEN** 导演设置当前目标
- **THEN** 叙事状态更新，参与者和 Viewer 收到目标事件，变更进入事件日志

### Requirement: Take 标记与镜头轨道

系统 SHALL 允许导演在时间轴上标记 Take 的入点、出点和备注，并绑定一个或多个镜头轨道。Take SHALL 记录事件区间、相机、分辨率预设和叙事节拍；已保存 Take 的区间和轨道 SHALL 不可被原地修改，只能创建新版本。

#### Scenario: 创建 Take

- **WHEN** 导演在时间轴上标记入点、出点并选择相机轨道
- **THEN** 系统创建带事件区间和轨道信息的 Take，并发布 `director.takeCreated` 事件

#### Scenario: 空区间

- **WHEN** Take 出点早于或等于入点
- **THEN** 创建失败并返回 `TAKE_RANGE_INVALID`，不写入 Take

#### Scenario: 修改已有 Take

- **WHEN** 导演修改已保存 Take 的入点或出点
- **THEN** 系统创建新版本而不是覆盖原 Take，原版本仍可渲染

### Requirement: 导演视角与历史分支点

系统 SHALL 提供 live、ghost 和 resim 三种导演视角。live 跟随权威世界；ghost 只播放历史快照和事件且不修改世界；resim 从历史点重演并可建立新分支。分支 SHALL 创建独立会话标识，原会话和原事件日志保持不变。

#### Scenario: 切换 ghost

- **WHEN** 导演切换到 ghost 并拖动时间轴
- **THEN** 客户端显示对应历史状态，服务器世界不产生新事件

#### Scenario: 从历史点分支

- **WHEN** 导演在事件序号处建立分支
- **THEN** 系统创建继承该点状态的新会话，原会话事件序号和历史保持不变

#### Scenario: 分支上执行导演命令

- **WHEN** 导演在分支会话执行修改
- **THEN** 修改只写入分支日志，不修改父会话或制作包
