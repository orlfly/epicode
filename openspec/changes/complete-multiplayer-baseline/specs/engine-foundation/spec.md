# Spec Delta

## ADDED Requirements

### Requirement: 构建目标降级与诊断

系统 SHALL 在构建或启动服务器和客户端时报告请求目标、实际二进制、运行模式和回退原因。独立 Server 目标不可用时，脚本 SHALL 使用 `UnrealEditor-Cmd -server` 并把 `serverMode` 标记为 `editor-fallback`；Game 客户端启动失败时 SHALL 回退到 `UnrealEditor -game` 或返回可诊断的非零退出码。系统 SHALL NOT 修改 `/usr/local/unreal`。

#### Scenario: 引擎不支持 Server 目标

- **WHEN** 操作者构建 `EpicodeVideoServer` 而当前引擎发行版返回不支持 Server 目标
- **THEN** 构建脚本以非零状态退出，日志包含引擎原始错误、请求目标和 `serverMode=editor-fallback`，不把失败报告为成功

#### Scenario: Game 客户端启动失败

- **WHEN** 打包的 `EpicodeVideo` Game 二进制在启动阶段崩溃
- **THEN** 客户端脚本记录崩溃日志，回退到 `UnrealEditor -game` 或返回非零退出码并输出回退命令，不静默挂起

#### Scenario: 编辑器构建保持不变

- **WHEN** 操作者执行现有 `Scripts/Build.sh`
- **THEN** 脚本继续构建 `EpicodeVideoEditor` Development 目标，退出码和日志路径与当前行为一致

### Requirement: 构建回退可复验

系统 SHALL 为服务器和客户端回退路径提供非交互式验证命令，并在 `Saved/Logs/` 写入包含请求目标、实际二进制、回退原因和健康检查结果的日志。验证命令 SHALL 在回退不可用时返回非零退出码。

#### Scenario: 服务器回退验证

- **WHEN** 操作者运行服务器回退验证
- **THEN** 日志包含 `serverMode=editor-fallback`、实际二进制路径和 `system.health.network` 的服务器模式字段

#### Scenario: 客户端回退验证

- **WHEN** 操作者运行客户端回退验证
- **THEN** 日志包含 `clientMode=editor-game` 或 `clientMode=game-binary`、实际二进制路径和连接结果

#### Scenario: 回退不可用

- **WHEN** 服务器和编辑器命令行都不可执行
- **THEN** 验证命令返回非零退出码，输出缺失路径和修复建议，不生成通过摘要
