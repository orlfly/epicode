# world-viewing Specification

## Purpose
让只读的世界查看客户端连接服务器、控制自己的相机并渲染运行时世界，同时不能占有角色或修改会话和世界状态。

## Requirements

### Requirement: Viewer 参与者

系统 SHALL 允许只读查看者加入活动会话，并分配独立于表演者角色的 Viewer 参与者记录。Viewer SHALL NOT 占用表演者角色或角色槽位，角色耗尽时仍可在查看者槽位未满时加入。

#### Scenario: 查看者加入

- **WHEN** 查看客户端使用服务器认可的查看者身份连接一个活动会话
- **THEN** 服务器创建 Viewer 参与者并返回会话标识、查看者标识和当前世界版本，不分配角色

#### Scenario: 角色已耗尽

- **WHEN** 所有表演者角色都已被占用但查看者槽位未满
- **THEN** 服务器仍允许 Viewer 加入，Viewer 可以观看但不能控制任何角色

#### Scenario: 查看者槽位已满

- **WHEN** 查看者数量已达到配置上限
- **THEN** 服务器拒绝新的 Viewer 加入并返回 `VIEWER_SLOTS_FULL`，现有查看者和表演者不受影响

#### Scenario: 未授权查看者

- **WHEN** 客户端声明 Viewer 角色但没有提供服务器认可的查看者身份
- **THEN** 服务器拒绝加入并返回 `VIEWER_NOT_AUTHORIZED`，不创建参与者记录

### Requirement: Spectator 相机

系统 SHALL 为 Viewer 生成只用于观察的 Spectator 相机，不生成可移动的表演角色。相机位置和朝向 SHALL 由查看端本地控制，不改变服务器角色和世界状态。

#### Scenario: 生成观察相机

- **WHEN** Viewer 完成加入
- **THEN** 客户端获得 Spectator 相机并看到会话世界，不占有任何角色

#### Scenario: 相机不影响角色

- **WHEN** Viewer 移动或旋转相机
- **THEN** 服务器角色位置、朝向和所有权保持不变

#### Scenario: 本地相机独立

- **WHEN** 两个 Viewer 同时观看同一会话
- **THEN** 每个 Viewer 使用自己的相机位置和朝向，互不覆盖

### Requirement: 查看相机模式

系统 SHALL 支持自由飞行、跟随角色、固定位置和场景预置相机四种查看模式，并允许 Viewer 在模式之间切换。跟随模式 SHALL 只改变相机目标，不改变目标角色状态。

#### Scenario: 自由飞行

- **WHEN** Viewer 选择自由飞行模式并移动相机
- **THEN** 相机在本地三维空间移动，不发送角色移动请求

#### Scenario: 跟随角色

- **WHEN** Viewer 选择一个角色并进入跟随模式
- **THEN** 相机持续对准该角色的当前权威位置，角色本身不被控制

#### Scenario: 使用预置相机

- **WHEN** Viewer 选择一个场景预置相机
- **THEN** 相机切换到该预置的位置、朝向和视场角

#### Scenario: 目标角色离开

- **WHEN** 跟随目标离开会话或被销毁
- **THEN** 相机保持最后位置并提示目标不可用，Viewer 可以重新选择目标

### Requirement: 运行时世界表现

系统 SHALL 让 Viewer 看到与服务器会话一致的运行时世界，至少包括角色、角色外观、角色移动、表演状态、道具、建造物、物品、灯光和环境。Viewer 加入时 SHALL 收到当前世界快照，之后接收增量变化。

#### Scenario: 加入时世界快照

- **WHEN** Viewer 在会话进行中加入
- **THEN** 客户端收到当前角色、世界实体、环境、灯光和活动相机的完整表现快照

#### Scenario: 角色移动与表演

- **WHEN** 服务器中的角色移动或更新表演状态
- **THEN** Viewer 在可接受延迟内看到角色移动和表演变化

#### Scenario: 运行时建造

- **WHEN** 表演者确认一个建造物
- **THEN** Viewer 看到该建造物出现在服务器确认的位置，不需要重新加载场景

#### Scenario: 场景关卡

- **WHEN** 会话指定了运行时关卡
- **THEN** Viewer 加载同一关卡并显示静态几何、光照和环境，而不是空地图

#### Scenario: 增量事件缺口

- **WHEN** Viewer 错过事件或请求的序号早于保留窗口
- **THEN** 服务器要求 Viewer 重新获取完整世界快照，客户端恢复后继续接收增量

### Requirement: 查看端只读强制

服务器 SHALL 在应用前拒绝 Viewer 的角色控制、表演修改、物品交互、制作、建造和会话管理请求。只读限制 SHALL NOT 仅依赖客户端隐藏界面或按钮。

#### Scenario: 伪造角色控制

- **WHEN** Viewer 客户端直接发送移动、表演或物品交互请求
- **THEN** 服务器返回 `VIEWER_READ_ONLY`，角色和世界状态不变

#### Scenario: 会话管理

- **WHEN** Viewer 请求踢人、分配角色、结束会话或修改其他参与者角色
- **THEN** 服务器返回 `FORBIDDEN`，会话保持运行

#### Scenario: 查看端控制 API

- **WHEN** Viewer 客户端尝试启动或调用控制 API
- **THEN** 控制 API 在 Game 客户端模式保持禁用，Viewer 只能通过游戏连接接收和查询授权信息

### Requirement: 查看端生命周期

Viewer SHALL 能独立加入、离开和重连，不影响会话生命周期、角色所有权和其他参与者。Viewer 断开后 SHALL 释放查看者槽位，重连后重新获取世界快照。

#### Scenario: 查看者离开

- **WHEN** Viewer 主动离开或断开
- **THEN** 服务器释放其查看者槽位，会话和其他参与者继续运行

#### Scenario: 查看者重连

- **WHEN** Viewer 在会话结束前重新加入
- **THEN** 服务器允许其重新连接并返回最新世界快照，不恢复任何角色所有权

#### Scenario: 多查看者

- **WHEN** 多个 Viewer 同时观看同一会话
- **THEN** 每个 Viewer 独立接收世界表现，任一 Viewer 断开不影响其他 Viewer

### Requirement: 查看端画面与信息叠加

查看端 SHALL 提供可关闭的信息叠加，至少包含会话状态、参与者数量、当前相机模式和跟随目标。关闭叠加后 SHALL 提供不被控件遮挡的干净画面。

#### Scenario: 显示信息叠加

- **WHEN** Viewer 启用信息叠加
- **THEN** 画面显示会话状态、参与者数量、相机模式和跟随目标

#### Scenario: 干净画面

- **WHEN** Viewer 关闭信息叠加
- **THEN** 画面只保留世界渲染，不显示会话控件和调试文本

#### Scenario: 会话结束

- **WHEN** 服务器结束会话
- **THEN** Viewer 显示会话已结束并停止接收世界更新，不崩溃或停留在过期画面
