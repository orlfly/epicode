# Spec Delta

## ADDED Requirements

### Requirement: 编辑脚本校验

系统 SHALL 提供 `script.validate` 命令，用于校验版本化 JSON 编辑脚本而不修改场景。校验 SHALL 覆盖 `schemaVersion`、`operations` 数量、方法白名单、参数类型和范围、实体存在性、`$clientId` 引用、资产路径和脚本权限。校验成功 SHALL 返回规范化脚本、影响实体摘要和警告；校验失败 SHALL 返回稳定的领域错误和字段路径。校验 SHALL NOT 创建、修改或删除实体。

#### Scenario: 校验合法脚本

- **WHEN** 客户端提交一个只包含 `character.spawn` 和 `entity.setTransform` 的合法脚本
- **THEN** `script.validate` 返回成功、规范化后的 operations、将创建的实体数量和警告列表，场景保持不变

#### Scenario: 校验非法方法

- **WHEN** 脚本包含白名单之外的 `import.file` 或 `render.submit`
- **THEN** `script.validate` 返回 `SCRIPT_METHOD_NOT_ALLOWED`，包含被拒绝的方法和 operation 索引，场景保持不变

#### Scenario: 校验资产缺失

- **WHEN** 脚本引用不存在的资产路径
- **THEN** `script.validate` 返回 `ASSET_NOT_FOUND`，包含字段路径和资产路径，场景保持不变

### Requirement: 编辑脚本应用

系统 SHALL 提供 `script.apply` 命令，在同一场景事务中执行经过校验的编辑脚本。执行 SHALL 支持 `clientId -> entityId` 映射，使后续 operation 可以通过 `$clientId` 引用前面创建的实体。任一 operation 失败 SHALL 回滚整个事务，SHALL NOT 留下部分实体或属性修改。成功后 SHALL 返回每个 operation 的结果、受影响实体和事务标识，并 SHALL 能通过一次撤销恢复全部修改。

#### Scenario: 应用创建和修改

- **WHEN** 客户端提交一个创建角色、设置其变换并修改现有灯光强度的脚本
- **THEN** 所有 operation 在一个事务中成功，返回新角色 ID、灯光修改结果和受影响实体列表

#### Scenario: 部分失败回滚

- **WHEN** 脚本第二个 operation 因实体不存在失败
- **THEN** 第一个 operation 创建的角色也被回滚，场景与执行前一致，返回失败 operation 和领域错误

#### Scenario: 一次撤销

- **WHEN** 脚本应用成功后操作者执行一次撤销
- **THEN** 脚本创建、修改和删除的全部实体恢复到执行前状态

### Requirement: 编辑脚本引用和安全限制

`script.apply` SHALL 只允许白名单内的场景编辑方法，SHALL NOT 执行嵌套 `scene.transaction`、`import.*`、`render.*`、`system.*`、`director.*`、`sandbox.*`、控制台命令、Python/Lua、文件写入或网络请求。脚本 SHALL 最多包含 50 个 operation，最多影响 100 个实体。`$clientId` SHALL 只引用同一脚本中更早的 operation。资产路径 SHALL 以 `/Game/` 开头并存在。违反限制 SHALL 返回稳定领域错误，且不执行任何 operation。

#### Scenario: 前向引用

- **WHEN** operation 在 `$hero` 创建之前引用它
- **THEN** `script.apply` 返回 `SCRIPT_REFERENCE_INVALID`，不执行任何 operation

#### Scenario: 超出操作上限

- **WHEN** 脚本包含 51 个 operation
- **THEN** `script.apply` 返回 `SCRIPT_LIMIT_EXCEEDED`，场景保持不变

#### Scenario: 禁止嵌套事务

- **WHEN** 脚本 operation 使用 `scene.transaction`
- **THEN** `script.apply` 返回 `SCRIPT_METHOD_NOT_ALLOWED`，不执行任何 operation

### Requirement: 编辑脚本审计

系统 SHALL 在 `script.apply` 成功或失败后写入审计记录，包含时间、脚本摘要、operation 数量、受影响实体、结果状态、错误码和耗时。审计记录 SHALL NOT 包含 API 令牌、对话密钥或完整资产二进制。审计日志 SHALL 写入 `Saved/Epicode/Script/audit.jsonl`，写入失败 SHALL NOT 回滚已成功的场景事务，但 SHALL 返回警告。

#### Scenario: 成功审计

- **WHEN** 脚本应用成功
- **THEN** 审计日志追加一条成功记录，包含 operation 数量和受影响实体摘要

#### Scenario: 失败审计

- **WHEN** 脚本应用因参数错误失败
- **THEN** 审计日志追加一条失败记录，包含错误码和失败 operation 索引，不包含敏感数据

#### Scenario: 审计写入失败

- **WHEN** 审计日志目录不可写但场景事务已成功
- **THEN** `script.apply` 返回成功并附带 `AUDIT_WRITE_FAILED` 警告，不回滚场景
