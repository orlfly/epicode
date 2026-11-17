# Fab 街景场景候选

日期：2026-11-13（UTC）
用途：为 Epicode 的街景拍摄筛选 UE 场景资源；Downtown West 已在项目中落地（见“项目内落地”），其余候选尚未导入。
价格：Fab 中国区返回的 Personal 价格（Professional 另计），括号内为对应 USD 标价；实际以下单页为准。London 的最低 ¥333.43 是 UEFN Reference only，不适用于 UE。
链接：`https://www.fab.com/listings/{uid}`。

## 结论

- **综合首选：London - British Environment**。真实伦敦 7 Dials 街区的摄影测量场景，1,920+ 资产、8,500 m²、日/日落/阴雨三种光照，4K 级贴图（官方称 mostly 4K），UE 5.4-5.8，4.9/5（10 条评分），¥1,333.92（$199.99，Personal；Professional ¥2,667.92 / $399.99）。注意：列表最低的 ¥333.43 是 UEFN Reference only，不是 UE 授权。这是本次候选中官方展示场景最完整的一组，但仍需在 UE 5.8 中验证导入、材质和性能。
- **零预算先试：Downtown West Modular Pack + City Street Props**。前者是免费完整街区（350+ 网格、约 11.8GB、4K、含展示地图），后者是免费 305 件 Nanite 街道道具，可补细节。Downtown West 官方最高标到 UE 5.6，进 5.8 前需做一次迁移验证。
- **中国街景：Chinese Streets**。中式夜市/密集街巷，91 个静态网格、展示关卡、模块化建筑蓝图、Lumen，UE 5.1-5.8，¥533.53（$79.99）。规模不大，但在本次候选中式题材最直接。
- **自定义美式街区：City Streets - Modular Pack**。1,039 个网格、20 栋预制楼、内外景一体、Lumen/Nanite，UE 5.3-5.8，¥867.03（$129.99）。适合需要大量自定义机位和室内外连拍的项目。
- **夜景/雨夜：Rainy Neon Streets**。真实雨夜霓虹街道，132 网格、42 个模块化件、雨 VFX、可绘制湿润/水洼材质，UE 5.1-5.8，5.0/5，¥466.83（$69.99）。雨夜材质与灯光卖点最匹配电影感夜景；如果要更重的霓虹城市，选 Cyberpunk Night Street。

证据等级：价格/授权/评分/UE 版本来自 Fab 搜索与详情接口；资产数量/贴图/光照来自官方 `technicalDetails` 和描述；画面类型来自官方缩略图抽查；“适合拍什么”是编辑判断，尚未做 UE 5.8 导入/渲染实测。

## 付费候选

| 场景 | 价格（授权） | UE | 评分 | 规模/关键规格 | 适合 | 注意 |
| --- | --- | --- | --- | --- | --- | --- |
| **London - British Environment**（Scans Factory） | ¥1,333.92（$199.99） | 5.4-5.8 | 4.9（10） | 1,920+ 资产、1,010+ 网格、8,500 m²、摄影测量、3 种光照、4K 级贴图（官方称 mostly 4K）、演示地图 | 真实伦敦街道，店面/餐厅/路面/植被齐全，官方展示场景完整，适合优先验证取景；模块化可扩展 | Professional ¥2,667.92；最低 ¥333.43 是 UEFN Reference only，不适用于 UE；夜景在 v1.1 路线图中，当前无夜景；演示车辆不含在包内 |
| **Modern City Downtown Megapack**（Leartes） | ¥666.93（$99.99） | 4.19-5.8 | 4.5（123） | 355 个模型、模块化建筑、餐厅/银行/地铁内景、车辆、45 种贴花、示例地图、ULAT 工具 | 美式市中心，内景和外景都覆盖，适合城市叙事/广告 | Professional ¥1,333.92；美式风格；部分工具需按文档走 ULAT 流程 |
| **City Streets - Modular Pack**（PropHaus） | ¥867.03（$129.99） | 5.3-5.8 | 4.76（25） | 1,039 个网格、20 栋预制楼、内外景模块、Lumen/Nanite、示例地图 | 自定义美式街区，模块支持 exterior + interior 连镜 | Professional ¥3,168.17；较重，官方建议 12 核 CPU / RTX 2080；价格较高 |
| **Modular Amsterdam City Street**（Barrel Bark） | ¥666.93（$99.99） | 5.0-5.8 | 4.73（56） | 377 网格（5.5+ 为 490）、运河/桥/船、咖啡馆/店内饰、自行车、2K/4K 贴图、Nanite | 运河、鹅卵石、咖啡馆，欧洲街景辨识度高；已更新夜景和店内饰 | 需启用 HDRI 插件；风格限定荷兰 |
| **City Core - Paris**（PolySphere） | ¥1,467.32（$219.99） | 5.2-5.8 | 5.0（3） | 1,000+ 网格、约 7.5GB、15 个主材质/370 个实例、程序化街区工具 | 高端巴黎街区，适合俯拍/全景/建筑特写 | 价格高、体积大；评分样本少 |
| **Downtown - City Pack**（PolySphere） | ¥1,467.32（$219.99） | 4.25-5.8 | 4.36（22） | 609 网格、程序化工具、背景城市、街道道具、NYC/曼哈顿风 | 快速生成大规模市中心，适合远景/俯拍 | 价格高；程序化工作流需要学习 |
| **European City Streets**（PixelPress） | ¥466.83（$69.99） | 5.5-5.8 | 5.0（2） | 320 网格、模块化建筑/路面/人行道、程序化电车线、路牌工具、示例场景；1K-2K 贴图 | 欧洲古典街景，性价比高，适合浪漫/复古镜头 | 演示场景需启用 HDRIBackdrop 插件；近景细节不如 4K 包 |
| **Chinese Streets**（LAYA DESIGN） | ¥533.53（$79.99） | 5.1-5.8 | 5.0（5） | 91 个静态网格、展示关卡、模块化建筑蓝图、市场/摊位/招牌/线缆/空调、Lumen，整包 <1GB | 中式夜市/密集街巷，本次候选中式题材最直接，轻量易改 | Professional ¥1,333.92；网格数量有限，标签含 Lowpoly，近景需补道具 |
| **Cyberpunk Night Street**（FreshCan） | ¥466.83（$69.99） | 5.1-5.8 | 4.63（8） | 350+ 网格、模块化建筑、道路/街道道具、废弃建筑、霓虹、Nanite（5.1+）、示例地图 | 夜景/赛博朋克/科幻街景，霓虹氛围强 | 风格强烈，不适合写实日常街景 |
| **Rainy Neon Streets**（Maarten Hof） | ¥466.83（$69.99） | 5.1-5.8 | 5.0（5） | 132 网格、42 模块化、1 长街+2 短街、雨 VFX、湿润/水洼材质、335 贴图、Lumen/Nanite、2 张地图 | 真实雨夜霓虹街道，电影感强，适合夜景/雨戏 | Professional ¥933.73；场景规模较小，只有 3 条街；无文档 |
| **Back Alley**（UB） | ¥200.03（$29.99） | 4.26-5.8 | 5.0（1） | 模块化街道/墙体蓝图、顶点色、贴花系统、展示地图、夜景 | 低成本后巷，适合犯罪/夜景特写 | 仅 1 条评分，质量样本少 |
| **Modular Street Pack**（FinlayPearston） | ¥166.68（$24.99） | 4.22-5.8 | 5.0（3） | 模块化路面/人行道/路缘/街道道具、PBR、夜间灯光支持 | 低成本搭 blocking 和基础街景 | 资产量少，细节有限 |
| **Modular Street (Industrial Slums...)**（Hivemind） | ¥200.03（$29.99） | 5.5-5.8 | 4.6（5） | 191 网格、模块化管道/贫民区/小巷、顶点绘制、蓝图材质控制 | 工业贫民区/脏旧街巷，官方标到 5.8 | 题材限定，不适合干净市中心 |
| **Tokyo Street Part 2**（Kyrylo Sibiriakov） | ¥600.23（$89.99） | 5.2-5.6 | 4.5（2） | 东京街头 demo 关卡、模块化、LOD/碰撞、夜景 | 东京街头/霓虹巷弄 | 官方最高 UE 5.6，进 5.8 需迁移验证 |
| **Downtown Alley**（Emran Arts） | ¥200.03（$29.99） | 4.19-5.7 | 4.65（51） | 高质量 PBR、LOD、市中心小巷 | 便宜且有 51 条评分的小巷方案 | 官方最高 UE 5.7，进 5.8 需迁移验证 |
| **Cairo Street**（Barrel Bark） | ¥600.23（$89.99） | 5.0-5.8 | 5.0（1） | 356 网格、模块化埃及老城街道 | 开罗/中东老城街景，异域题材 | 仅 1 条评分，题材限定 |
| **Feudal Japan Megapack**（Meshingun） | ¥1,333.92（$199.99） | 5.1-5.8 | 4.64（33） | 预制展示地图、日本町屋/市场/寺庙/庭院、模块化 | 江户/战国时期日本街区，历史题材 | 价格高；不是现代街景 |

License 价格备注：表内为 Personal。Professional 另计：London ¥2,667.92、Modern ¥1,333.92、City Streets ¥3,168.17、Amsterdam ¥1,333.92、Paris/Downtown City Pack ¥3,001.42、Chinese ¥1,333.92、Rainy Neon/European ¥933.73。London 列表最低的 ¥333.43 是 UEFN Reference only，不是 UE 授权。完整数字见 JSON 的 `license_prices_cny`。商用/团队项目请按 Fab 条款选择 Professional，必要时向 Fab 支持确认。

## 免费候选

| 场景 | 价格（授权） | UE | 评分 | 规模/关键规格 | 适合 | 注意 |
| --- | --- | --- | --- | --- | --- | --- |
| **Downtown West Modular Pack**（PurePolygons） | 免费 | 4.26-5.6 | 4.54（148） | 350+ 网格、约 11.8GB、550+ 张 4K 贴图、250+ 材质/实例、展示地图、10+ 蓝图、Ray Tracing 演示 | 免费完整美式 downtown，模块化街道/建筑/道具，口碑样本大 | Personal/Professional 均免费；官方最高 UE 5.6；5.8 使用前需迁移验证；高画质设置对硬件要求高 |
| **City Street Props**（Dekogon） | 免费 | 5.0-5.8 | 4.56（16） | 305 个网格、Nanite、分层材质、Lumen 演示场景 | 免费街道道具库，补招牌/咖啡店/容器/城市家具 | Personal/Professional 均免费；只是道具，不是完整街区 |
| **Modular Urban houses**（Replex） | 免费 | 5.0-5.4 | 5.0（4） | 41 网格、10 栋预制住宅、可平铺墙面、Lumen、2K 贴图 | 免费街边住宅立面，快速搭街区 | CC-BY，需署名；官方最高 UE 5.4；资产量小 |
| **City Sample**（Epic Games） | 免费 | 5.0-5.8 | 4.17（157） | Matrix Awakens 完整城市、建筑/车辆/人群、Nanite/Lumen/World Partition、PCG 示例 | 官方标到 5.8 的完整城市参考，街道规模最大 | Personal/Professional 均免费；体积和硬件要求极高，属于完整项目而非轻量资产包；不建议直接并入 Epicode |

## 选择建议

### 按拍摄目标

- **真实欧美街景、最接近直接出片**：London。摄影测量、4K、三种天气/时段，官方展示场景最完整。
- **美式市中心、需要内景**：Modern City Downtown Megapack 或 City Streets - Modular Pack。前者内景/车辆/贴花更全，后者模块更细、可自定义程度更高。
- **欧洲经典街景**：European City Streets（性价比）或 City Core - Paris（高端）。想要运河/咖啡馆的独特氛围，选 Amsterdam。
- **中国城市街景**：Chinese Streets。轻量、题材准，搭配免费 City Street Props 补细节。
- **夜景/雨夜**：Rainy Neon Streets（真实雨夜）或 Cyberpunk Night Street（更重的霓虹/赛博朋克）；Amsterdam 已带夜景，London 夜景在路线图中。
- **预算有限**：Back Alley（¥200.03）或 Modular Street Pack（¥166.68）。
- **零预算试水**：Downtown West Modular Pack + City Street Props。先验证 5.8 迁移和渲染性能，再决定是否买付费包。

### 兼容性与成本

- 明确标到 **UE 5.8** 的候选：London、Modern City Downtown、City Streets Modular、Amsterdam、Paris、Downtown City Pack、European City Streets、Chinese Streets、Cyberpunk Night Street、Rainy Neon Streets、Back Alley、Modular Street Pack、Industrial Slums、Cairo、Feudal Japan。
- **未标到 5.8**：Tokyo Street Part 2（5.6）、Downtown Alley（5.7）、Downtown West（5.6）、Modular Urban houses（5.4）。UE 5.8 通常能打开旧版本资产，但材质/插件/蓝图可能需要迁移修复，不能当作“已验证”。
- **体积**：Downtown West 约 11.8GB；City Core Paris 约 7.5GB；City Streets Modular 网格多、加载重；Chinese Streets <1GB，适合快速迭代。
- **硬件**：City Streets Modular 官方建议 12 核 CPU / RTX 2080；London 官方称 120 FPS，测试环境是 RTX 4080 / Ryzen 9 / 64GB / SSD；Downtown West 高画质设置面向高配 PC。

## 方法与证据

- 数据来自 Fab 公开接口，无需登录：
  - 搜索：`GET https://www.fab.com/i/listings/search?q=<query>&channels=unreal-engine&count=40`
  - 详情：`GET https://www.fab.com/i/listings/<uid>`
- 检索词：12 组主检索（`street`、`city street`、`urban environment`、`modular street`、`alley`、`cyberpunk street`、`japanese street`、`european street`、`night street`、`downtown city`、`street scene`、`city block`）+ 3 组定向检索（`Downtown West Modular Pack`、`Modular Urban houses`、`City Sample`）。原始搜索 JSON 共 15 个，保存在 `~/Documents/EpicVault/fab_street_search/`。
- 注意：搜索结果里的 `isFree` 字段不可靠（例如免费的 City Street Props 标为 `false`，但 `startingPrice.price` 为 0），本报告以 `startingPrice.price == 0` 判定免费。
- 机器可读明细：`engine/Docs/street_scene_candidates.json`（含 uid、Personal/Professional 授权价、评分、UE 版本、标签、技术细节摘要）。
- 校验记录：`engine/Docs/StreetSceneValidation.md`（搜索覆盖、详情一致性、表格比对、链接、缩略图、manifest 阻塞）。
- 价格与授权：Fab 每个 listing 可有多档 license，本次按 Personal 记录；Professional 与 UEFN 等档位写入 JSON 的 `license_prices_cny`。不要把最低价直接当成 UE 商用授权价。
- 下载结果：Downtown West 已下载到 `~/Documents/EpicVault/DowntownWest/data/`。Fab 的 JSON manifest 用 `engine/Scripts/Fab/decode_json_manifest.py` 解码后，11,263/11,263 个 chunk 下载成功，1,252/1,252 个文件 SHA1 全部通过，`data` 约 11.81GB；`SHA256SUMS` 已生成。

### 项目内落地（2026-11-14 UTC）

- 资源包已复制到 `engine/Content/Downtown_West`（12GB、1,252 文件），源目录保留；`rsync --dry-run --checksum` 校验无差异。
- 为不改动原包引用，使用 UE Python `EditorAssetLibrary.duplicate_asset` 把 `Demo_Environment` 复制为 `/Game/Epicode/Scenes/DowntownWest`，文件 `engine/Content/Epicode/Scenes/DowntownWest.umap`（51,820,973 bytes）。复制后的地图仍引用 `Content/Downtown_West` 下的原资产，因此原包不能删除。
- 用 `UnrealEditor-Cmd ... /Game/Epicode/Scenes/DowntownWest -game -nullrhi ... -ExecCmds=quit` 加载验证：退出码 0，`LogLoad: Took 1.928296 seconds to LoadMap(/Game/Epicode/Scenes/DowntownWest)`，无地图加载错误。
- 通过控制 API 创建 Epicode 场景 `Downtown West`，`sceneId=01A1DAC5-004D-7F5D-B11E-A02B53A2EED2`，`levelPath=/Game/Epicode/Scenes/DowntownWest`；`scene.save` 写入 `engine/Saved/Epicode/Scenes/01A1DAC5-004D-7F5D-B11E-A02B53A2EED2/scene.json`。`scene.close` 后 `scene.open` 重开成功，`scene.runtime.get` 返回同一 `levelPath`。
- 渲染工作进程会加载场景的 `levelPath` 基础地图并叠加 Epicode 实体和相机。Downtown West 已完成 640x360 单帧渲染验证，输出可见街道、建筑、植被和天空；多机位和长序列渲染仍待验收。
- 场景默认相机已对齐地图内 `CineCameraActor`：位置 `(-71.846, 1130.894, 124.521)`，旋转 `(pitch=-6.200, yaw=-112.994, roll=0.000)`；Viewer runtime 的 `cameraIds` 和 `defaultCameraId` 均指向该相机。
- 说明：UE 地图资产在 `Content/Epicode/Scenes`，Epicode 场景文档按引擎设计存放在 `Saved/Epicode/Scenes`，不在 `Content`。两者通过 `levelPath` 关联。

### 视觉抽查

- 已核对 8 个候选的官方缩略图，确认画面是街景/街区而非纯道具或室内包；图片保存在 `~/Documents/EpicVault/fab_street_search/thumbnails/`。
- **London**：红色电话亭、`HEART OF LONDON` 咖啡店、Seven Dials 纪念柱，和“伦敦 7 Dials”描述一致。
- **Modern City Downtown**：美式砖楼、方尖碑、街边商铺，符合 downtown 定位。
- **City Streets**：外景街区与室内房间的拼贴，能看出内外景模块。
- **Amsterdam**：运河、砖桥、自行车、联排山墙，题材匹配。
- **Chinese Streets**：中文霓虹招牌、市场摊位、密集街巷，题材匹配。
- **Rainy Neon Streets**：湿漉漉的后巷、空调外机、霓虹灯，雨夜氛围匹配。
- **European City Streets**：古典立面、电车线、路牌，欧洲街景匹配。
- **Downtown West**：美式街角、店面、咖啡座、花盆和湿地面，免费包里少见的完整街区。
- 注意：搜索接口的 `thumbnails[].mediaUrl` 对个别条目会返回旧图/错图（例如 London 的搜索图是美式街区，详情接口才是 Seven Dials 街景）；London/Modern Downtown 以详情接口 `thumbnails` 为准，其余为搜索接口图。以上均为官方宣传图，不等于在 Epicode 项目中的实际渲染效果。

## 下一步

可以复用现有 Fab 下载链路：

1. 免费包：Downtown West 已下载并校验完成；City Street Props 的 JSON manifest 也可用 `decode_json_manifest.py` 解码，按同一流程下载。
2. 付费包：由你先在 Fab 完成登录和购买（支付环节不经过我），之后我接手下载、SHA1 校验、导入和 AssetRegistry 加载检查。
3. 若目标是“尽快拍”，优先 London；若目标是“零成本先搭一条街”，优先 Downtown West + City Street Props。
