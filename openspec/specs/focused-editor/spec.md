# focused-editor Specification

## Purpose
定义 `Scripts/Editor.sh` 默认启动的 Epicode 聚焦编辑器外壳：保留 Unreal 场景视口和 gizmo，提供按 Epicode 能力组织的主菜单、四类实体编辑、本地库、对话和状态栏，同时通过显式 `--legacy` 保留旧版完整 Editor 和九页签 Studio。

## Requirements

### Requirement: 聚焦编辑器启动模式

系统 SHALL 提供聚焦编辑器模式，并作为 `Scripts/Editor.sh` 的默认启动模式。聚焦模式 SHALL 创建只包含 Epicode 工作台的顶层窗口，而不是把完整 Unreal Editor 主界面作为默认产品界面。系统 SHALL 保留显式的 `--legacy` 启动参数，用于进入旧版完整 Editor。

#### Scenario: 默认启动聚焦模式

- **WHEN** 操作者执行 `./Scripts/Editor.sh`
- **THEN** 启动后只显示 Epicode 聚焦工作台、场景视口、主菜单、库/检查器、对话和状态栏，不显示 Unreal 默认主标签页

#### Scenario: 启动旧版编辑器

- **WHEN** 操作者执行 `./Scripts/Editor.sh --legacy`
- **THEN** 系统启动旧版完整 Unreal Editor，并保留现有 Epicode Studio 页签

#### Scenario: 聚焦模式启动失败

- **WHEN** 聚焦窗口或视口初始化失败
- **THEN** 系统显示可复制的诊断信息并退出非零状态，不静默回退到完整 Editor

### Requirement: 保留场景渲染视口

聚焦编辑器 SHALL 保留 Unreal Engine 的场景渲染区，使用编辑器世界和实时视口显示当前场景。视口 SHALL 支持选择、移动、旋转、缩放 gizmo、网格、Lit/Unlit/Wireframe 视图模式和聚焦选中/框选全部。视口 SHALL NOT 以静态图片、离屏截图或游戏模式替代编辑器视口。

#### Scenario: 显示当前场景

- **WHEN** 聚焦编辑器打开一个 Epicode 场景
- **THEN** 视口显示该场景的 UE 世界、角色、物品、灯光和相机，镜头位置与保存的场景相机一致

#### Scenario: 选择并移动实体

- **WHEN** 操作者在视口中点击一个角色并拖动移动 gizmo
- **THEN** 实体位置更新，场景文档和 Actor 投影一致，操作可撤销

#### Scenario: 切换视图模式

- **WHEN** 操作者从“视图”菜单选择 Unlit 或 Wireframe
- **THEN** 视口立即切换显示模式，不改变场景数据

### Requirement: 隐藏通用 Unreal Editor 功能

聚焦模式 SHALL 隐藏或禁用 Unreal Editor 的通用界面入口，包括默认菜单栏、主工具栏、Content Browser、Details、World Settings、Blueprint Editor、Material Editor、Sequencer、Output Log、插件浏览器、项目设置、PIE/Simulate/Build 和 Source Control 的可见入口。系统 SHALL 拦截这些功能的默认快捷键，使操作者不能从聚焦界面进入通用编辑器工作流。底层引擎功能可以保留，但不得作为聚焦模式的可见产品功能。

#### Scenario: 默认页签不可见

- **WHEN** 聚焦模式启动完成
- **THEN** 默认的主页签、Content Browser、Details、World Settings、Blueprint、Sequencer 和 Output Log 不存在于可见窗口

#### Scenario: 通用快捷键被拦截

- **WHEN** 操作者按下打开 Content Browser、Blueprint、Sequencer、PIE 或 Build 的默认快捷键
- **THEN** 聚焦模式不打开对应功能，并在状态栏提示该功能只在 `--legacy` 模式可用

#### Scenario: 维护入口

- **WHEN** 操作者需要排障或使用旧功能
- **THEN** 可以通过 `./Scripts/Editor.sh --legacy` 启动完整 Editor，聚焦模式本身不提供隐藏入口

### Requirement: Epicode 主菜单

聚焦编辑器 SHALL 用 Epicode 能力组织主菜单，至少包含：场景、角色、物品、灯光、摄像、本地库、编辑、视图、对话、帮助。主菜单 SHALL NOT 以 Unreal 的 File/Edit/Window/Tools 通用菜单作为主结构。环境、渲染队列、API 状态、Content Browser、蓝图、材质、Sequencer、地形、植被、PIE、插件和项目设置 SHALL NOT 出现在主菜单。

#### Scenario: 菜单结构

- **WHEN** 操作者打开主菜单
- **THEN** 只看到场景、角色、物品、灯光、摄像、本地库、编辑、视图、对话和帮助入口

#### Scenario: 场景菜单

- **WHEN** 操作者展开“场景”菜单
- **THEN** 可以看到新建、打开、保存、另存为、关闭、最近场景、从本地库导入场景和从本地库删除场景

#### Scenario: 移除功能不可见

- **WHEN** 操作者浏览全部主菜单
- **THEN** 找不到环境编辑、渲染队列、API 状态、蓝图、材质、Sequencer、地形、植被、PIE 或项目设置入口

### Requirement: 四类实体编辑范围

聚焦编辑器 SHALL 只允许角色、物品、灯光、摄像四类实体的放置、选择、位置/旋转/缩放调整、属性调整和从当前场景删除。系统 SHALL NOT 提供层级重挂接、分组、环境编辑、材质图、蓝图、动画蓝图、Sequencer、地形、植被、物理约束、导航、PIE、渲染队列或通用资产编辑。

#### Scenario: 放置四类实体

- **WHEN** 操作者从本地库或菜单放置角色、物品、灯光或相机
- **THEN** 当前场景新增对应类型实体，实体自动选中并进入移动模式

#### Scenario: 调整位置和属性

- **WHEN** 操作者选中一个实体并修改变换或类型属性
- **THEN** 视口、对象列表和检查器同步更新，修改通过场景命令提交并可撤销

#### Scenario: 从场景删除

- **WHEN** 操作者选中一个实体并执行删除
- **THEN** 实体从当前场景和视口移除，源资产仍保留，操作可通过一次撤销恢复

#### Scenario: 不允许的编辑

- **WHEN** 操作者尝试重挂接层级、编辑环境、打开蓝图或进入 PIE
- **THEN** 聚焦模式不提供该入口或拒绝该操作，并提示使用 `--legacy` 维护模式

### Requirement: 聚焦工作台布局

聚焦工作台 SHALL 包含：顶部主菜单、中央场景视口、可折叠左侧本地库/最小对象列表、可折叠右侧对话面板、底部状态栏。左侧和右侧面板 SHALL 可调整宽度或折叠，布局 SHALL 在同一台机器上跨编辑器会话保存。中央视口 SHALL 始终可用，不因侧栏折叠而消失。

#### Scenario: 调整布局

- **WHEN** 操作者拖动左侧或右侧面板边缘
- **THEN** 面板宽度变化，视口按剩余空间重排，不遮挡菜单和状态栏

#### Scenario: 折叠对话

- **WHEN** 操作者点击对话面板的折叠按钮
- **THEN** 对话面板收起，视口获得更多空间，菜单中的“对话”入口仍可重新打开

#### Scenario: 恢复布局

- **WHEN** 操作者关闭并重新打开聚焦编辑器
- **THEN** 左侧/右侧面板宽度、折叠状态和视口布局恢复上次状态

### Requirement: 最小对象列表

聚焦编辑器 SHALL 提供一个最小对象列表，用于选择当前场景中的角色、物品、灯光和摄像。列表 SHALL 支持按名称搜索和按类型过滤，SHALL 与视口选择双向同步。列表 SHALL NOT 提供层级拖放重挂接、分组、任意 Actor 编辑或资产编辑。

#### Scenario: 从列表选择

- **WHEN** 操作者在对象列表点击一个角色
- **THEN** 视口选中该角色，检查器显示其属性

#### Scenario: 从视口选择

- **WHEN** 操作者在视口点击一个灯光
- **THEN** 对象列表高亮对应行，检查器显示灯光属性

#### Scenario: 过滤列表

- **WHEN** 操作者在对象列表输入名称片段或选择“摄像”过滤
- **THEN** 列表只显示匹配实体，当前选中项保持可见

### Requirement: 四类实体属性检查器

聚焦编辑器 SHALL 提供按实体类型区分的属性检查器，至少覆盖：角色的骨骼网格、动画、播放/暂停、动画速度、可见性；物品的静态网格、材质槽覆盖、物理开关、可见性；灯光的类型、强度、颜色、衰减、阴影开关；摄像的视场角、焦距、光圈、对焦距离和活动相机。非法输入 SHALL 在控件旁显示错误并且不修改场景。

#### Scenario: 编辑角色动画

- **WHEN** 操作者选中角色并切换动画或调整动画速度
- **THEN** 视口更新角色动画，检查器显示新值，修改可撤销

#### Scenario: 编辑灯光强度

- **WHEN** 操作者选中灯光并修改强度
- **THEN** 视口照明立即更新，场景文档和检查器一致

#### Scenario: 非法输入

- **WHEN** 操作者在相机视场角输入超出范围的值
- **THEN** 控件显示范围错误，场景保持上一个有效值，不提交命令

### Requirement: 视图和变换快捷键

聚焦编辑器 SHALL 提供以下快捷键：`Q` 选择、`W` 移动、`E` 旋转、`R` 缩放、`F` 聚焦选中、`Delete` 删除选中、`Ctrl+Z` 撤销、`Ctrl+Shift+Z` 或 `Ctrl+Y` 重做、`Ctrl+S` 保存。快捷键 SHALL 只在聚焦视口或 Epicode 控件获得焦点时生效，不得触发 Unreal 通用编辑器功能。

#### Scenario: 切换变换模式

- **WHEN** 操作者按下 W
- **THEN** 视口进入移动模式，gizmo 显示移动轴，状态栏显示“移动”

#### Scenario: 删除后撤销

- **WHEN** 操作者按 Delete 删除选中实体，然后按 Ctrl+Z
- **THEN** 实体恢复，选择状态和变换恢复

#### Scenario: 快捷键不触发通用功能

- **WHEN** 操作者按下 Unreal 默认的 Content Browser 或 PIE 快捷键
- **THEN** 聚焦模式不打开对应功能

### Requirement: 旧版兼容

系统 SHALL 保留 `--legacy` 启动模式，旧版完整 Editor 和现有 Epicode Studio 九页签 SHALL 在该模式下继续工作。聚焦模式 SHALL NOT 因旧版功能存在而暴露旧页签；旧版功能 SHALL 只通过显式 `--legacy` 进入。

#### Scenario: 旧版页签

- **WHEN** 操作者以 `--legacy` 启动
- **THEN** 现有 `Epicode Studio`、`Epicode Multiplayer` 和 `Epicode Director Workspace` 页签仍可打开

#### Scenario: 聚焦模式不显示旧页签

- **WHEN** 操作者以默认聚焦模式启动
- **THEN** 上述旧页签不出现在主窗口和主菜单中

### Requirement: 界面响应性

聚焦编辑器的菜单展开、对象列表刷新、属性提交、库刷新和对话流式更新 SHALL 在 100 毫秒内返回控制权，不得阻塞界面线程。长任务 SHALL 在后台执行，并提供进度、取消或忙碌状态。

#### Scenario: 大场景列表刷新

- **WHEN** 当前场景包含数千个实体并刷新对象列表
- **THEN** 界面保持可交互，列表分批更新，不出现持续卡顿

#### Scenario: 对话流式回复

- **WHEN** 对话后端持续推送文本
- **THEN** 对话面板逐段显示回复，视口和菜单仍可操作，取消按钮可用

### Requirement: 空状态和错误恢复

聚焦编辑器 SHALL 在没有打开场景、场景为空、没有选中实体、库为空、对话后端不可用和视口初始化失败时显示明确空状态和下一步操作。不可恢复错误 SHALL 提供复制诊断信息、重试或进入 `--legacy` 的入口，不得只显示空白区域。

#### Scenario: 没有打开场景

- **WHEN** 聚焦编辑器启动后没有当前场景
- **THEN** 视口显示空场景提示，并提供“新建场景”“从本地库打开”入口

#### Scenario: 对话后端不可用

- **WHEN** DeepSeek 后端连接失败
- **THEN** 对话面板显示错误和重试入口，手工编辑、视口和本地库继续可用

#### Scenario: 场景为空

- **WHEN** 当前场景没有任何四类实体
- **THEN** 对象列表显示空状态，并提供从本地库放置角色/物品或创建灯光/相机的入口
