# 街景候选校验记录

日期：2026-11-13（UTC）
对象：`engine/Docs/StreetScenes.md`、`engine/Docs/street_scene_candidates.json`
原始数据：`~/Documents/EpicVault/fab_street_search/`（15 个搜索 JSON）、`~/Documents/EpicVault/fab_street_search/thumbnails/`（8 张官方缩略图）

## 通过项

| 检查 | 方法 | 结果 |
| --- | --- | --- |
| 搜索覆盖 | 12 组主检索 + 3 组定向检索，共 15 个 JSON | 每个主检索 24 条，定向检索 10 条；候选全部有来源 |
| 候选去重 | 按 listing UID 合并 | 21 个候选，无重复 UID |
| 详情一致性 | 21 个候选逐一请求 `GET /i/listings/<uid>`，比对 Personal 价格、USD 价格、评分、评分数 | 0 错误 |
| 授权价格 | 比对 `licenses[].priceTier` 的 Personal/Professional/UEFN/CC 档位 | 21/21 有授权记录；London 最低价 ¥333.43 确认为 UEFN Reference only |
| 报告与 JSON | 解析 Markdown 的 21 行候选表，逐行比对价格、UE 范围、评分 | 21/21 匹配，0 错误 |
| 链接可访问 | 对 21 个 `https://www.fab.com/listings/<uid>` 发 HEAD | 21/21 返回 200 |
| 缩略图 | 检查 8 张图片的尺寸、MD5、README 清单 | 8/8 存在，尺寸 1280x720 或 1920x1080 |
| UE 5.8 标注 | 检查每个候选的 `ue_versions` | 17 个含 UE_5.8；4 个明确低于 5.8，已在报告中注明 |

## 下载与校验

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| JSON manifest 解码 | 通过 | `decode_json_manifest.py` 还原 1,252 文件、11,263 chunk；用 Lyra 新旧 manifest 验证，0 差异 |
| Chunk 下载 | 通过 | 11,263/11,263 下载成功，`bad 0` |
| 文件 SHA1 | 通过 | 1,252/1,252 与 manifest 一致，0 missing / 0 bad |
| 体积 | 通过 | `data` 11,810,016,165 bytes（约 11.81GB） |
| 目录结构 | 通过 | `data/Content/Downtown_West/{Assets,Maps,Materials,Textures,VFX}` |
| SHA256 记录 | 通过 | `~/Documents/EpicVault/DowntownWest/SHA256SUMS`，1,252 行 |
| 项目内复制 | 通过 | `engine/Content/Downtown_West` 12GB、1,252 文件；`rsync --dry-run --checksum` 与源目录无差异 |
| 地图复制 | 通过 | `/Game/Epicode/Scenes/DowntownWest`，文件 51,820,973 bytes；使用 UE `EditorAssetLibrary.duplicate_asset`，不是磁盘复制 |
| UE 5.8 地图加载 | 通过 | `-game -nullrhi` 加载 `/Game/Epicode/Scenes/DowntownWest`，退出码 0，`LoadMap` 1.928296 秒，无地图加载错误 |
| Epicode 场景 | 通过 | `Downtown West`，`sceneId=01A1DAC5-004D-7F5D-B11E-A02B53A2EED2`，`levelPath=/Game/Epicode/Scenes/DowntownWest`；`scene.save`、`scene.close`、`scene.open`、`scene.runtime.get` 均通过；默认相机对齐地图内 `CineCameraActor`，Viewer 的 `cameraIds/defaultCameraId` 已设置 |

## 未完成项

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| UE 5.8 导入 | 部分完成 | 12GB 资源已复制到 `engine/Content/Downtown_West` 并通过 checksum；`Demo_Environment` 已复制为 `/Game/Epicode/Scenes/DowntownWest` 并用 `-game -nullrhi` 加载成功（1.93s）；Epicode 场景 `Downtown West` 已创建并重开验证。渲染工作进程现在会加载 `levelPath` 基础地图，已完成 Downtown West 640x360 单帧渲染验证；仍待完整 5.6→5.8 资产逐项迁移、材质/LOD 检查和多机位/长序列渲染验收。 |

## 复现要点

- 搜索：`GET https://www.fab.com/i/listings/search?q=<query>&channels=unreal-engine&count=40`
- 详情：`GET https://www.fab.com/i/listings/<uid>`
- 资产格式：`GET https://www.fab.com/i/listings/<uid>/asset-formats`
- 下载信息：`GET https://www.fab.com/i/listings/<uid>/asset-formats/<format>/files/<file_uid>/download-info`
- Downtown West manifest：1,252 文件、11,263 chunk；City Street Props manifest：751 文件、5,870 chunk。
- 两个 manifest 都是 JSON 文本，不是旧版二进制 manifest；`ManifestFileVersion` 为字符串 `013000000000`，尺寸字段带尾随零。
- 编码规则：每个字节写成 3 位十进制后拼接；`uint32` 为 4 组、`uint64` 为 8 组、SHA1 为 20 组，均按小端还原。`decode_json_manifest.py` 负责转换。

## 结论

- 报告中的价格、授权、评分、UE 版本、链接和表格数据已交叉校验。
- 下载与 SHA1/SHA256 校验已完成；UE 5.8 侧已完成资源落地、地图加载和 Epicode 场景创建/重开验证，但尚未做完整资产迁移审计和渲染验收。
