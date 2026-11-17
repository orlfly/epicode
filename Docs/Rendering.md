# 渲染

## 流程

1. `render.submit` 校验请求并创建任务目录 `Saved/Epicode/RenderJobs/<jobId>/`。
2. 写入 `request.json`、`meta.json`。
3. 保存当前场景。
4. 启动独立 `UnrealEditor-Cmd` 工作进程，传入 `-EpicodeRenderWorker=<jobId>`。
5. 工作进程新建空白关卡，按场景 JSON 重建实体，生成临时 Level Sequence，构建 Movie Render Pipeline 队列并执行。
6. MP4 请求先生成 PNG 帧序列，再调用系统 `ffmpeg` 编码为 H.264；其他格式直接输出帧序列。
7. 工作进程写 `result.json` 后退出；主进程轮询到终态。

## 请求

```json
{
  "sceneId": "optional-scene-id",
  "cameraId": "optional-camera-id",
  "outputDirectory": "Saved/Epicode/Renders/Demo",
  "format": "png",
  "codec": "h264",
  "width": 1920,
  "height": 1080,
  "frameRate": 24,
  "startFrame": 0,
  "endFrame": 120,
  "quality": 20,
  "overwrite": true,
  "includeAudio": false
}
```

- `format`：`png`、`exr`、`jpg` 或 `mp4`。
- PNG/EXR/JPG 为帧序列；MP4 使用 H.264，`quality` 为 CRF 16–51。
- `endFrame` 为排他上界，必须大于 `startFrame`；`startFrame=0,endFrame=3` 渲染 3 帧。
- 分辨率范围 16–8192，帧率 1–240。
- `outputDirectory` 为空时使用 `Saved/Epicode/Renders/<jobId>`。

## 提交与轮询

```bash
JOB=$(curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{"method":"scene.render","params":{"format":"png","width":320,"height":180,"startFrame":0,"endFrame":3}}' \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["result"]["jobId"])')

curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d "{\"method\":\"render.status\",\"params\":{\"jobId\":\"$JOB\"}}"
```

## 输出

`render.status` 的 `result` 包含：

- `state`：`queued/running/succeeded/failed/cancelled`
- `stage`：工作进程当前阶段
- `outputDirectory`
- `outputFiles`：绝对路径列表
- `renderedFrames`
- `durationSeconds`
- `errorCode`、`errorMessage`

任务目录同时写入 `request.json`、`meta.json`、`worker.log`、运行期 `progress.json` 和终态 `job.json`。`progress.json` 每秒刷新，包含 `state`、`stage`、`progress`、`renderedFrames` 和 `totalFrames`；任务结束或取消时写入终态。

默认一次只运行一个渲染任务（`RenderConcurrency=1`）；已有任务运行时提交的新任务进入 `queued`，前一个任务结束后自动启动。排队任务可直接取消。运行超过 `RenderTimeoutSeconds`（默认 1800 秒）的任务会被终止并标记 `RENDER_TIMEOUT`。

如果场景文档的 `levelPath` 指向存在的 UE 地图（例如 `/Game/Epicode/Scenes/DowntownWest`），工作进程会先加载该地图作为基础关卡，再叠加 Epicode 实体、环境和相机，然后保存为本次任务的临时关卡。这样导入的街景地图内容会进入最终画面；没有有效 `levelPath` 的场景仍使用空白临时关卡。渲染相机以 Level Sequence 的可生成对象（spawnable）创建，避免 PIE 复制地图后找不到场景相机。

父进程在标记成功前校验产物：文件数量等于 `endFrame - startFrame`（MP4 为 1），文件非空，PNG/JPG 的分辨率与请求一致。每个产物的 SHA-1 写入 `result.json` 和 `job.json` 的 `checksums` 字段。

失败时 `result.json` 写入 `retryable` 和 `diagnostics`：显存不足和 GPU 设备丢失标记为可重试，磁盘写满、缺少相机/场景资产标记为不可重试，并给出对应诊断。`system.health` 同时返回 `gpuAdapter`、`gpuVendorId` 和 `gpuDeviceId`。

## Take 渲染

Take 渲染以 `take.render` 提交。Take 是不可变版本，渲染读取 Take 固化的区间、内容摘要和镜头轨道，不复用提交时的 live 世界。

```bash
curl -sS -X POST http://127.0.0.1:8710/api/v1/command \
  -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
  -d '{
    "method": "take.render",
    "params": {
      "takeId": "take-042",
      "takeVersion": 3,
      "outputDirectory": "Saved/Epicode/Renders/take-042-v3",
      "format": "png",
      "expectedContentDigest": "<take.contentDigest>",
      "tracks": [
        {"cameraId": "<camera-a-guid>", "startSequence": 10, "endSequence": 20},
        {"cameraId": "<camera-b-guid>", "startSequence": 20, "endSequence": 30}
      ]
    }
  }'
```

参数：

- `takeId`、`takeVersion`：必填，指向 `<takeRoot>/takes/<takeId>-v<version>.json`。
- `takeRoot`：可选，默认 `Saved/Epicode/Takes`。
- `expectedContentDigest`：可选；非空且与 Take 的 `contentDigest` 不一致时拒绝。
- `outputDirectory`：可选；默认 `<takeRoot>/<takeId>-v<version>/render`。
- `tracks`：可选；每项包含 `cameraId`、`startSequence`、`endSequence`。无显式轨道时使用 Take 的第一台相机覆盖整个区间。

提交前依次校验 Take 版本、`contentDigest`、轨道顺序与相机列表、区间和输出目录可写。任一失败返回稳定错误且不创建作业：

- `TAKE_NOT_FOUND`、`TAKE_INVALID`：Take 或版本不存在、元数据无效。
- `TAKE_CORRUPT`：Take 校验和与文件内容不一致。
- `TAKE_DIGEST_MISSING`、`TAKE_DIGEST_MISMATCH`：缺少内容摘要或与期望值不一致。
- `TAKE_CAMERA_MISSING`：轨道相机不在 Take 的 `cameraIds` 中。
- `TAKE_TRACK_OVERLAP`、`TAKE_TRACK_OUT_OF_RANGE`、`TAKE_TRACK_INVALID`：轨道重叠、越界或没有可渲染帧。
- `TAKE_OUTPUT_UNWRITABLE`：输出目录无法创建或写入。

每个有效轨道提交一个独立渲染作业，输出目录为 `<outputDirectory>/track_<序号>_<cameraId>`。`take.render` 的 `result` 保留首个作业的 `jobId` 和状态，同时返回：

- `takeId`、`takeVersion`、`contentDigest`、`manifestPath`。
- `trackCount` 和 `tracks`：每项的 `cameraId`、`track`、`startFrame`、`endFrame`、`outputDirectory`、`jobId`。
- `jobs`：全部轨道作业的完整状态；用 `render.status` 或 `render.cancel` 按各作业标识查询和取消。

序列号到帧号的映射为 `inTick + (sequence - inSequence) * (outTick - inTick) / (outSequence - inSequence)`，区间端点分别落在 `inTick` 和 `outTick`。

提交成功后写出 `<outputDirectory>/take-render.json`：

```json
{
  "schema": "epicode.take-render.v1",
  "jobId": "first-job-id",
  "jobIds": ["first-job-id", "second-job-id"],
  "takeId": "take-042",
  "takeVersion": 3,
  "takeChecksum": "...",
  "contentDigest": "...",
  "computedDigest": "...",
  "outputDirectory": "...",
  "startFrame": 100,
  "endFrame": 200,
  "tracks": [
    {
      "cameraId": "...",
      "track": "track_00_camera-a",
      "startFrame": 100,
      "endFrame": 150,
      "outputDirectory": ".../track_00_camera-a",
      "outputPattern": "track_00_camera-a_%04d",
      "jobId": "first-job-id"
    }
  ],
  "createdAt": "2026-11-05T15:00:00.000Z"
}
```

`computedDigest` 由 Take 的规范化 JSON 计算。重复渲染同一 Take 版本时，轨道帧范围、输出目录和命名规则一致，但每个作业标识独立；产物验证沿用普通渲染作业的帧数、非空、分辨率和 SHA-1 检查。

## 工作进程

主进程通过绝对路径启动 `UnrealEditor-Cmd`，参数包含 `-EpicodeRenderWorker=<jobId>`、`-unattended`、`-nosplash`、`-NoSound`、`-stdout`、`-RenderOffscreen`。工作进程在 `OnPostEngineInit` 检测到该参数后执行一次渲染并 `RequestExit`。

日志位于任务目录下的 `worker.log`。失败时先看该文件，再看 `result.json`。

## 限制

- 需要可用的渲染后端；`-nullrhi` 的 API 冒烟进程不能完成实际像素输出。
- `scene.render` 只输出活动相机视角；多相机切镜使用 `take.render` 的逐轨道作业。两者都不支持字幕和音频混音。
- MP4 统一通过 `ffmpeg` 编码；引擎自带 MP4 编码器在 Linux 上可能报告 unsupported。缺少 `ffmpeg` 时任务返回 `RENDER_MP4_ENCODE_FAILED`。
- `includeAudio` 首版忽略，MP4 无音轨。
- 取消任务会尝试终止工作进程；已写出的帧不会自动删除。
- `render.retry` 用原请求重新提交已结束任务；任务超过 30 分钟会被父进程终止并写入 `RENDER_TIMEOUT`。
