# Spec Delta

## MODIFIED Requirements

### Requirement: 模块与插件边界

系统 SHALL 将功能拆分为运行时核心、资产导入、控制 API、制作界面、视频渲染、沙盒运行时和导演重演七个可独立加载的模块。制作界面 SHALL 仅在 Editor 目标中加载，运行时模块 SHALL NOT 依赖 Editor 专属模块。沙盒与导演模块 SHALL 可在无界面服务器加载。系统 SHALL NOT 修改 `/usr/local/unreal` 下的任何文件。

#### Scenario: 编辑器目标加载全部模块

- **WHEN** 启动 `EpicodeVideoEditor`
- **THEN** 核心、导入、控制 API、制作界面、渲染、沙盒和导演模块均完成加载，启动日志列出各模块名称与版本

#### Scenario: 无界面目标不加载制作界面

- **WHEN** 以 `-nullrhi -unattended` 启动无界面控制服务
- **THEN** 制作界面模块不被加载，控制 API、核心、沙盒和导演运行时模块可用

#### Scenario: 引擎目录保持只读

- **WHEN** 完成构建、启动和测试流程后检查 `/usr/local/unreal`
- **THEN** 引擎安装目录中没有新增、修改或删除的文件

#### Scenario: 沙盒模块独立加载

- **WHEN** 以只读 Viewer 或纯 Agent 模式启动
- **THEN** 沙盒模块完成世界规则与权限加载，导演模块可被禁用而不影响 Viewer 和 Agent

## ADDED Requirements

### Requirement: 确定性运行时配置

系统 SHALL 提供固定模拟 tick 率、随机种子策略、事件日志目录、检查点间隔、重演保留窗口、实体预算和事件预算配置。配置 SHALL 在启动时校验，非法值 SHALL 阻止沙盒会话启动并报告字段与允许范围。

#### Scenario: 默认确定性配置

- **WHEN** 引擎以默认配置启动
- **THEN** `system.health` 报告固定 tick 率、种子策略、日志目录和重演保留窗口

#### Scenario: 非法 tick 率

- **WHEN** 配置的模拟 tick 率为零或负值
- **THEN** 启动诊断失败并指出配置字段，沙盒会话不可创建

#### Scenario: 重演保留窗口过小

- **WHEN** 检查点间隔大于保留窗口
- **THEN** 启动配置被拒绝，避免产生无法重演的日志

### Requirement: 导演与重演运行脚本

系统 SHALL 提供非交互式 `Scripts/Director.sh`、`Scripts/Replay.sh` 和相应冒烟脚本。脚本 SHALL 支持服务器地址、制作包、会话、事件序号、输出目录和额外 Unreal 参数，并把日志写入 `Saved/Logs/`；非法参数 SHALL 非零退出。

#### Scenario: 启动导演客户端

- **WHEN** 操作者执行 `Scripts/Director.sh --server <host:port> --production <id>`
- **THEN** 客户端以 Director 进程角色连接服务器，日志包含服务器地址和制作包标识

#### Scenario: 执行重演冒烟

- **WHEN** 操作者执行 `Scripts/ReplaySmoke.sh`
- **THEN** 脚本创建会话、产生事件、重演到目标序号、创建分支并返回 0，日志写入 `Saved/Logs/replay-smoke.log`

#### Scenario: 非法重演目标

- **WHEN** 客户端请求不存在或早于保留窗口的事件序号
- **THEN** 脚本以非零状态退出并输出 `REPLAY_HISTORY_UNAVAILABLE`

### Requirement: 构建与内容摘要

构建与启动 SHALL 生成引擎版本、项目版本、规则 schema 和内容目录摘要，并在会话快照、Take 和重演结果中记录。摘要不一致 SHALL 触发明确的非精确重演或渲染错误，不得静默继续。

#### Scenario: 记录构建摘要

- **WHEN** 会话启动
- **THEN** `system.health` 和初始快照包含引擎、项目、规则和内容摘要

#### Scenario: 摘要变化

- **WHEN** 使用不同内容摘要打开旧会话重演
- **THEN** 系统报告 `REPLAY_VERSION_MISMATCH` 并要求显式允许非精确重演

#### Scenario: 摘要生成失败

- **WHEN** 内容目录不可读导致摘要无法生成
- **THEN** 沙盒会话创建被拒绝并返回 `CONTENT_DIGEST_UNAVAILABLE`

### Requirement: 沙盒与导演诊断

启动日志和 `system.health` SHALL 报告沙盒模块、导演模块、事件日志、检查点、重演队列、导演连接数和预算状态。日志 SHALL 标记 `Director` 和 `Replay` 进程角色，且不记录完整参与者令牌。

#### Scenario: 健康检查包含新模块

- **WHEN** 客户端调用 `system.health`
- **THEN** 返回沙盒与导演模块版本、事件日志可写性、重演能力和当前预算占用

#### Scenario: 进程角色标记

- **WHEN** 以导演或重演模式启动
- **THEN** 启动日志包含 `Epicode process role: Director` 或 `Epicode process role: Replay`

#### Scenario: 日志脱敏

- **WHEN** 导演命令被审计记录
- **THEN** 日志包含命令、参与者和结果，不包含完整参与者令牌
