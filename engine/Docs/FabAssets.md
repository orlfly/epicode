# Fab 资源下载记录

日期：2026-11-11（UTC）
账号：`xiao.xf@gmail.com`（Epic 显示名 `Orffy-xiao`）
引擎：`/usr/local/unreal`（UE 5.8.4）

## 已获取资源

| 资源 | 类型 | 引擎版本 | 大小 | 路径 |
| --- | --- | --- | --- | --- |
| Akuma（JonCG，Fab 免费） | MetaHuman 角色，`.mhpkg` | 5.8 | 781 MB | `~/Documents/EpicVault/Akuma/mhc_akuma.mhpkg` |
| FREE Farm Paths and Stone Walls Kit（Lauraa，Fab 免费） | 完整 UE 项目，模块化场景 | 5.8 | 177 MB | `~/Documents/EpicVault/FarmPaths_UE58/data` |
| Lyra Starter Game（Epic，Fab 免费） | 完整 UE 项目，含角色/地图/武器 | 5.0 | 3.9 GB | `~/Documents/EpicVault/Lyra_5.0/data` |
| Downtown West Modular Pack（PurePolygons，Fab 免费） | 完整 UE Content 街区（`Content/Downtown_West`） | 4.26-5.6 | 11.81 GB | `~/Documents/EpicVault/DowntownWest/data` |

Farm Paths 已复制到本项目：

```text
engine/Content/PK_Equestrian/
```

其内部资源路径为 `/Game/PK_Equestrian/...`，复制后保持原路径，引用不会断开。

## 校验

- Akuma：`unzip -t` 通过，无错误；包含 `MHC_Akuma.uasset`、`Unpacked/Akuma/**` 等。
- Farm Paths：177/177 个 chunk 下载成功，238 个文件 SHA1 与 Fab manifest 全部一致（`bad 0`）。
- Lyra：9544 个文件 SHA1 与 Fab manifest 全部一致。
- Downtown West：Fab 新 JSON manifest 用 `Scripts/Fab/decode_json_manifest.py` 解码；11,263/11,263 个 chunk 下载成功，1,252/1,252 个文件 SHA1 与 manifest 全部一致（missing 0、bad 0），`data` 11,810,016,165 bytes；`~/Documents/EpicVault/DowntownWest/SHA256SUMS` 已生成 1,252 行。
- 启动 `UnrealEditor-Cmd EpicodeVideo.uproject -game -nullrhi` 检查，日志无资产加载错误（仅编辑器既有警告）。
- 强制 AssetRegistry 扫描：`NumCachedFiles 8983, NumUncachedFiles 236`，无 `Error:`，说明 236 个新增文件已被发现并索引。
- 无头 Python 实际加载检查：`FAB_LOAD_CHECK total=236 failed=0`，即 `/Game/PK_Equestrian` 下全部 236 个资产都能被 UE 5.8 成功加载（脚本 `Scripts/Fab/check_assets.py`）。
- 校验和记录：`~/Documents/EpicVault/SHA256SUMS`；Farm Paths 数据目录 238 个文件，复制进项目 236 个（`Content` 部分）。

## 使用方式

### Farm Paths

资源已在项目内，打开编辑器后可在 Content Browser 的 `Content/PK_Equestrian` 下找到：

- `Meshes/FarmPaths`：路面、石墙、台阶、门等模块
- `Materials` / `Textures`：材质与贴图
- `Blueprints`：演示 Blueprint
- `Maps`：展示关卡

可直接拖入 Epicode 场景，或参考 `Maps` 中的摆放。

### Downtown West

资源只包含 UE Content，不含 `.uproject`：

```text
~/Documents/EpicVault/DowntownWest/data/Content/Downtown_West/
├── Assets/
├── Maps/
├── Materials/
├── Textures/
└── VFX/
```

在 UE 5.8 中使用有两种方式：

1. 复制 `Content/Downtown_West` 到 `engine/Content/Downtown_West`，重新扫描 AssetRegistry；首次打开会触发 4.26/5.6 → 5.8 的资产版本转换，建议先备份。
2. 新建临时 UE 5.8 项目，把 `Content/Downtown_West` 复制进去，确认材质/贴图/地图能加载，再只迁移需要的子目录到 Epicode。

### Akuma

`.mhpkg` 是 MetaHuman 包，需在 UE 5.8 编辑器中导入：

1. 确认 MetaHuman 插件已启用。
2. Content Browser → Import → 选择 `~/Documents/EpicVault/Akuma/mhc_akuma.mhpkg`。
3. 按提示解包，得到 `MHC_Akuma` 与 `Unpacked/Akuma`。
4. 将 `BP_Akuma` 拖入场景，或通过 MetaHuman Creator 调整外观。

### Lyra

Lyra 是 UE 5.0 项目，直接在本项目中使用需先升级/迁移：

```bash
# 仅作为独立项目打开（会触发 5.0 -> 5.8 转换，建议先备份）
/usr/local/unreal/Engine/Binaries/Linux/UnrealEditor \
  ~/Documents/EpicVault/Lyra_5.0/data/Lyra.uproject
```

若只需要其中部分角色/场景，建议在 Lyra 项目中用 Content Browser 的 `Asset Actions → Migrate` 迁移到 `EpicodeVideo`，避免整包 3.9 GB 进入本项目。

## Linux 下载方法（可复现）

Epic 没有 Linux 官方启动器。本次使用 Epic Asset Manager（Flatpak）完成认证和资产获取，关键点：

1. EAM 在 Wayland 下无法被 `xdotool` 注入点击；用 Xvfb 跑一个独立的 X11 显示：
   ```bash
   Xvfb :2 -screen 0 1920x1080x24 -ac
   env DISPLAY=:2 flatpak run --env=GDK_BACKEND=x11 \
     io.github.achetagames.epic_asset_manager
   ```
2. EAM 的 Fab 页面可以浏览全站免费资源，但部分免费条目的 `price_label` 是 `$0.00` 而非 `Free`，EAM 不会直接“加入库”；用浏览器登录 Fab 后点 `Add to My Library` 即可。
3. UE 资产在 Fab 网页只提供 `View in Launcher`，不提供直接 zip。本次通过 Fab 的 `download-info` 接口取得签名 manifest，再按 manifest 的 chunk 列表下载并解包：
   - `GET /i/listings/{uid}/asset-formats` 取文件 UID
   - `GET /i/listings/{uid}/asset-formats/{format}/files/{file_uid}/download-info` 取 manifest URL
   - manifest 是二进制格式（magic `0x44BEC00C`），chunk 目录按 `ManifestFileVersion` 选择 `ChunksV3`/`ChunksV4`
   - 每个 `.chunk` 是带 header 的 chunk 容器，数据可能 zlib 压缩；解压后按 `Offset/Size` 拼接，最后校验文件 SHA1
4. 下载辅助脚本已纳入项目：`Scripts/Fab/parse_manifest.py`、`Scripts/Fab/fetch_asset.py`、`Scripts/Fab/check_assets.py`（无头加载校验）。

## 备注

- EAM 的 Fab 库接口在本机曾出现 `www.fab.com` TCP 超时；不影响已取得的 manifest 和 chunk 直链。
- 不要把 Epic 访问令牌写入仓库或日志；本次临时凭据文件已删除。
