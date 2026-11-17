# conversational-editing Specification

## Purpose
定义聚焦编辑器的对话面板、DeepSeek Harness sidecar 适配层、上下文构建和受控编辑脚本闭环。模型回复只能生成经过 schema、白名单和引用校验的 JSON 脚本，修改必须可预览、可拒绝、可确认、可一次撤销并写入审计。

## Requirements

### Requirement: 对话窗口

聚焦编辑器 SHALL 在右侧提供可折叠对话面板，包含消息历史、输入框、发送、停止、重试、清空、新建会话和设置入口。对话面板 SHALL 支持流式显示模型回复，SHALL 在请求进行中保持视口和菜单可操作，SHALL 允许用户随时停止生成。对话面板 SHALL 在关闭后保留当前会话内容，重新打开时恢复。

#### Scenario: 发送消息

- **WHEN** 操作者在输入框输入“在主街放一个角色”并发送
- **THEN** 用户消息立即出现在历史中，助手回复逐段流式显示，发送按钮变为停止按钮

#### Scenario: 停止生成

- **WHEN** 操作者在流式回复中点击停止
- **THEN** 后端请求被取消，已收到的文本保留，界面恢复可发送状态

#### Scenario: 重新打开面板

- **WHEN** 操作者关闭对话面板后再次打开
- **THEN** 当前会话的消息历史、滚动位置和未应用的脚本预览恢复

### Requirement: DeepSeek Harness SDK 适配层

系统 SHALL 通过 `IConversationBackend` 适配层连接对话后端，首个后端 SHALL 使用 Python 包 `deepseek-harness-sdk`。Unreal SHALL NOT 直接导入 Python 包；系统 SHALL 通过隔离 venv 中的 sidecar 加载 SDK，并使用 stdio JSONL（默认）或本地 HTTP 与 Unreal 通信。对话 UI SHALL NOT 直接依赖 SDK 类型、网络细节或供应商协议。后端配置 SHALL 包含启用开关、后端类型、sidecar 命令/脚本、通信模式、模型、密钥文件、超时、空闲回收和最大重试次数。

#### Scenario: 使用 DeepSeek 后端

- **WHEN** 配置 `Backend=deepseek-harness-sidecar`，sidecar 启动成功且 `deepseek-harness-sdk` 已安装在隔离 venv 中
- **THEN** 对话面板通过 sidecar 发送消息并显示流式回复

#### Scenario: sidecar 启动失败

- **WHEN** sidecar 进程无法启动、Python 版本不兼容或 SDK import 失败
- **THEN** 系统显示 stderr 摘要、修复命令和期望路径，允许切换到 OpenAI 兼容后端或继续手工编辑

#### Scenario: SDK 包不可用

- **WHEN** `deepseek-harness-sdk` 无法从配置的索引或 wheel 安装
- **THEN** 系统显示安装失败原因和所需索引/凭据提示，不进入“后端已连接”状态

#### Scenario: 后端替换

- **WHEN** 实现替换为另一个符合 `IConversationBackend` 的后端
- **THEN** 对话 UI、脚本校验、预览和应用流程不变

#### Scenario: sidecar 崩溃

- **WHEN** sidecar 在请求过程中退出
- **THEN** 对话面板显示崩溃和重试入口，当前请求标记失败，视口、库和手工编辑不受影响

### Requirement: 对话配置和密钥安全

系统 SHALL 从环境变量或 `Saved/Epicode/deepseek.env`（权限 0600）读取 DeepSeek 凭据，SHALL NOT 把密钥写入仓库、日志、对话记录、脚本或错误消息。对话设置面板 SHALL 默认掩码显示密钥，只有显式操作才显示有限时间。日志和审计 SHALL 只记录后端名、模型名、耗时和结果状态。

#### Scenario: 密钥缺失

- **WHEN** 后端需要密钥但环境变量和密钥文件都不存在
- **THEN** 对话面板显示缺失密钥和期望路径，不发起请求，不把空密钥发送到网络

#### Scenario: 密钥掩码

- **WHEN** 设置面板显示已加载的密钥
- **THEN** 只显示固定掩码或末四位，复制操作要求显式确认

#### Scenario: 日志脱敏

- **WHEN** 请求失败并写入日志
- **THEN** 日志不包含密钥、Authorization 头或完整用户消息中的敏感内容

### Requirement: 对话上下文构建

系统 SHALL 为每次请求构建受预算约束的上下文，包含当前场景摘要、当前选择、本地库摘要、允许的编辑脚本 schema、约束和最近对话。上下文 SHALL NOT 包含密钥、完整资产二进制、未选择的私有数据或其他会话数据。预算由 `MaxContextTokens` 控制，超出时 SHALL 在本地裁剪历史对话和库列表，优先保留当前场景、当前选择和命令 schema。

#### Scenario: 场景上下文

- **WHEN** 当前场景有 20 个角色、30 个物品、5 盏灯和 3 台相机
- **THEN** 请求包含四类实体的 ID、名称、变换和关键属性摘要，不包含网格或贴图二进制

#### Scenario: 当前选择

- **WHEN** 操作者选中一个灯光后发送“把它调亮一点”
- **THEN** 上下文包含该灯光的 ID、类型、当前强度和变换，模型可以生成针对该实体的脚本

#### Scenario: 上下文超限

- **WHEN** 场景和库摘要超过 `MaxContextTokens`
- **THEN** 系统裁剪历史对话和库列表，保留当前场景、选择和 schema，并在对话面板显示一次“上下文已裁剪”提示

### Requirement: 编辑脚本格式

模型生成的场景修改 SHALL 使用版本化 JSON 编辑脚本，而不是任意代码。脚本 SHALL 包含 `schemaVersion`、`summary`、`requiresConfirmation` 和 `operations`；每个 operation SHALL 包含可选 `clientId`、`method` 和 `params`。`clientId` SHALL 唯一，后续 operation 可以用 `$clientId` 引用前面创建实体的 ID。脚本 SHALL 支持纯问答回复：当用户只是提问或没有可执行修改时，助手 SHALL 只返回文本，不生成 operation。

#### Scenario: 生成放置脚本

- **WHEN** 用户要求“在原点放一个角色，再放一台相机”
- **THEN** 助手返回包含 `character.spawn` 和 `camera.create` 的脚本，每个 operation 有唯一 `clientId` 和合法参数

#### Scenario: 引用新实体

- **WHEN** 脚本先创建角色，再设置其变换
- **THEN** 后续 operation 使用 `$hero` 引用前一个 `clientId`，校验器解析为实际实体 ID

#### Scenario: 纯问答

- **WHEN** 用户问“当前场景有多少个角色”
- **THEN** 助手只显示回答文本，不生成编辑脚本，不弹出应用确认

### Requirement: 编辑脚本方法白名单

系统 SHALL 只允许 `Docs/Assistant.md` 中 `allowedMethods` 列出的场景编辑方法。初始白名单限于角色、物品、灯光、摄像的创建/更新/删除，以及实体变换、可见性、重命名和删除；`scene.save` 仅允许显式保存。系统 SHALL 拒绝任何不在白名单中的方法。

#### Scenario: 拒绝非白名单方法

- **WHEN** 脚本包含 `import.file`、`render.submit` 或任意控制台命令
- **THEN** 校验失败，脚本不可应用，对话面板显示被拒绝的方法和原因

#### Scenario: 拒绝任意代码

- **WHEN** 模型返回 Python、Lua、shell 或 `Exec` 命令而不是 JSON 脚本
- **THEN** 系统不执行任何内容，只显示“未生成可应用脚本”并允许重试

### Requirement: 编辑脚本执行边界

系统 SHALL NOT 执行嵌套 `scene.transaction`、`import.*`、`render.*`、`system.*`、`director.*`、`sandbox.*`、控制台命令、Python/Lua、文件写入或网络请求。脚本执行 SHALL 限制在单次场景事务和最多 50 个 operation、100 个受影响实体。违反限制 SHALL 不执行任何 operation。

#### Scenario: 拒绝嵌套事务

- **WHEN** 脚本 operation 使用 `scene.transaction`
- **THEN** 校验失败，系统提示脚本执行器已经提供单事务包装

#### Scenario: 拒绝跨域操作

- **WHEN** 脚本试图导入资产、提交渲染、修改系统设置或调用导演/沙盒命令
- **THEN** 系统拒绝整个脚本，不执行任何 operation，场景保持不变

### Requirement: 编辑脚本校验

系统 SHALL 在应用前校验脚本的 `schemaVersion`、operation 数量、方法白名单、参数类型、数值范围、实体存在性、`$clientId` 引用、资产路径和权限。脚本 SHALL 最多包含 50 个 operation，最多影响 100 个实体。资产路径 SHALL 以 `/Game/` 开头并存在于资产注册表或本地库索引中。无效脚本 SHALL NOT 修改场景。

#### Scenario: 参数越界

- **WHEN** 脚本把相机视场角设置为 500
- **THEN** 校验失败，显示字段路径和允许范围，脚本不可应用

#### Scenario: 资产不存在

- **WHEN** 脚本引用 `/Game/Epicode/Imported/Characters/Missing`
- **THEN** 校验失败，显示缺失资产和建议从本地库选择，不创建实体

#### Scenario: 引用前向引用

- **WHEN** 脚本在创建 `$hero` 之前就引用它
- **THEN** 校验失败，显示引用错误，脚本不可应用

### Requirement: 脚本预览和确认

系统 SHALL 在应用脚本前显示差异预览，包含新建实体、修改实体及字段、删除实体、资产路径、影响数量和警告。破坏性操作（删除、批量删除、覆盖已有实体、修改活动相机）SHALL 要求二次确认。用户拒绝或关闭预览时 SHALL NOT 修改场景。

#### Scenario: 预览新建和修改

- **WHEN** 脚本创建一个角色并修改一盏灯
- **THEN** 预览显示“新建 1 个角色”“修改 1 盏灯：强度 5 → 8”，并提供应用/取消按钮

#### Scenario: 拒绝破坏性操作

- **WHEN** 脚本删除 3 个实体且用户取消确认
- **THEN** 场景不变，对话面板保留脚本和取消状态

#### Scenario: 预览高亮

- **WHEN** 预览包含修改现有实体
- **THEN** 视口高亮受影响实体，取消预览后恢复原选择

### Requirement: 脚本应用和撤销

系统 SHALL 通过单次场景事务应用经过确认的脚本。应用成功后，所有修改 SHALL 能通过一次撤销恢复；应用失败 SHALL 回滚全部修改并显示领域错误。脚本执行结果 SHALL 写入审计日志，包含时间、会话 ID、模型、后端、脚本、结果、耗时和确认状态，不包含密钥。

#### Scenario: 应用成功

- **WHEN** 用户确认应用一个合法脚本
- **THEN** 场景按脚本创建/修改/删除实体，视口和对象列表刷新，一次 Ctrl+Z 恢复全部修改

#### Scenario: 应用部分失败

- **WHEN** 脚本第 3 个 operation 因实体被删除而失败
- **THEN** 整个事务回滚，前两个 operation 不留下实体或属性修改，对话显示失败原因

#### Scenario: 审计记录

- **WHEN** 脚本应用成功
- **THEN** `Saved/Epicode/Chat/audit.jsonl` 追加一条记录，包含脚本和结果摘要，不包含密钥或完整敏感文本

### Requirement: 生成失败和修复

当模型输出无法解析、schema 不匹配或校验失败时，系统 SHALL 显示具体错误，SHALL 允许用户重试或让模型修复。自动修复 SHALL 最多尝试 `MaxRepairAttempts` 次，每次把校验错误反馈给模型；超过次数后 SHALL 停止并保留最后一次错误，SHALL NOT 自动应用任何部分脚本。

#### Scenario: 非法 JSON

- **WHEN** 模型返回缺少闭合括号的 JSON
- **THEN** 系统显示解析错误，提供“重试”和“让模型修复”，不修改场景

#### Scenario: 修复成功

- **WHEN** 模型根据校验错误修正了资产路径
- **THEN** 新脚本重新进入预览，用户确认后才应用

#### Scenario: 修复次数耗尽

- **WHEN** 连续 `MaxRepairAttempts` 次修复仍失败
- **THEN** 系统停止自动修复，显示最后一次错误，场景保持不变

### Requirement: 对话降级和离线

当对话后端不可用、超时或被禁用时，聚焦编辑器 SHALL 继续支持手工编辑、视口、本地库和场景保存。对话面板 SHALL 显示后端状态、错误原因和重试入口，SHALL NOT 阻塞其他面板或反复自动重试。

#### Scenario: 后端超时

- **WHEN** 请求超过 `RequestTimeoutSeconds`
- **THEN** 对话显示超时，停止按钮恢复为发送按钮，视口和手工编辑仍可用

#### Scenario: 后端禁用

- **WHEN** 配置 `bEnabled=false`
- **THEN** 对话面板显示“对话已禁用”和设置入口，不发起网络请求

#### Scenario: 离线编辑

- **WHEN** 没有网络连接
- **THEN** 本地库、四类实体编辑、保存和撤销正常工作

### Requirement: 对话历史和隐私

系统 SHALL 把对话历史保存在 `Saved/Epicode/Chat/<conversationId>.json`，SHALL 允许新建、清空和删除会话。历史 SHALL 只保存在本地，SHALL NOT 自动上传到云端。用户清空会话时 SHALL 删除本地消息和关联脚本预览；审计日志按配置保留，且不包含密钥。

#### Scenario: 本地持久化

- **WHEN** 操作者关闭并重新打开聚焦编辑器
- **THEN** 最近的对话会话从本地文件恢复，消息顺序和脚本预览保持

#### Scenario: 清空会话

- **WHEN** 操作者点击清空并确认
- **THEN** 当前会话消息、脚本预览和临时上下文被删除，审计日志按保留策略处理

#### Scenario: 不上传历史

- **WHEN** 对话后端请求只包含当前上下文和最近消息
- **THEN** 系统不发送未选择的旧会话或其他本地文件

### Requirement: 对话响应性

对话流式更新、脚本校验、预览生成和审计写入 SHALL 在 100 毫秒内返回控制权，不得阻塞视口、菜单或对象列表。长脚本预览 SHALL 分批构建，取消操作 SHALL 立即停止 UI 更新。

#### Scenario: 长回复

- **WHEN** 模型流式返回数千字回复
- **THEN** 对话面板逐段追加，视口仍可旋转和选择

#### Scenario: 大脚本预览

- **WHEN** 脚本包含 50 个 operation
- **THEN** 预览分批显示，应用按钮在校验完成后才启用，界面不冻结
