# session-replay Specification

## Purpose
定义可验证的会话事件日志、确定性重演、历史分支和 Take 固化，使同一沙盒会话能从任意检查点重建并用于导演复盘与离线渲染。

## Requirements

### Requirement: 只追加会话事件日志

系统 SHALL 为每个沙盒会话维护只追加的 `journal.jsonl`，记录事件序号、模拟 tick、时间、事件类型、发起者、权威变更、输入和随机结果。成功写入后 SHALL 持久化并带校验和；已写入事件 SHALL NOT 被原地修改或删除。

#### Scenario: 追加权威事件

- **WHEN** 服务器接受一个修改世界的命令
- **THEN** 系统在应用变更前分配单调事件序号和 tick，并把可重演记录追加到日志

#### Scenario: 日志校验失败

- **WHEN** 读取到校验和不匹配或序号不连续的事件
- **THEN** 会话标记为不可精确重演并返回 `JOURNAL_CORRUPT`，不继续静默加载

#### Scenario: 结束会话

- **WHEN** 会话结束
- **THEN** 日志写入结束摘要并保持可读，后续导演操作进入新的分支或新会话

### Requirement: 初始快照与检查点

系统 SHALL 在会话开始时保存完整初始快照，并按配置间隔保存检查点。快照 SHALL 包含世界实体、参与者、叙事状态、随机种子、内容摘要和最近事件序号；重演 SHALL 从目标序号之前最近的检查点开始。

#### Scenario: 从检查点重演

- **WHEN** 客户端请求重演到事件序号
- **THEN** 系统加载不晚于该序号的最近检查点，并只重放其后的日志事件

#### Scenario: 缺少初始快照

- **WHEN** 会话目录缺少初始快照且日志无法独立重建
- **THEN** 重演被拒绝并返回 `SNAPSHOT_MISSING`，不返回近似结果

#### Scenario: 创建检查点

- **WHEN** 会话到达配置的检查点间隔
- **THEN** 系统写入带事件序号和内容摘要的新检查点，后续重演可使用它

### Requirement: 确定性模拟

系统 SHALL 使用固定模拟 tick、显式随机种子和稳定迭代顺序执行重演所需逻辑。事件、生成、随机和实体遍历 SHALL 不依赖帧率、墙钟时间或哈希容器迭代顺序；渲染线程和本地表现 SHALL NOT 改变权威结果。

#### Scenario: 相同输入重复重演

- **WHEN** 使用相同初始快照、事件日志、种子和构建重复重演
- **THEN** 目标序号的世界状态摘要一致

#### Scenario: 版本摘要不一致

- **WHEN** 重演环境的引擎、内容或规则摘要与录制时不同
- **THEN** 系统标记 `REPLAY_VERSION_MISMATCH` 并要求显式允许非精确重演

#### Scenario: 非确定操作

- **WHEN** 权威逻辑尝试使用未记录的随机源或墙钟时间
- **THEN** 自动化测试失败并指出违规调用点

### Requirement: Ghost 回放

系统 SHALL 支持 ghost 回放，把历史快照和事件以只读时间轴提供给导演和 Viewer。Ghost 客户端 SHALL 能播放、暂停和跳转，但 SHALL NOT 写入会话、触发级联模拟或影响 live 参与者。

#### Scenario: 跳转历史位置

- **WHEN** 导演把 ghost 时间轴跳转到某个事件序号
- **THEN** 客户端显示该序号对应的世界快照，服务器 live 状态不变

#### Scenario: Ghost 中提交命令

- **WHEN** 客户端在 ghost 模式提交修改命令
- **THEN** 命令被拒绝并返回 `REPLAY_READ_ONLY`，不产生事件

#### Scenario: Ghost 事件缺口

- **WHEN** 请求的历史序号早于保留窗口
- **THEN** 系统返回 `REPLAY_HISTORY_UNAVAILABLE` 并要求从可用检查点重新加载

### Requirement: 确定性重演与差异检测

系统 SHALL 支持从检查点重演到指定事件序号，并在关键检查点比较状态摘要。重演 SHALL NOT 写入原会话日志；状态摘要不一致时 SHALL 停止并报告首个差异事件、实体和字段。

#### Scenario: 精确重演

- **WHEN** 客户端请求重演到有效事件序号且版本摘要一致
- **THEN** 重演到达目标状态并返回状态摘要，标记为精确重演

#### Scenario: 状态差异

- **WHEN** 重演后的状态摘要与录制摘要不一致
- **THEN** 系统返回 `REPLAY_DIVERGED` 并指出首个差异，不继续覆盖后续状态

#### Scenario: 重演取消

- **WHEN** 导演取消进行中的重演
- **THEN** 重演停止并保留已产生的诊断，原会话和日志不受影响

### Requirement: 历史分支隔离

系统 SHALL 允许从指定事件序号创建分支会话，分支继承该点状态和必要日志摘要，但拥有独立会话标识、目录和后续日志。父会话 SHALL 保持不可变；分支 SHALL 记录父会话标识、分叉序号和创建者。

#### Scenario: 创建分支

- **WHEN** 导演从事件序号创建分支
- **THEN** 系统生成独立分支目录并返回分支会话标识和初始状态

#### Scenario: 分支写入

- **WHEN** 分支会话产生新事件
- **THEN** 事件只写入分支日志，父会话事件序号和摘要不变

#### Scenario: 分叉序号无效

- **WHEN** 请求序号不存在或早于保留窗口
- **THEN** 分支创建失败并返回 `BRANCH_POINT_UNAVAILABLE`，不创建目录

### Requirement: Take 固化与重演产物

系统 SHALL 把 Take 区间固化为独立产物，包含事件序号范围、初始快照引用、相机轨道、分辨率预设、叙事节拍、内容摘要和预期帧数。Take 产物 SHALL 可独立加载用于渲染，不依赖 live 会话继续运行。

#### Scenario: 固化 Take

- **WHEN** 导演保存一个有效 Take
- **THEN** 系统写入 Take 清单和必要的快照/事件引用，返回 Take 标识和内容摘要

#### Scenario: 缺失依赖

- **WHEN** Take 引用的快照、事件或相机不存在
- **THEN** 固化失败并返回 `TAKE_DEPENDENCY_MISSING`，不写不完整产物

#### Scenario: 独立加载 Take

- **WHEN** 渲染器加载 Take 产物
- **THEN** 渲染器获得区间、相机轨道和确定性快照，不要求 live 会话存在

### Requirement: 重演保留、压缩与权限

系统 SHALL 按配置保留事件日志、检查点和分支，并在压缩时保留可重演边界。只有 owner 或 director SHALL 能创建分支、执行重演或固化 Take；Viewer 只能读取被授权的 ghost 时间轴。

#### Scenario: 日志压缩

- **WHEN** 日志超过保留上限
- **THEN** 系统按检查点边界压缩旧事件，最近检查点和后续事件仍可重演

#### Scenario: 无权限重演

- **WHEN** performer 或 agent 请求创建分支
- **THEN** 命令被拒绝并返回 `FORBIDDEN`，不创建目录或事件

#### Scenario: 保留窗口耗尽

- **WHEN** 请求的重演点已被压缩且没有可用检查点
- **THEN** 系统返回 `REPLAY_HISTORY_UNAVAILABLE`，不伪造近似状态
