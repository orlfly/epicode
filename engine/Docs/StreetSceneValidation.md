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

## 未通过/未完成项

| 检查 | 结果 | 说明 |
| --- | --- | --- |
| 免费包下载 | 未完成 | Fab 现在返回 JSON manifest；`parse_manifest.py` 在 magic 断言处失败（`0x614d227b`，即 JSON 的 `{"Ma`） |
| Chunk 路径 | 未解码 | 已探测的 `ChunksV3/ChunksV4`、hash/group 组合均 404 |
| UE 5.8 导入 | 未执行 | 没有下载到资产，未做 AssetRegistry/无头加载测试 |

## 复现要点

- 搜索：`GET https://www.fab.com/i/listings/search?q=<query>&channels=unreal-engine&count=40`
- 详情：`GET https://www.fab.com/i/listings/<uid>`
- 资产格式：`GET https://www.fab.com/i/listings/<uid>/asset-formats`
- 下载信息：`GET https://www.fab.com/i/listings/<uid>/asset-formats/<format>/files/<file_uid>/download-info`
- Downtown West manifest：1,252 文件、11,263 chunk；City Street Props manifest：751 文件、5,870 chunk。
- 两个 manifest 都是 JSON 文本，不是旧版二进制 manifest；`ManifestFileVersion` 为字符串 `013000000000`，尺寸字段带尾随零。

## 结论

- 报告中的价格、授权、评分、UE 版本、链接和表格数据已交叉校验。
- 仍未验证的是“实际下载后在 UE 5.8 中导入/渲染”，报告中已把相关表述限定为官方描述和缩略图证据。
