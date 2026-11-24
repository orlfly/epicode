# Spec Delta

## ADDED Requirements

### Requirement: 运行时客户端脚本

系统 SHALL 提供 `Scripts/Viewer.sh`、`Scripts/AgentSmoke.sh` 和 `Scripts/ViewerSmoke.sh`。查看端脚本 SHALL 支持服务器地址、查看者名称、加入令牌、相机模式和额外 Unreal 参数；Agent 冒烟脚本 SHALL 不启动 Unreal 进程，只通过控制 API 完成加入、租约、移动、观察和释放。

#### Scenario: 启动查看端

- **WHEN** 操作者执行 `Scripts/Viewer.sh --server 127.0.0.1:7796 --name viewer-a`
- **THEN** 脚本启动 `UnrealEditor -game` 查看端，传入服务器地址和查看者身份，并把日志写入 `Saved/Logs/`

#### Scenario: 无效查看端参数

- **WHEN** 操作者传入非法服务器地址或缺少查看者令牌
- **THEN** 脚本在启动引擎前退出并返回非零状态，输出可读错误

#### Scenario: Agent 冒烟不启动引擎

- **WHEN** 操作者执行 `Scripts/AgentSmoke.sh`
- **THEN** 脚本通过 HTTP/WebSocket 完成 Agent 控制闭环，不启动新的 Unreal 进程，并输出通过或失败摘要

#### Scenario: 查看端冒烟

- **WHEN** 操作者执行 `Scripts/ViewerSmoke.sh`
- **THEN** 脚本启动服务器和查看端，验证相机、世界表现和只读拒绝，并返回与结果一致的状态码

### Requirement: 运行时进程角色

系统 SHALL 在启动日志中明确当前进程角色为 Editor、Headless、Server、Viewer 或 Agent。Agent SHALL NOT 加载 Unreal 运行时；Viewer SHALL 使用 Game 客户端模式且不启动控制 API；Server SHALL 以服务器模式启动并启动控制 API。

#### Scenario: 查看端日志

- **WHEN** Viewer 启动
- **THEN** 日志包含查看者标识、服务器地址、相机模式和 `Epicode API stays disabled in game client mode`

#### Scenario: Agent 日志

- **WHEN** Agent 冒烟运行
- **THEN** 日志包含参与者标识、会话标识、租约标识和命令摘要，不包含 Unreal 启动日志

#### Scenario: 服务器日志

- **WHEN** 服务器启动并接受 Viewer 和 Agent
- **THEN** 日志分别记录 Viewer 加入、Agent 加入、查看者槽位和 Agent 租约，不把 Viewer 计为表演者
