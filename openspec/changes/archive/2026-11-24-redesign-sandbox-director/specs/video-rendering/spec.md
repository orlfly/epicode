# Spec Delta

## ADDED Requirements

### Requirement: Take 渲染提交与校验

系统 SHALL 接受以 Take 标识和版本提交的渲染作业，读取事件区间、镜头轨道、分辨率预设和输出目录。提交前 SHALL 校验 Take 版本、依赖快照、相机和输出目录；缺失或冲突 SHALL 返回稳定错误且不创建作业。

#### Scenario: 提交 Take 渲染

- **WHEN** 客户端以有效 Take 版本提交渲染
- **THEN** 系统创建引用该 Take 的作业，返回作业标识和预期帧范围

#### Scenario: Take 版本不存在

- **WHEN** 请求的 Take 标识或版本不存在
- **THEN** 返回 `TAKE_NOT_FOUND`，不创建作业

#### Scenario: 输出目录不可写

- **WHEN** Take 渲染的输出目录无法创建或写入
- **THEN** 提交失败并返回 `OUTPUT_NOT_WRITABLE`，队列不变

### Requirement: Take 确定性快照

Take 渲染 SHALL 使用 Take 固化的事件区间、初始快照和内容摘要，而不是提交时的 live 世界。作业元数据 SHALL 记录 Take 标识、版本、内容摘要、事件范围、相机轨道和确定性标记。

#### Scenario: 渲染与 live 隔离

- **WHEN** Take 提交后 live 会话继续变化
- **THEN** 渲染输出仍来自 Take 快照，不受后续 live 事件影响

#### Scenario: 内容摘要不一致

- **WHEN** 渲染环境的引擎或内容摘要与 Take 不一致
- **THEN** 作业默认拒绝并返回 `RENDER_CONTENT_MISMATCH`，除非显式请求非精确渲染

#### Scenario: 作业元数据完整

- **WHEN** Take 渲染完成
- **THEN** `job.json` 包含 Take 版本、事件范围、相机轨道和内容摘要

### Requirement: 多镜头轨道与输出命名

Take SHALL 支持一个或多个镜头轨道，渲染 SHALL 按轨道拆分输出或生成带镜头切换的序列。文件名 SHALL 包含 Take、版本、轨道和帧号；轨道重叠或相机缺失 SHALL 在提交时拒绝。

#### Scenario: 多轨道输出

- **WHEN** Take 包含两个独立镜头轨道
- **THEN** 渲染输出按轨道生成独立文件，文件名可区分轨道和 Take 版本

#### Scenario: 轨道重叠

- **WHEN** 两个镜头轨道在相同帧范围声明不同相机
- **THEN** 提交失败并返回 `TAKE_TRACK_OVERLAP`，不开始渲染

#### Scenario: 镜头切换序列

- **WHEN** Take 声明按事件切换相机
- **THEN** 输出片段在切换点使用对应相机，元数据记录切换事件序号

### Requirement: Take 渲染进度、取消与失败

Take 渲染 SHALL 提供排队、运行、取消中、成功、失败和已取消状态，并支持取消、重试和打开产物。失败 SHALL 保留诊断和已生成产物信息，重试 SHALL 创建新作业而不是覆盖旧作业。

#### Scenario: 取消 Take 渲染

- **WHEN** 操作者取消运行中的 Take 渲染
- **THEN** 作业停止并进入已取消，部分输出按规则清理

#### Scenario: 失败重试

- **WHEN** Take 渲染因临时 GPU 错误失败
- **THEN** 重试创建新作业并复用同一 Take 版本，旧作业保留失败诊断

#### Scenario: 进度可查询

- **WHEN** Take 渲染运行中
- **THEN** 查询返回当前帧、总帧数、当前轨道和预计剩余时间

### Requirement: Take 产物验证

Take 渲染完成 SHALL 校验帧数、分辨率、文件名规则、时长和校验和，并登记到 Take 产物清单。验证失败 SHALL 把作业标记为失败，不得报告成功。

#### Scenario: 产物完整

- **WHEN** Take 渲染完成且帧数、尺寸和校验和均匹配
- **THEN** 作业标记为成功，产物登记到 Take 清单

#### Scenario: 帧数不足

- **WHEN** 输出帧数少于 Take 预期帧数
- **THEN** 作业标记失败并返回 `OUTPUT_INCOMPLETE`，保留诊断

#### Scenario: 重复渲染同一 Take

- **WHEN** 使用相同 Take 版本和预设重复渲染
- **THEN** 两次作业的文件名规则、帧数和分辨率一致，元数据分别记录作业标识
