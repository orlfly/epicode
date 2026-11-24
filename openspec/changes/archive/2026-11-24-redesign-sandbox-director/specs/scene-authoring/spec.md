# Spec Delta

## ADDED Requirements

### Requirement: 制作包生命周期

系统 SHALL 支持创建、打开、保存、另存为、列出和删除制作包。制作包 SHALL 位于 `Saved/Epicode/Productions/<productionId>/production.json`，包含场景引用、演员阵容、世界规则、叙事节拍图、镜头预设和版本；删除 SHALL 默认移入回收目录。

#### Scenario: 创建制作包

- **WHEN** 操作者以现有场景创建制作包
- **THEN** 系统写入默认世界规则和空节拍图，返回制作包标识并标记为未修改

#### Scenario: 保存并重新打开

- **WHEN** 操作者保存制作包后关闭并重新打开
- **THEN** 场景引用、演员、规则、节拍和镜头预设与保存前一致

#### Scenario: 删除当前制作包

- **WHEN** 操作者尝试删除正在打开的制作包
- **THEN** 命令失败并返回 `PRODUCTION_IN_USE`，文件和当前状态不变

### Requirement: 制作包与场景绑定校验

制作包 SHALL 引用一个已保存场景，并可覆盖场景中的演员阵容、初始状态和运行时规则。装载或保存前 SHALL 校验场景存在、schema 受支持、引用实体类型正确；校验失败 SHALL 原子拒绝并保留原文件。

#### Scenario: 有效场景引用

- **WHEN** 制作包引用一个受支持的已保存场景
- **THEN** 校验通过并返回场景标识、版本和实体摘要

#### Scenario: 场景版本过高

- **WHEN** 制作包引用高于构建支持版本的场景
- **THEN** 校验失败并返回 `SCENE_VERSION_UNSUPPORTED`，不修改制作包

#### Scenario: 引用类型错误

- **WHEN** 演员阵容引用的实体不是角色
- **THEN** 校验失败并指出字段路径和实体标识

### Requirement: 沙盒规则作者化

系统 SHALL 支持作者化交互点、可操作动词、区域、生成器、生成表、资源节点、NPC 行为档案、重生规则和运行预算。规则 SHALL 使用稳定标识和显式引用；保存前 SHALL 校验范围、重复标识、缺失引用和预算上限。

#### Scenario: 添加交互点

- **WHEN** 操作者为场景实体添加带动词和前置条件的交互点
- **THEN** 制作包保存该规则并在校验结果中返回可交互对象摘要

#### Scenario: 重复规则标识

- **WHEN** 两个生成器使用相同标识
- **THEN** 保存失败并返回 `DUPLICATE_RULE_ID`，原制作包不变

#### Scenario: 超出运行预算

- **WHEN** 规则声明的实体预算超过配置上限
- **THEN** 保存失败并指出预算字段和允许范围

### Requirement: 叙事图作者化

系统 SHALL 支持作者化节拍节点、进入/退出条件、目标、导演 cue、镜头轨道和分支边。条件 SHALL 使用受限表达式和已声明变量；节拍图 SHALL 支持循环但入口必须可达，所有引用 SHALL 在保存前校验。

#### Scenario: 保存节拍图

- **WHEN** 操作者保存包含入口、目标和后继边的节拍图
- **THEN** 制作包保存图结构并在校验结果中返回节拍数量和入口标识

#### Scenario: 不可达节拍

- **WHEN** 节拍图存在从入口不可达的节拍
- **THEN** 保存失败并返回 `NARRATIVE_UNREACHABLE_BEAT`，指出节拍标识

#### Scenario: 条件引用未声明变量

- **WHEN** 条件表达式使用未声明变量
- **THEN** 保存失败并返回 `NARRATIVE_UNKNOWN_VARIABLE`，原图不变

### Requirement: 制作包版本与兼容迁移

制作包 SHALL 带 `schemaVersion`。系统 SHALL 能加载当前版本和受支持的旧版本，并在保存时写入当前版本；未知顶层字段 SHALL 保留，高于支持版本的制作包 SHALL 被拒绝。仅场景的旧工作流 SHALL 继续可用。

#### Scenario: 加载旧版本

- **WHEN** 打开受支持的旧版制作包
- **THEN** 系统迁移缺失字段并在内存中标记升级，保存后写入当前版本

#### Scenario: 保留未知字段

- **WHEN** 打开包含未知扩展字段的当前版本制作包并保存
- **THEN** 未知字段保持原值，不被静默删除

#### Scenario: 版本过高

- **WHEN** 制作包版本高于构建支持版本
- **THEN** 打开失败并返回 `PRODUCTION_VERSION_UNSUPPORTED`，不执行部分迁移

### Requirement: 制作包原子验证与引用完整性

制作包的创建、修改和保存 SHALL 在单一事务内验证并写入；任一引用、规则或叙事图失败 SHALL 回滚全部修改。系统 SHALL 拒绝指向允许根目录之外的资产路径、循环场景引用和重复的演员槽位。

#### Scenario: 批量修改原子回滚

- **WHEN** 一次制作包更新同时修改规则和节拍图，其中节拍引用不存在
- **THEN** 整个更新失败，内存和磁盘制作包均保持修改前状态

#### Scenario: 资产路径逃逸

- **WHEN** 规则引用允许根目录之外的资产路径
- **THEN** 保存失败并返回 `PATH_NOT_ALLOWED`，不写入路径

#### Scenario: 演员槽位重复

- **WHEN** 制作包把两个演员分配到同一槽位
- **THEN** 校验失败并指出重复槽位，不启动运行时会话
