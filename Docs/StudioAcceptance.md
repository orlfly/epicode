# Studio 验收记录

日期：2026-11-04  
环境：Linux x86_64，Unreal Engine 5.8.4，无头编辑器（`-nullrhi`）

## 自动化证据

| 测试 | 覆盖 |
| --- | --- |
| `Epicode.Studio.Panel` | 构造完整 Studio 面板并 tick；校验九个页签切换、Outliner 折叠/展开/筛选、四类实体 inspector 字段、角色/道具/灯光筛选空状态、导入/渲染任务行与 API 摘要 |
| `Epicode.Studio.Responsiveness` | 导入与渲染提交在 100ms 内返回，长任务不阻塞调用线程 |
| `Epicode.Studio.ViewModel` | 页签切换、revision 刷新门控、单选/多选、空选择与过期选择清理 |
| `Epicode.Core.Undo` | Studio 事务撤销/重做恢复场景文档与投影 |
| `Epicode.Import.Undo` | 导入作为单次事务撤销/重做 |
| `Epicode.Studio.MultiplayerPanel` | 构造多人面板，校验连接输入、会话控件、快照和空状态 |
| `Epicode.Studio.MultiplayerRoles` | 所有者、导演、表演者和观察者的按钮权限与角色行 |
| `Epicode.Studio.PerformerHud` | 当前角色、动作队列、情绪、表情强度、说话状态和口型源 |
| `Epicode.Studio.DirectorConsole` | 导演可操作、观察者只读，以及玩家/角色/相机/灯光入口 |
| `Epicode.Studio.SessionStates` | 重连中、角色权限变化和会话结束状态 |

干净构建证据：

- `engine/Saved/Logs/build-clean.log`：`Result: Succeeded`
- `engine/Saved/Logs/test-clean.log`：63/63 `Success`，0 `Fail`
- `engine/Saved/Logs/smoke-clean.log`：`Epicode smoke test passed`
- `engine/Saved/Logs/smoke-final-png.log`：`render succeeded, files: 3`

## 手工检查清单

- 打开 `Sandbox / Director Workspace`，确认 Overview 显示制作包、沙盒状态和预算。
- 切换到 Timeline，输入事件类型或参与者过滤，确认列表只保留匹配行。
- 以 director 身份进入 ghost/resim，确认 Controls 页签显示模式和分支；切换到 observer 后确认写控件不可用并显示 `VIEWER_READ_ONLY`。
- 选择不同 Take 版本，确认超过最新版本时显示 `TAKE_NOT_FOUND`。
- 观察长会话刷新，确认工作区构建/刷新不超过 100 ms 预算且 live 继续。

以下条目通过代码检查和命令级测试确认；当前无头环境未捕获逐页签截图。

- Scenes：新建、保存、另存、关闭、打开、删除、列表当前标记。
- Outliner：层级缩进、名称/类型/ID 过滤、多选、批量删除、展开折叠、拖放重挂接、复制、显示/隐藏、重命名、变换和按类型属性。
- Characters：资产选择器绑定骨骼网格/动画、生成、播放、暂停、删除、过滤。
- Props：资产选择器绑定静态网格/材质、材质槽、物理开关、删除、过滤。
- Lights：类型/强度/颜色创建、更新、删除、过滤。
- Environment：四个预设、时间/雾/云/曝光/泛光、应用。
- Import：多选浏览、拖放、自动放置/覆盖、任务列表、失败详情、取消、内容浏览器定位。
- Render：提交三帧 PNG、状态/进度、取消、刷新。
- API：`system.health`、渲染后端、命令数、场景、HTTP/WS 地址和令牌文件。

## 已知限制

- 未捕获 Studio 各页签的图形界面截图；验收以自动化测试、冒烟日志和代码检查为准。
- 关闭工作区后重新打开恢复进度的交互未单独自动化；导入/渲染任务由服务层持有，面板重建后会重新查询任务列表。
- 多人面板的 `Server` 输入仅预留，面板按钮只操作编辑器/Headless 进程内会话，尚未连接专用服务器。
