# Spec Delta

## Purpose

提供基于 Unreal Engine 5.8.4 的 C++ 项目骨架、插件边界、引擎路径绑定以及 Linux 构建、启动、无界面运行和测试工具，使其余能力有稳定可复现的运行基础。

## ADDED Requirements

### Requirement: 项目与引擎版本绑定

系统 SHALL 提供 `engine/EpicodeVideo.uproject`，声明 Unreal Engine 5.8 项目并启用核心、导入、控制 API、制作界面和渲染插件。所有构建与运行脚本 SHALL 默认使用 `/usr/local/unreal` 作为引擎根目录，并允许通过 `UE_ROOT` 环境变量覆盖。

#### Scenario: 使用默认引擎路径构建

- **WHEN** 操作者在仓库根目录执行 `engine/Scripts/Build.sh`
- **THEN** 脚本使用 `/usr/local/unreal` 下的 Linux 构建工具构建 `EpicodeVideoEditor` Development 目标，并在失败时以非零状态退出并输出 UnrealBuildTool 日志位置

#### Scenario: 覆盖引擎路径

- **WHEN** 操作者设置 `UE_ROOT=/opt/unreal` 后执行构建脚本
- **THEN** 脚本使用 `/opt/unreal` 下的引擎工具，且不读取 `/usr/local/unreal`

#### Scenario: 引擎路径无效

- **WHEN** `UE_ROOT` 指向的目录缺少 `Engine/Build/BatchFiles/Linux/Build.sh`
- **THEN** 脚本在开始构建前退出，输出期望路径和当前解析到的引擎根目录，不创建构建产物

### Requirement: 模块与插件边界

系统 SHALL 将功能拆分为运行时核心、资产导入、控制 API、制作界面和视频渲染五个可独立加载的模块。制作界面 SHALL 仅在 Editor 目标中加载，运行时模块 SHALL NOT 依赖 Editor 专属模块。系统 SHALL NOT 修改 `/usr/local/unreal` 下的任何文件。

#### Scenario: 编辑器目标加载全部模块

- **WHEN** 启动 `EpicodeVideoEditor`
- **THEN** 核心、导入、控制 API、制作界面和渲染模块均完成加载，启动日志列出各模块名称与版本

#### Scenario: 无界面目标不加载制作界面

- **WHEN** 以 `-nullrhi -unattended` 启动无界面控制服务
- **THEN** 制作界面模块不被加载，控制 API 与核心模块可用

#### Scenario: 引擎目录保持只读

- **WHEN** 完成构建、启动和测试流程后检查 `/usr/local/unreal`
- **THEN** 引擎安装目录中没有新增、修改或删除的文件

### Requirement: 构建与运行脚本

系统 SHALL 提供非交互式脚本 `Scripts/Build.sh`、`Scripts/Editor.sh`、`Scripts/Headless.sh`、`Scripts/Test.sh` 和 `Scripts/Clean.sh`。脚本 SHALL 支持选择构建配置、传递额外 Unreal 命令行参数，并把日志写入 `Saved/Logs/`。

#### Scenario: 增量构建

- **WHEN** 操作者连续两次执行 `Build.sh`
- **THEN** 第二次构建复用已有中间产物，成功退出，并报告构建目标与耗时

#### Scenario: 启动编辑器

- **WHEN** 操作者执行 `Editor.sh` 并传入项目路径
- **THEN** 脚本启动 `/usr/local/unreal/Engine/Binaries/Linux/UnrealEditor` 并加载 `EpicodeVideo.uproject`

#### Scenario: 无界面启动控制服务

- **WHEN** 操作者执行 `Headless.sh --port 8710`
- **THEN** 引擎以无界面模式启动，控制 API 监听配置端口，且脚本在前台输出就绪标记

#### Scenario: 运行自动化测试

- **WHEN** 操作者执行 `Test.sh`
- **THEN** 脚本运行全部已注册的 Epicode 自动化测试并返回与测试结果一致的状态码

### Requirement: 配置与运行目录

系统 SHALL 从 `engine/Config/DefaultEpicode.ini` 读取控制 API 的 HTTP 端口、WebSocket 端口、监听地址、鉴权令牌引用、导入根目录、场景目录、场景回收目录和渲染输出目录。用户级覆盖 SHALL 通过环境变量或 `Saved/Config/` 下的配置完成。仓库中 SHALL NOT 提交真实鉴权令牌。

#### Scenario: 使用默认配置

- **WHEN** 首次启动且没有用户级覆盖
- **THEN** 控制 API 在 `127.0.0.1:8710` 提供 HTTP、在 `127.0.0.1:8711` 提供 WebSocket，导入资产写入 `Content/Epicode/`，场景写入 `Saved/Epicode/Scenes/`，删除场景移入 `Saved/Epicode/Trash/Scenes/`，渲染产物写入 `Saved/Epicode/Renders/`

#### Scenario: 环境变量覆盖输出目录

- **WHEN** 设置 `EPICODE_OUTPUT_DIR=/data/renders` 后启动
- **THEN** 渲染作业默认写入 `/data/renders`，API 状态响应报告该目录

#### Scenario: 配置中缺少令牌

- **WHEN** 配置启用了鉴权但没有可解析的令牌
- **THEN** 控制 API 拒绝启动并输出说明，而不是以无鉴权模式继续运行

### Requirement: 启动诊断

系统 SHALL 在启动时检查引擎版本、必需插件、GPU/渲染后端和输出目录可写性，并在日志和 `system.health` 响应中报告结果。版本低于 5.8 或缺少必需插件时 SHALL 拒绝启动控制 API。

#### Scenario: 版本不匹配

- **WHEN** 项目被 5.7 引擎加载
- **THEN** 启动失败并输出要求的最低版本与实际版本

#### Scenario: 健康检查通过

- **WHEN** 引擎以 5.8.4 正常启动且输出目录可写
- **THEN** `system.health` 返回 `ok`，并包含引擎版本、项目版本、API 版本和渲染后端名称

#### Scenario: 输出目录不可写

- **WHEN** 配置的渲染输出目录不存在且无法创建
- **THEN** 健康检查返回降级状态并指出不可用的目录，渲染提交被拒绝
