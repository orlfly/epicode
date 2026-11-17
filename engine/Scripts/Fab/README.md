# Fab 下载辅助脚本

这些脚本用于在 Linux 上没有 Epic Games Launcher 时，从 Fab 的 UE manifest/chunk 下载并校验资产。

## parse_manifest.py

把 Fab/Epic 返回的二进制 `.manifest` 转成 JSON（与 EAM 的 `manifest.json` 结构一致）。

```bash
python3 parse_manifest.py <input.manifest> <output.json>
```

## decode_json_manifest.py

Fab 新接口对部分资产返回 JSON manifest。它的二进制字段不是普通整数，而是把每个字节写成 3 位十进制数字再拼接，例如：

- `uint32` → 12 位（4 组）
- `uint64` → 24 位（8 组）
- SHA1 → 60 位（20 组）

本脚本把这些字段还原成 `fetch_asset.py` 需要的整数 JSON：

```bash
python3 decode_json_manifest.py <input.json> <output.json>
```

已验证方式：用 Lyra 5.0 的新 JSON manifest 解码后，与旧二进制 manifest 的 9,544 个文件、3,870 个 chunk、文件 SHA1、chunk hash/group/size 逐字段比对，0 差异。

## fetch_asset.py

按 manifest 下载所有 chunk、解包并按 `Offset/Size` 拼接文件，最后逐个校验 SHA1。

```bash
python3 fetch_asset.py <manifest.json> <destination> <distribution-base-url>
```

输出目录结构：

```text
<destination>/data/...      # 拼接后的文件
<destination>/_chunks/...   # chunk 缓存，可删除
```

`distribution-base-url` 来自 Fab 的 `download-info`，例如：

```text
https://egdownload.fastly-edge.com/Builds/Rocket/Automated/<App>/CloudDir
```

## check_assets.py

在 UE 编辑器 Python 环境中加载 `/Game/PK_Equestrian` 下的全部资产，用于验证复制进项目的资源确实可加载。

```bash
UnrealEditor-Cmd <project>.uproject -unattended -nosplash -nop4 -nullrhi \
  -ExecCmds="py $(pwd)/Scripts/Fab/check_assets.py"
```

日志中应出现：

```text
FAB_LOAD_CHECK total=236 failed=0
```

## 注意

- 这些脚本不处理 Epic 登录和令牌，只消费已经取得的签名 manifest URL 或二进制 manifest；已验证免费包的 `asset-formats` 与 `download-info` 可匿名访问。
- 新 Fab JSON manifest 先跑 `decode_json_manifest.py`，再交给 `fetch_asset.py`。
- 不要把访问令牌写入脚本或日志。
