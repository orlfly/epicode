# Spec Delta

## Purpose

定义多人会话在服务器目标不可用、网络质量采样和健康诊断方面的可观测行为，使回退模式和基线数据可查询、可复验。

## ADDED Requirements

### Requirement: 服务器模式诊断

系统 SHALL 在 `system.health.network` 和 `session.snapshot` 中报告 `serverMode`、`serverTargetSupported`、`fallbackReason` 和 `serverBinary`。`serverMode` SHALL 为 `dedicated` 或 `editor-fallback` 之一；使用回退路径时 SHALL NOT 报告 `dedicated`。

#### Scenario: 专用服务器可用

- **WHEN** 以 `EpicodeVideoServer` 启动会话
- **THEN** `serverMode` 为 `dedicated`，`serverTargetSupported` 为 true，`fallbackReason` 为空

#### Scenario: 编辑器回退

- **WHEN** 当前引擎发行版不支持 Server 目标且 `Server.sh` 使用 `UnrealEditor-Cmd -server`
- **THEN** `serverMode` 为 `editor-fallback`，`serverTargetSupported` 为 false，`fallbackReason` 非空并包含引擎返回原因

#### Scenario: 回退不可用

- **WHEN** 既没有 Server 二进制也没有可执行的编辑器命令行
- **THEN** `Server.sh` 以非零状态退出，输出尝试的二进制路径和修复建议，不启动监听

### Requirement: 网络基线采样

网络冒烟 SHALL 记录 `sampleCount`、`maxRecentDrift`、`rttMs`、`packetLoss` 和 `serverMode`，并在摘要中输出。系统 SHALL NOT 把本地回环数据标记为广域网基线；未采样的指标 SHALL 明确为 `not-measured`。

#### Scenario: 本地回环冒烟

- **WHEN** `NetworkSmoke.sh` 完成双客户端本地回环
- **THEN** 摘要包含样本数、最近漂移和服务器模式，RTT 与丢包标记为 `not-measured` 或实际采样值

#### Scenario: 网络模拟采样

- **WHEN** 操作者通过 `tc netem` 或等价工具注入延迟和丢包
- **THEN** 摘要记录实际 `rttMs` 和 `packetLoss`，并在文档中标注采样命令和阈值

### Requirement: 证据一致性

`Docs/Acceptance.md`、`Docs/Multiplayer.md` 和网络冒烟摘要 SHALL 使用同一组测试数量、样本数和漂移阈值。任一数字不一致时 SHALL 视为验收失败，不得只更新其中一处。

#### Scenario: 测试数量一致

- **WHEN** 全量测试完成
- **THEN** 验收文档、任务记录和测试日志报告相同的通过数，失败数为零

#### Scenario: 漂移数字一致

- **WHEN** 网络冒烟完成
- **THEN** `network-smoke.log` 摘要、`Docs/Multiplayer.md` 基线和 `Docs/Acceptance.md` 使用同一组样本数和最大漂移
