# engine-foundation Specification

## Purpose
提供基于 Unreal Engine 5.8.4 的 C++ 项目骨架、插件边界、引擎路径绑定以及 Linux 构建、启动、无界面运行和测试工具，使其余能力有稳定可复现的运行基础。

## Requirements

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

系统 SHALL 将功能拆分为运行时核心、资产导入、控制 API、制作界面、视频渲染、多人网络、沙盒运行时和导演重演八个可独立加载的模块。制作界面 SHALL 仅在 Editor 目标中加载，运行时模块 SHALL NOT 依赖 Editor 专属模块。专用服务器 SHALL NOT 加载制作界面、资产导入和渲染模块。沙盒与导演模块 SHALL 可在无界面服务器加载。系统 SHALL NOT 修改 `/usr/local/unreal` 下的任何文件。

#### Scenario: 编辑器目标加载全部模块

- **WHEN** 启动 `EpicodeVideoEditor`
- **THEN** 核心、导入、控制 API、制作界面、渲染、网络、沙盒和导演模块均完成加载，启动日志列出各模块名称与版本

#### Scenario: 无界面目标不加载制作界面

- **WHEN** 以 `-nullrhi -unattended` 启动无界面控制服务
- **THEN** 制作界面模块不被加载，控制 API、核心、沙盒和导演运行时模块可用

#### Scenario: 专用服务器模块集合

- **WHEN** 启动专用服务器目标
- **THEN** 核心、网络、控制 API、沙盒和导演模块加载，制作界面、资产导入和渲染模块不加载，服务器可以创建和运行会话

#### Scenario: 客户端模块集合

- **WHEN** 启动客户端目标
- **THEN** 核心、网络、沙盒和现有 `EpicodeVideo` 游戏模块加载，编辑器专用制作界面不加载，客户端可以加入会话并控制已分配角色

#### Scenario: 引擎目录保持只读

- **WHEN** 完成构建、启动和测试流程后检查 `/usr/local/unreal`
- **THEN** 引擎安装目录中没有新增、修改或删除的文件

#### Scenario: 沙盒模块独立加载

- **WHEN** 以只读 Viewer 或纯 Agent 模式启动
- **THEN** 沙盒模块完成世界规则与权限加载，导演模块可被禁用而不影响 Viewer 和 Agent

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

### Requirement: 构建目标降级与诊断

系统 SHALL 在构建或启动服务器和客户端时报告请求目标、实际二进制、运行模式和回退原因。独立 Server 目标不可用时，脚本 SHALL 使用 `UnrealEditor-Cmd -server` 并把 `serverMode` 标记为 `editor-fallback`；Game 客户端启动失败时 SHALL 回退到 `UnrealEditor -game` 或返回可诊断的非零退出码。系统 SHALL NOT 修改 `/usr/local/unreal`。

#### Scenario: 引擎不支持 Server 目标

- **WHEN** 操作者构建 `EpicodeVideoServer` 而当前引擎发行版返回不支持 Server 目标
- **THEN** 构建脚本以非零状态退出，日志包含引擎原始错误、请求目标和 `serverMode=editor-fallback`，不把失败报告为成功

#### Scenario: Game 客户端启动失败

- **WHEN** 打包的 `EpicodeVideo` Game 二进制在启动阶段崩溃
- **THEN** 客户端脚本记录崩溃日志，回退到 `UnrealEditor -game` 或返回非零退出码并输出回退命令，不静默挂起

#### Scenario: 编辑器构建保持不变

- **WHEN** 操作者执行现有 `Scripts/Build.sh`
- **THEN** 脚本继续构建 `EpicodeVideoEditor` Development 目标，退出码和日志路径与当前行为一致

### Requirement: 构建回退可复验

系统 SHALL 为服务器和客户端回退路径提供非交互式验证命令，并在 `Saved/Logs/` 写入包含请求目标、实际二进制、回退原因和健康检查结果的日志。验证命令 SHALL 在回退不可用时返回非零退出码。

#### Scenario: 服务器回退验证

- **WHEN** 操作者运行服务器回退验证
- **THEN** 日志包含 `serverMode=editor-fallback`、实际二进制路径和 `system.health.network` 的服务器模式字段

#### Scenario: 客户端回退验证

- **WHEN** 操作者运行客户端回退验证
- **THEN** 日志包含 `clientMode=editor-game` 或 `clientMode=game-binary`、实际二进制路径和连接结果

#### Scenario: 回退不可用

- **WHEN** 服务器和编辑器命令行都不可执行
- **THEN** 验证命令返回非零退出码，输出缺失路径和修复建议，不生成通过摘要

### Requirement: 确定性运行时配置

系统 SHALL 提供固定模拟 tick 率、随机种子策略、事件日志目录、检查点间隔、重演保留窗口、实体预算和事件预算配置。配置 SHALL 在启动时校验，非法值 SHALL 阻止沙盒会话启动并报告字段与允许范围。

#### Scenario: 默认确定性配置

- **WHEN** 引擎以默认配置启动
- **THEN** `system.health` 报告固定 tick 率、种子策略、日志目录和重演保留窗口

#### Scenario: 非法 tick 率

- **WHEN** 配置的模拟 tick 率为零或负值
- **THEN** 启动诊断失败并指出配置字段，沙盒会话不可创建

#### Scenario: 重演保留窗口过小

- **WHEN** 检查点间隔大于保留窗口
- **THEN** 启动配置被拒绝，避免产生无法重演的日志

### Requirement: 导演与重演运行脚本

系统 SHALL 提供非交互式 `Scripts/Director.sh`、`Scripts/Replay.sh` 和相应冒烟脚本。脚本 SHALL 支持服务器地址、制作包、会话、事件序号、输出目录和额外 Unreal 参数，并把日志写入 `Saved/Logs/`；非法参数 SHALL 非零退出。

#### Scenario: 启动导演客户端

- **WHEN** 操作者执行 `Scripts/Director.sh --server <host:port> --production <id>`
- **THEN** 客户端以 Director 进程角色连接服务器，日志包含服务器地址和制作包标识

#### Scenario: 执行重演冒烟

- **WHEN** 操作者执行 `Scripts/ReplaySmoke.sh`
- **THEN** 脚本创建会话、产生事件、重演到目标序号、创建分支并返回 0，日志写入 `Saved/Logs/replay-smoke.log`

#### Scenario: 非法重演目标

- **WHEN** 客户端请求不存在或早于保留窗口的事件序号
- **THEN** 脚本以非零状态退出并输出 `REPLAY_HISTORY_UNAVAILABLE`

### Requirement: 构建与内容摘要

构建与启动 SHALL 生成引擎版本、项目版本、规则 schema 和内容目录摘要，并在会话快照、Take 和重演结果中记录。摘要不一致 SHALL 触发明确的非精确重演或渲染错误，不得静默继续。

#### Scenario: 记录构建摘要

- **WHEN** 会话启动
- **THEN** `system.health` 和初始快照包含引擎、项目、规则和内容摘要

#### Scenario: 摘要变化

- **WHEN** 使用不同内容摘要打开旧会话重演
- **THEN** 系统报告 `REPLAY_VERSION_MISMATCH` 并要求显式允许非精确重演

#### Scenario: 摘要生成失败

- **WHEN** 内容目录不可读导致摘要无法生成
- **THEN** 沙盒会话创建被拒绝并返回 `CONTENT_DIGEST_UNAVAILABLE`

### Requirement: 沙盒与导演诊断

启动日志和 `system.health` SHALL 报告沙盒模块、导演模块、事件日志、检查点、重演队列、导演连接数和预算状态。日志 SHALL 标记 `Director` 和 `Replay` 进程角色，且不记录完整参与者令牌。

#### Scenario: 健康检查包含新模块

- **WHEN** 客户端调用 `system.health`
- **THEN** 返回沙盒与导演模块版本、事件日志可写性、重演能力和当前预算占用

#### Scenario: 进程角色标记

- **WHEN** 以导演或重演模式启动
- **THEN** 启动日志包含 `Epicode process role: Director` 或 `Epicode process role: Replay`

#### Scenario: 日志脱敏

- **WHEN** 导演命令被审计记录
- **THEN** 日志包含命令、参与者和结果，不包含完整参与者令牌

### Requirement: 运行时客户端脚本

系统 SHALL 提供 `Scripts/Viewer.sh`、`Scripts/AgentSmoke.sh` 和 `Scripts/ViewerSmoke.sh`。查看端脚本 SHALL 支持服务器地址、查看者名称、加入令牌、相机模式和额外 Unreal 参数；Agent 冒烟脚本 SHALL 不启动 Unreal 进程，只通过控制 API 完成加入、租约、移动、观察和释放。

#### Scenario: 启动查看端

- **WHEN** 操作者执行 `Scripts/Viewer.sh --server 127.0.0.1:7796 --name viewer-a`
- **THEN** 脚本启动 `UnrealEditor -game` 查看端，传入服务器地址和查看者身份，并把日志写入 `Saved/Logs/`

#### Scenario: 无效查看端参数

- **WHEN** 操作者传入非法服务器地址或缺少查看者令牌
- **THEN** 脚本在启动引擎前退出并返回非零状态，输出可读错误

#### Scenario: Agent 冒烟不启动引擎

- **WHEN** 操作者执行 `Scripts/AgentSmoke.sh`
- **THEN** 脚本通过 HTTP/WebSocket 完成 Agent 控制闭环，不启动新的 Unreal 进程，并输出通过或失败摘要

#### Scenario: 查看端冒烟

- **WHEN** 操作者执行 `Scripts/ViewerSmoke.sh`
- **THEN** 脚本启动服务器和查看端，验证相机、世界表现和只读拒绝，并返回与结果一致的状态码

### Requirement: 运行时进程角色

系统 SHALL 在启动日志中明确当前进程角色为 Editor、Headless、Server、Viewer 或 Agent。Agent SHALL NOT 加载 Unreal 运行时；Viewer SHALL 使用 Game 客户端模式且不启动控制 API；Server SHALL 以服务器模式启动并启动控制 API。

#### Scenario: 查看端日志

- **WHEN** Viewer 启动
- **THEN** 日志包含查看者标识、服务器地址、相机模式和 `Epicode API stays disabled in game client mode`

#### Scenario: Agent 日志

- **WHEN** Agent 冒烟运行
- **THEN** 日志包含参与者标识、会话标识、租约标识和命令摘要，不包含 Unreal 启动日志

#### Scenario: 服务器日志

- **WHEN** 服务器启动并接受 Viewer 和 Agent
- **THEN** 日志分别记录 Viewer 加入、Agent 加入、查看者槽位和 Agent 租约，不把 Viewer 计为表演者
