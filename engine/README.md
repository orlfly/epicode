# Epicode

Epicode 是面向视频制作的 Unreal Engine 5.8 编辑器扩展。`scene.json` 是静态布景真相源，`production.json` 是演员、规则和叙事的作者根，沙盒世界是运行时权威状态，Editor Actor 是投影；编辑器 UI、HTTP API、WebSocket、导演和渲染工作进程共享同一套命令注册表。

## 目录

```
engine/
  EpicodeVideo.uproject
  Config/DefaultEpicode.ini
  Plugins/
    EpicodeCore/        场景文档、实体 Actor、命令注册表、子系统
    EpicodeImport/      Interchange 导入队列
    EpicodeSandbox/     沙盒世界、规则、玩法、确定性随机和重演 journal
    EpicodeDirector/    叙事节拍、cue、接管、Take 和 ghost/resim
    EpicodeControlAPI/  HTTP/JSON-RPC/WebSocket
    EpicodeStudio/      Slate 管理器面板和导演工作区
    EpicodeRender/      Movie Render Pipeline 工作进程
    EpicodeNetwork/     多人会话、Agent/Viewer 和沙盒桥
  Scripts/              构建、启动、测试、冒烟、清理
../Docs/                设计、API、场景、导入、渲染、运维
```

## 快速开始

```bash
cd engine
./Scripts/env.sh --check
./Scripts/Build.sh
./Scripts/Editor.sh
```

编辑器内打开 `Window > Epicode Studio`。

完整操作流程、九个页签、导演工作区、制作包/沙盒/重演、导入/渲染工作流和 API 示例见 [使用说明](../Docs/Usage.md)。

无界面 API 服务：

```bash
./Scripts/Headless.sh
TOKEN=$(cat Saved/Epicode/api-token)
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8710/api/v1/health
```

## 文档

- [使用说明](../Docs/Usage.md)
- [架构](../Docs/Architecture.md)
- [场景模型](../Docs/SceneModel.md)
- [HTTP/JSON-RPC/WebSocket API](../Docs/API.md)
- [导入](../Docs/Import.md)
- [渲染](../Docs/Rendering.md)
- [制作包](../Docs/ProductionPackage.md)
- [沙盒世界](../Docs/SandboxWorld.md)
- [导演](../Docs/Director.md)
- [重演](../Docs/Replay.md)
- [控制 API](../Docs/ControlAPI.md)
- [Studio 面板](../Docs/Studio.md)
- [运维手册](../Docs/Operations.md)
- [验收记录](../Docs/Acceptance.md)

## 配置

`Config/DefaultEpicode.ini`：

```ini
[/Script/EpicodeCore.EpicodeSettings]
ApiHttpPort=8710
ApiWsPort=8711
bAllowRemote=false
bEnableCors=false
MaxRequestsPerSecond=200
MaxRequestBytes=8388608
MaxImportBytes=2147483648
RenderConcurrency=1
```

端口也可用 `EPICODE_API_HTTP_PORT`、`EPICODE_API_WS_PORT` 覆盖。令牌位于 `Saved/Epicode/api-token`。

## 测试

```bash
./Scripts/Test.sh
./Scripts/Smoke.sh
EPICODE_SMOKE_RENDER=1 ./Scripts/Smoke.sh
./Scripts/SandboxSmoke.sh
./Scripts/ReplaySmoke.sh
```
