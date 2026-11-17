# local-library Specification

## Purpose
定义受控的本地场景、角色和物品库：维护可重建索引，支持搜索、标签过滤、场景/资产导入、拖放放置、缺失资产标记和引用安全检查的删除。本地库只读取配置根目录，不上传资产、不自动下载资产。

## Requirements

### Requirement: 本地库索引

系统 SHALL 维护本地库索引，覆盖场景、角色和物品三类条目。索引 SHALL 写入 `Saved/Epicode/Library/index.json`，并包含 `schemaVersion`、`updatedAt` 和条目列表；每个条目 SHALL 包含稳定 `id`、`kind`、`name`、资产路径或场景路径、源路径、源哈希、标签、缩略图路径（可选）、创建时间和更新时间。索引是缓存，系统 SHALL 能从场景库、资产注册表和配置根重建索引。

#### Scenario: 首次扫描

- **WHEN** 本地库首次启动或索引不存在
- **THEN** 系统扫描场景库、角色库和物品库，生成索引并显示三类条目

#### Scenario: 索引损坏

- **WHEN** `index.json` 无法解析或 schema 版本不支持
- **THEN** 系统备份损坏文件，重建索引，并在库面板显示一次可关闭的警告

#### Scenario: 条目缺失资产

- **WHEN** 索引中的角色或物品资产已不存在
- **THEN** 条目保留为不可用状态并显示原因，放置按钮禁用，不静默删除条目

### Requirement: 本地库浏览和搜索

本地库面板 SHALL 按场景、角色、物品三个分类显示条目，支持名称搜索、标签过滤、刷新和定位。列表 SHALL 显示名称、类型、路径、修改时间和可用状态。搜索 SHALL 不区分大小写，并 SHALL 在 100 毫秒内返回控制权。

#### Scenario: 搜索角色

- **WHEN** 操作者在角色库输入名称片段
- **THEN** 列表只显示名称或标签匹配的角色，清空搜索后恢复全部条目

#### Scenario: 刷新库

- **WHEN** 操作者点击刷新
- **THEN** 系统重新扫描配置根和场景库，更新新增、删除和不可用状态，不阻塞界面

#### Scenario: 空库

- **WHEN** 某个分类没有任何条目
- **THEN** 面板显示空状态和“导入”入口，而不是空白区域

### Requirement: 从本地库导入场景

系统 SHALL 支持从本地库导入 Epicode 场景 JSON。导入 SHALL 校验 `schemaVersion`、`sceneId`、`levelPath` 和实体结构，拒绝未来版本、损坏文件和重复 `sceneId` 冲突。导入成功后场景 SHALL 出现在场景库并可直接打开；导入失败 SHALL 保留原文件并显示错误原因。

#### Scenario: 导入场景

- **WHEN** 操作者选择一个有效的 Epicode `scene.json` 并确认导入
- **THEN** 系统校验并写入 `Saved/Epicode/Scenes/<sceneId>/scene.json`，场景库刷新并选中新场景

#### Scenario: 重复场景

- **WHEN** 导入的 `sceneId` 已存在
- **THEN** 系统要求选择覆盖、另存为新场景或取消；默认不覆盖

#### Scenario: 损坏场景

- **WHEN** 导入文件不是有效 JSON 或 schema 版本过高
- **THEN** 系统拒绝导入，显示具体字段和原因，原文件不变

### Requirement: 从本地库导入角色和物品

系统 SHALL 支持从本地文件导入角色和物品到本地库。角色导入 SHALL 要求源文件包含骨骼网格，目标路径默认为 `/Game/Epicode/Imported/Characters/<name>`；物品导入 SHALL 要求静态网格，目标路径默认为 `/Game/Epicode/Imported/Props/<name>`。导入 SHALL 使用现有 Interchange/OBJ 流程，显示任务进度，支持取消，并在失败时清理未完成资产和索引条目。

#### Scenario: 导入角色

- **WHEN** 操作者选择一个包含骨骼网格的 FBX/glTF 并导入为角色
- **THEN** 系统导入资产、生成骨骼网格条目、刷新角色库并选中新条目

#### Scenario: 缺少骨骼

- **WHEN** 操作者把只有静态网格的文件导入为角色
- **THEN** 系统拒绝并提示改用物品导入，不创建角色条目

#### Scenario: 导入取消

- **WHEN** 操作者在导入任务运行中取消
- **THEN** 当前文件完成或失败后停止后续文件，清理本任务创建但未完成的资产和条目

### Requirement: 从本地库删除

系统 SHALL 支持从本地库删除场景、角色和物品条目。删除场景 SHALL 调用现有 `scene.delete` 移入 `Saved/Epicode/Trash/Scenes/<timestamp>/`，SHALL NOT 永久删除。删除角色或物品 SHALL 默认只移除索引条目，SHALL NOT 删除 `.uasset` 文件；只有操作者显式勾选“同时删除资产文件”并二次确认后，系统才检查引用并删除资产。删除当前打开的场景或仍被场景引用的资产 SHALL 被拒绝并显示引用原因。

#### Scenario: 删除场景

- **WHEN** 操作者删除一个未打开的场景并确认
- **THEN** 场景移入回收目录，从场景库移除，当前场景不变

#### Scenario: 删除角色条目

- **WHEN** 操作者删除一个角色库条目且未勾选删除资产
- **THEN** 条目从索引移除，`.uasset` 保留，现有场景中的角色实体不受影响

#### Scenario: 删除被引用资产

- **WHEN** 操作者勾选同时删除资产文件，但资产仍被当前场景引用
- **THEN** 系统拒绝删除，显示引用实体和场景，不修改索引或资产

### Requirement: 从本地库放置

系统 SHALL 支持从本地库把角色和物品放置到当前场景。放置 SHALL 通过现有 `character.spawn` 或 `prop.spawn` 命令创建实体，使用视口中心或鼠标位置作为初始位置，放置后自动选中并进入移动模式。没有打开场景、条目不可用或资产类型不匹配时 SHALL 拒绝放置并显示原因。

#### Scenario: 放置角色

- **WHEN** 操作者从角色库双击一个可用角色
- **THEN** 当前场景创建角色实体，视口显示该角色，对象列表和检查器选中它

#### Scenario: 放置物品

- **WHEN** 操作者把物品条目拖到视口
- **THEN** 物品在放置点创建，位置和视口光标一致，操作可撤销

#### Scenario: 没有场景

- **WHEN** 操作者在没有打开场景时尝试放置
- **THEN** 系统提示先新建或打开场景，不创建实体

### Requirement: 本地库路径安全

本地库 SHALL 只读取 `DefaultEpicode.ini` 配置的库根、`Saved/Epicode/Scenes` 和 `/Game/Epicode/Imported` 下的资产。导入源路径 SHALL 位于 `ImportRoots` 允许根内，目标路径 SHALL 以 `/Game/` 开头。系统 SHALL 拒绝目录穿越、符号链接逃逸、非允许扩展名和超过大小上限的文件。

#### Scenario: 路径越界

- **WHEN** 导入源路径位于允许根之外
- **THEN** 系统拒绝导入并显示允许根，不读取文件内容

#### Scenario: 非法目标路径

- **WHEN** 目标路径不是 `/Game/` 开头或包含 `..`
- **THEN** 系统拒绝导入，不创建资产或索引条目

#### Scenario: 符号链接逃逸

- **WHEN** 允许根内的符号链接指向允许根之外
- **THEN** 系统拒绝该路径，并在日志中记录拒绝原因但不记录完整敏感路径

### Requirement: 本地库冲突和重复

系统 SHALL 使用源文件 SHA-1 和资产路径检测重复导入。相同源内容且导入选项一致时 SHALL 复用已有资产和条目；相同源内容但选项不同 SHALL 要求覆盖、创建副本或取消；不同源内容但目标名称冲突 SHALL 要求重命名或取消。

#### Scenario: 重复导入相同文件

- **WHEN** 操作者再次导入同一文件且选项一致
- **THEN** 系统复用已有资产和条目，不创建重复资产

#### Scenario: 名称冲突

- **WHEN** 导入的角色名称与已有条目冲突但源内容不同
- **THEN** 系统要求输入新名称或取消，不自动覆盖

#### Scenario: 选项冲突

- **WHEN** 同一源文件以不同缩放或材质选项再次导入
- **THEN** 系统要求覆盖或创建副本，不静默复用旧资产

### Requirement: 本地库与场景解耦

本地库删除条目 SHALL NOT 修改当前场景文档。场景中已放置的实体 SHALL 保留其资产引用；只有放置新实体时才会检查库条目可用性。删除库资产时 SHALL 检查当前场景和已保存场景的引用，避免产生断链。

#### Scenario: 删除库条目后场景仍可用

- **WHEN** 操作者删除一个角色库条目但当前场景已放置该角色
- **THEN** 场景中的角色继续显示和编辑，库面板不再列出该条目

#### Scenario: 删除资产前检查引用

- **WHEN** 操作者尝试删除仍被任何已保存场景引用的资产文件
- **THEN** 系统列出引用场景并拒绝删除，除非先移除引用
