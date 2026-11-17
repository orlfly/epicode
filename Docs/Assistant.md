# Epicode Assistant 与编辑脚本

Epicode 的对话面板把自然语言转换为受控 JSON 编辑脚本。模型不能直接操作场景；所有修改先经过 `script.validate`，再由用户确认，最后由 `script.apply` 在一个事务内执行。后端不可用或脚本非法时，视口、对象列表、本地库和保存不受影响。

## 1. 后端配置

配置位于 `DefaultEpicode.ini` 的 `[/Script/EpicodeCore.EpicodeSettings]`，也可在本地 `Saved/Config/LinuxEditor/Epicode/EpicodeEditor.ini` 覆盖。

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `bAssistantEnabled` | `true` | 关闭后对话面板显示不可用 |
| `AssistantBackend` | `deepseek_sidecar` | `mock`、`openai_compat`、`deepseek_sidecar` |
| `AssistantEndpoint` | `https://api.deepseek.com` | OpenAI 兼容端点 |
| `AssistantModel` | `deepseek-chat` | 请求模型名 |
| `AssistantMaxContextChars` | `12000` | 本地上下文裁剪上限 |
| `AssistantMaxRepairAttempts` | `2` | 非法 JSON/校验失败时的自动修复次数 |
| `AssistantRequestTimeoutSeconds` | `60` | 流式请求超时 |
| `AssistantSidecarCommand` | `python3` | sidecar 启动命令 |
| `AssistantSidecarScript` | `Scripts/Assistant/DeepSeekHarnessSidecar.py` | sidecar 入口 |
| `AssistantSidecarMode` | `stdio` | `stdio` 或可选 `http` |
| `AssistantSidecarIdleSeconds` | `300` | stdio 空闲退出秒数 |

当前可用后端：

- `mock`：确定性的离线流式回复，用于 UI 演示和自动化测试。
- `openai_compat`：直接调用 DeepSeek `/chat/completions`，使用 HTTP/SSE 流式解析。
- `deepseek_sidecar`：默认值。编辑器启动 `Scripts/Assistant/DeepSeekHarnessSidecar.py`，通过 stdio JSONL 或可选本地 HTTP/SSE 与 `deepseek-harness-sdk` 通信。SDK 和 runtime 位于隔离 venv，不加载到 UE 进程。

### 1.1 Sidecar 安装

SDK 使用公开 PyPI 包 `deepseek-harness-sdk==0.2.2rc2`，许可证 MIT：

```text
https://pypi.org/project/deepseek-harness-sdk/
```

安装到项目隔离 venv：

```bash
cd engine
./Scripts/Assistant/SetupSidecar.sh
```

国内网络可指定镜像：

```bash
PIP_INDEX_URL=https://pypi.tuna.tsinghua.edu.cn/simple \
  ./Scripts/Assistant/SetupSidecar.sh
```

venv 位于 `engine/Saved/Epicode/Assistant/venv`，已被 git 忽略。`DeepSeekHarnessSidecar.py` 启动时会自动 re-exec 到该 venv；缺少 SDK 时返回 `sdk_missing` 并给出修复命令。

### 1.2 Sidecar 协议

默认 stdio 模式，每行一个 JSON 对象，UTF-8，单行上限 1 MiB：

```text
in : {"id":"1","op":"chat","prompt":"...","context":"...","history":[...]}
in : {"id":"1","op":"cancel"}
in : {"op":"ping"}
in : {"op":"shutdown"}
out: {"id":"1","event":"delta","text":"..."}
out: {"id":"1","event":"done","finishReason":"..."}
out: {"id":"1","event":"cancelled"}
out: {"id":"1","event":"error","code":"...","message":"..."}
out: {"event":"pong"}
out: {"event":"idle_exit"}
```

C++ 侧 `FDeepSeekHarnessSidecarBackend` 负责进程启动/复用、请求 ID 路由、超时、取消、崩溃重启和 stderr 诊断。stdio 模式下 sidecar 空闲 `AssistantSidecarIdleSeconds` 后退出，下一次请求自动重启。

可选 HTTP 模式：

```bash
cd engine
python3 Scripts/Assistant/DeepSeekHarnessSidecar.py --mode http --http-port 8720
```

提供 `GET /health`、`POST /v1/chat`（SSE）和 `POST /v1/cancel`，只监听 `127.0.0.1`。本地 token 位于 `engine/Saved/Epicode/assistant-token`（0600），请求使用 `Authorization: Bearer <token>` 或 `X-Epicode-Token`。编辑器默认仍使用 stdio。

## 2. API Key

优先级：

1. 环境变量 `DEEPSEEK_API_KEY`。
2. `engine/Saved/Epicode/deepseek.env` 中的 `DEEPSEEK_API_KEY=<key>`。

Key 文件模板首次读取时创建，权限应保持 `0600`。UI 只显示前四/后四掩码；日志、审计和错误信息不记录完整 Key。Key 不应写入仓库、对话历史或脚本。

## 3. 对话历史

历史保存到 `Saved/Epicode/Chat/<conversationId>.json`，schema 1。用户和助手消息落盘，系统提示和上下文不落盘；写入使用临时文件原子替换。启动时恢复最近会话，支持新建、清空和删除当前会话。

上下文由本地构建，包含场景摘要、当前选择、本地库摘要、命令 schema、约束和最近历史；超过 `AssistantMaxContextChars` 时优先裁剪历史和库列表，保留场景、选择和命令约束。

## 4. 编辑脚本 schema

````json
{
  "schemaVersion": 1,
  "summary": "添加主灯并把角色移到灯下",
  "requiresConfirmation": false,
  "operations": [
    {
      "clientId": "lamp",
      "method": "light.create",
      "params": {
        "lightType": "point",
        "name": "Key Light",
        "intensity": 5000
      }
    },
    {
      "method": "entity.setTransform",
      "params": {
        "id": "$lamp",
        "transform": {
          "location": [0, 0, 300],
          "rotation": [0, 0, 0],
          "scale": [1, 1, 1]
        }
      }
    }
  ]
}
````

约束：

- `schemaVersion` 必须为 `1`。
- `summary` 最多 200 字符；为空时规范化为“AI 编辑脚本”。
- `operations` 为 1–50 条；总影响实体最多 100 个。
- `clientId` 只允许字母、数字、`_`、`-`，最多 64 字符，同一脚本内不能重复。
- `$clientId` 只能引用同一脚本中更早的 operation。
- `entity.setTransform` 接受顶层 `transform` 对象，或顶层 `location`/`rotation`/`scale` 数组。
- 资产路径必须以 `/Game/` 开头且包存在。

允许的方法（24 个）：

`character.spawn`、`character.bind`、`character.play`、`character.pause`、`character.setSpeed`、`character.setMaterial`、`prop.spawn`、`prop.create`、`prop.setMaterial`、`prop.resetMaterial`、`prop.setPhysics`、`light.create`、`light.update`、`light.delete`、`camera.create`、`camera.update`、`camera.activate`、`camera.delete`、`entity.setTransform`、`entity.setVisible`、`entity.rename`、`entity.delete`、`entity.duplicate`、`scene.save`。

白名单之外的方法，包括 `import.*`、`render.*`、`system.*`、`director.*`、`sandbox.*`、控制台、Python、Lua、文件和网络命令，在 `script.validate` 阶段直接拒绝。脚本不能嵌套事务。

## 5. 校验、预览与应用

`script.validate` 只读，返回：

- `valid`、`requiresConfirmation`、`operationCount`、`affectedCount`。
- `normalizedScript`：补齐 `requiresConfirmation` 后的规范化脚本。
- `errors`：`{index, code, message}`，`index=-1` 表示脚本级错误。
- `warnings`：例如删除/活动相机需要确认。
- `preview`：每条 operation 的 `action`（create/modify/delete）、`target`、`fields`、`assets` 和 `destructive` 标记。

`script.apply` 会重新校验，然后：

1. 备份当前场景文档并记录 dirty 状态。
2. 在单个编辑器事务中逐条解析 `$clientId`，调用同一命令注册表；子命令使用 `bUndoable=false`。
3. 任一条失败时恢复备份、取消事务，返回失败码和 operation 序号。
4. 全部成功后提交事务，返回 `clientIds`、`affected`、`operationCount`、`warnings` 和 `auditPath`。
5. 一次 `Ctrl+Z` 撤销整个脚本。

删除实体、`camera.delete`、`camera.activate` 等破坏性操作会自动设置 `requiresConfirmation=true`。未传 `confirmed=true` 时返回 `CONFIRMATION_REQUIRED`，场景不变。

对话面板在流式回复完成后自动提取回复中的 JSON 脚本并调用 `script.validate`。助手消息下方显示脚本卡片：

- 标题：操作数、影响实体数和脚本摘要。
- 状态：校验通过/失败、是否需要确认、应用成功或失败原因。
- 预览：每条 operation 的动作（新建/修改/删除）、方法、目标、字段、资产路径和破坏性标记。
- “定位”：目标为已有实体 GUID 时选中并聚焦视口。
- “应用”：先做确认检查，成功后在单个事务内执行并刷新对象列表、检查器和视口。
- “取消”：清除待应用脚本，不修改场景。

如果回复包含脚本但 JSON 无法解析，或 `script.validate` 返回 `valid=false`，对话面板会自动发送一条修复请求，只附带校验错误并要求返回完整 JSON。修复次数受 `AssistantMaxRepairAttempts` 限制；耗尽后卡片显示“自动修复 N 次后仍失败”，停止发送，场景保持原样。

## 6. 审计

审计写入 `engine/Saved/Epicode/Script/audit.jsonl`，每行一个 condensed JSON 对象：

```json
{"timestamp":"...","conversationId":"...","model":"deepseek-chat","backend":"openai_compat","summary":"...","operationCount":3,"affectedCount":3,"success":true,"confirmed":true,"errorCode":"","durationMs":2.1}
```

不记录完整脚本正文和密钥。审计写入失败只返回 `auditWarning`，不阻止已成功的场景修改。

## 7. 隐私与安全

- 不把 API Key、完整文件系统路径、资产二进制或未选择实体发送给模型。
- 本地库只发送名称、路径、标签和摘要。
- 模型输出必须经过 schema、白名单、引用、范围和资产校验。
- 用户拒绝确认时不执行脚本。
- 场景修改失败回滚，不残留已创建实体。

## 8. 当前限制

- 真实模型调用需要用户提供 `DEEPSEEK_API_KEY` 和可用网络；自动化测试使用 `TestSidecar.py` 和 `mock`，不验证模型输出质量。
- sidecar HTTP 模式已提供，但编辑器 C++ 客户端默认仍走 stdio；HTTP 模式当前用于本地诊断/外部客户端。
- 脚本只允许 24 个方法和已有实体/资产，不支持层级重挂接、环境、渲染、网络和导入操作。
