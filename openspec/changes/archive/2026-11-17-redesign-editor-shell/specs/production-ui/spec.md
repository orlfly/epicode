# Spec Delta

## MODIFIED Requirements

### Requirement: 工作区入口与布局

系统 SHALL 默认启动聚焦编辑器外壳，而不是完整 Unreal Editor 内的九页签制作工作区。聚焦外壳 SHALL 保留 UE 场景渲染视口，并提供 Epicode 主菜单、本地库、四类实体检查器、对话面板和状态栏。旧版九页签制作工作区 SHALL 只在显式 `--legacy` 模式下注册和显示，并且 SHALL NOT 作为默认入口、默认页签或默认菜单项出现。

聚焦模式 SHALL 按 Epicode 能力组织工作区，而不是按 Unreal 的 File/Edit/Window/Tools 通用菜单组织。制作工作区中的环境、渲染队列和 API 状态页签 SHALL NOT 出现在聚焦模式；这些能力只在 `--legacy` 模式下保留。

#### Scenario: 打开工作区

- **WHEN** 操作者在 `--legacy` 模式下从 Epicode 菜单选择“打开制作工作区”
- **THEN** 旧版工作区页签出现并获得焦点，场景、角色、道具、灯光、环境和导入入口以及大纲、检查器、渲染、API 区域均可访问且未崩溃

#### Scenario: 切换专项管理入口

- **WHEN** 操作者在 `--legacy` 模式的旧版工作区点击“灯光”管理入口
- **THEN** 工作区切换到灯光管理界面，当前入口高亮，其他管理入口仍可见

#### Scenario: 重复打开

- **WHEN** 旧版工作区已经打开时操作者在 `--legacy` 模式再次点击入口
- **THEN** 系统聚焦已有页签，不创建第二个实例

#### Scenario: 恢复布局

- **WHEN** 操作者调整聚焦外壳的左侧库、右侧对话和视口大小后关闭并重新打开编辑器
- **THEN** 聚焦外壳恢复上次的布局

#### Scenario: 默认入口

- **WHEN** 操作者启动 `Scripts/Editor.sh`
- **THEN** 系统进入聚焦编辑器外壳，不显示九页签 Studio 作为默认工作区

#### Scenario: 旧版入口

- **WHEN** 操作者启动 `Scripts/Editor.sh --legacy`
- **THEN** 系统保留旧版九页签制作工作区，并允许打开现有 `Epicode Studio` 页签

#### Scenario: 聚焦模式不注册旧页签

- **WHEN** 操作者以默认聚焦模式启动
- **THEN** `Epicode Studio`、`Epicode Multiplayer` 和 `Epicode Director Workspace` 不作为默认页签注册到可见窗口

#### Scenario: 环境/渲染/API 入口

- **WHEN** 操作者浏览聚焦模式主菜单
- **THEN** 找不到环境管理、渲染队列或 API 状态入口；这些入口只在 `--legacy` 模式下可见
