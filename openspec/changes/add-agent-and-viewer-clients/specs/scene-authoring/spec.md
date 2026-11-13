# Spec Delta

## ADDED Requirements

### Requirement: Agent 控制元数据

场景 SHALL 支持把角色标记为可由 Agent 控制、定义 Agent 出生点和可选的动作白名单，并 SHALL 拒绝不存在的角色引用、重复出生点标识和空动作名。没有标记的角色 SHALL NOT 被 Agent 自动占用。

#### Scenario: 标记可控角色

- **WHEN** 操作者把一个角色实体标记为 Agent 可控并保存场景
- **THEN** 场景查询返回该角色的可控标记，服务器装载后只将这些角色列为 Agent 可申请角色

#### Scenario: 无效引用

- **WHEN** Agent 控制元数据引用不存在的角色实体
- **THEN** 保存或装载失败并报告缺失实体标识，原场景文件不被破坏

#### Scenario: 动作白名单

- **WHEN** 场景为可控角色定义动作白名单
- **THEN** Agent 请求白名单内动作可以进入校验，白名单外动作返回 `ACTION_NOT_ALLOWED`

### Requirement: 查看端相机元数据

场景 SHALL 支持标记查看端可用相机、设置默认查看端相机，并保存相机的位置、朝向、视场角和可选跟随目标。默认查看端相机 SHALL 引用一个存在的、已标记为查看端可用的相机。

#### Scenario: 设置默认查看相机

- **WHEN** 操作者把一台相机标记为查看端可用并设为默认
- **THEN** 场景查询返回默认查看端相机标识，Viewer 加入后自动使用该相机

#### Scenario: 默认相机无效

- **WHEN** 默认查看端相机引用不存在的相机或未标记为查看端可用
- **THEN** 场景校验失败并报告相机标识，保存被拒绝

#### Scenario: 多台查看相机

- **WHEN** 场景包含多台查看端可用相机
- **THEN** Viewer 可以列出并切换这些相机，切换不修改场景活动相机

### Requirement: 运行时关卡与导航校验

场景 SHALL 在发布运行时装载前校验运行时关卡引用、查看端相机和 Agent 移动所需的导航数据。缺失关卡 SHALL 阻止 Viewer 装载；缺失导航数据 SHALL 允许只读查看但拒绝 Agent 移动。

#### Scenario: 关卡存在

- **WHEN** 场景引用一个可加载的运行时关卡
- **THEN** 校验通过并返回关卡路径，服务器和 Viewer 使用同一关卡标识

#### Scenario: 关卡缺失

- **WHEN** 场景引用的运行时关卡不存在或不可加载
- **THEN** 校验失败并返回 `LEVEL_NOT_FOUND`，Viewer 不进入空地图

#### Scenario: 导航缺失

- **WHEN** 场景没有导航数据但包含 Agent 可控角色
- **THEN** 校验返回警告，Viewer 仍可观看，Agent 移动命令返回 `NAVIGATION_UNAVAILABLE`

### Requirement: 运行时元数据兼容

场景运行时元数据 SHALL 为可选字段，缺失时使用兼容默认值：角色不可由 Agent 控制、无 Agent 出生点、当前活动相机作为查看端默认相机、关卡引用沿用现有 `LevelPath`。旧场景 SHALL 能继续加载和渲染。

#### Scenario: 旧场景加载

- **WHEN** 加载不包含任何 Agent 或 Viewer 元数据的旧场景
- **THEN** 场景正常加载，Agent 可申请角色列表为空，Viewer 使用现有活动相机

#### Scenario: 保存往返

- **WHEN** 包含运行时元数据的场景保存后重新加载
- **THEN** 可控角色、Agent 出生点、查看端相机和默认相机与保存前一致

#### Scenario: 未知字段

- **WHEN** 场景包含未来版本新增的运行时元数据字段
- **THEN** 当前版本保留未知字段或明确报告不支持，不静默丢弃并覆盖原场景
