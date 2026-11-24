# Spec Delta

## MODIFIED Requirements

### Requirement: 项目与引擎版本绑定

系统 SHALL 提供 `engine/EpicodeVideo.uproject`，声明 Unreal Engine 5.8 项目并启用核心、导入、控制 API、制作界面、渲染和网络插件。所有构建与运行脚本 SHALL 默认使用 `/usr/local/unreal` 作为引擎根目录，并允许通过 `UE_ROOT` 环境变量覆盖。

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

系统 SHALL 将功能拆分为运行时核心、资产导入、控制 API、制作界面、视频渲染和多人网络六个可独立加载的模块。制作界面 SHALL 仅在 Editor 目标中加载，运行时模块 SHALL NOT 依赖 Editor 专属模块。专用服务器 SHALL NOT 加载制作界面、资产导入和渲染模块。系统 SHALL NOT 修改 `/usr/local/unreal` 下的任何文件。

#### Scenario: 编辑器目标加载全部模块

- **WHEN** 启动 `EpicodeVideoEditor`
- **THEN** 核心、导入、控制 API、制作界面、渲染和网络模块均完成加载，启动日志列出各模块名称与版本

#### Scenario: 无界面目标不加载制作界面

- **WHEN** 以 `-nullrhi -unattended` 启动无界面控制服务
- **THEN** 制作界面模块不被加载，控制 API 与核心模块可用

#### Scenario: 专用服务器模块集合

- **WHEN** 启动专用服务器目标
- **THEN** 核心、网络和控制 API 模块加载，制作界面、资产导入和渲染模块不加载，服务器可以创建和运行会话

#### Scenario: 客户端模块集合

- **WHEN** 启动客户端目标
- **THEN** 核心、网络和现有 `EpicodeVideo` 游戏模块加载，编辑器专用制作界面不加载，客户端可以加入会话并控制已分配角色

#### Scenario: 引擎目录保持只读

- **WHEN** 完成构建、启动和测试流程后检查 `/usr/local/unreal`
- **THEN** 引擎安装目录中没有新增、修改或删除的文件

## ADDED Requirements

### Requirement: 多人网络构建目标

系统 SHALL 在现有编辑器目标之外提供服务器运行模式和可运行客户端目标。引擎发行版支持 Server 目标时 SHALL 构建并使用 `EpicodeVideoServer`；不支持时 SHALL 回退到 `UnrealEditor-Cmd -server`，在构建日志和 `system.health.network` 中标记 `serverMode=editor-fallback`、`serverTargetSupported=false` 和回退原因。客户端 SHALL 优先使用 Game 目标，Game 二进制不可用或启动失败时 SHALL 回退到 `UnrealEditor -game`。编辑器目标 SHALL 继续用于单用户制作和渲染。构建脚本 SHALL 支持分别构建服务器、客户端和编辑器。

#### Scenario: 构建专用服务器

- **WHEN** 操作者在仓库根目录执行服务器构建脚本
- **THEN** 引擎支持 Server 目标时脚本使用 `/usr/local/unreal` 构建 `EpicodeVideoServer` Linux Development 目标；不支持时脚本以退出码 6 写入 `Saved/Logs/build-server-target.log`，标记 `serverMode=editor-fallback` 并输出日志位置

#### Scenario: 构建客户端

- **WHEN** 操作者执行客户端启动脚本并指定 Linux 平台
- **THEN** 脚本优先运行 Game 客户端目标，Game 二进制缺失或退出时回退到 `UnrealEditor -game`，并写入 `clientMode` 和实际二进制

#### Scenario: 编辑器目标保持不变

- **WHEN** 操作者执行现有 `Scripts/Build.sh`
- **THEN** 脚本继续构建 `EpicodeVideoEditor` Development 目标，现有单用户制作流程不受影响

### Requirement: 网络运行脚本

系统 SHALL 提供非交互式服务器和客户端启动脚本，并支持传入监听端口、服务器地址、会话标识、玩家名称和额外 Unreal 命令行参数。脚本 SHALL 把服务器和客户端日志写入 `Saved/Logs/`，并在启动失败时输出可读错误。

#### Scenario: 启动本地服务器

- **WHEN** 操作者执行服务器脚本并指定场景标识和端口
- **THEN** 脚本启动专用服务器，监听指定端口，并在日志中输出会话标识、场景标识和就绪状态

#### Scenario: 启动两个本地客户端

- **WHEN** 操作者在同一台机器上启动两个客户端并连接到本地服务器
- **THEN** 两个客户端使用不同玩家名称加入同一会话，服务器日志记录两个独立玩家标识

#### Scenario: 端口占用

- **WHEN** 服务器监听端口已被占用
- **THEN** 服务器脚本以非零状态退出并输出占用端口和错误原因，不静默改用其他端口

### Requirement: 网络配置与诊断

系统 SHALL 从项目配置读取游戏监听端口、最大玩家数、重连宽限期、会话目录、默认玩家角色和在线子系统选择。默认配置 SHALL 使用 LAN 直连，不要求 Steam 或 EOS；启用 Steam 或 EOS 时 SHALL 报告子系统初始化结果。`system.health` SHALL 报告网络模块状态、监听端口、会话能力和在线子系统状态。

#### Scenario: 默认 LAN 配置

- **WHEN** 首次启动专用服务器且没有用户级网络配置
- **THEN** 服务器使用默认游戏端口和 LAN 直连，不因缺少 Steam 或 EOS 而拒绝启动

#### Scenario: 在线子系统不可用

- **WHEN** 配置启用 Steam 或 EOS 但对应子系统初始化失败
- **THEN** 健康检查返回降级状态并指出失败的子系统，LAN 直连仍可使用

#### Scenario: 会话目录不可写

- **WHEN** 配置的会话目录不存在且无法创建
- **THEN** 服务器拒绝创建会话并返回目录错误，不丢失已有会话摘要
