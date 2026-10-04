<p align="right"><a href="pocket-resources.md">English</a> · <strong>简体中文</strong></p>

# Pocket 视觉资源与 Flash 预算

本实现安装真实图像资源，区别于旧的 16 字节主题索引。资源替换十五张
64×80 RGB565A8 人物姿势、四张 16×16 图标、三种房间颜色，以及现有字体
支持的可打印 ASCII 成员/团体/标题文字。歌曲、玩法、字体和内置回退保持原有。
格式 2 支持 `member_visual`、`group_visual`，两者均不包含音乐或固件更新。
其他成员/团体须提供真实且获准使用的原始画稿。当前只有仓库内 IAN 图像，
没有伪造官方资源目录或生产素材。

## 预算与可选开发分区

`python3 tools/h2h/resource_budget.py` 统计真实图像/字体数组和音频目录。
目录中的十四个音频二进制缺失，3,411,595 字节仅为嵌入数据下限，并非 app 实测大小。

| 项目 | 字节 |
| --- | ---: |
| ESP32-C3 Flash，无 PSRAM | 8,388,608 |
| 默认 factory app，资源容量为零 | 8,323,072 |
| 音频目录，七首完整版及七段副歌 | 3,156,760 |
| 十五张人物图；四张图标；字体 bitmap | 230,400；3,072；21,363 |
| 单视觉包，含 512 字节头 | 233,984 |
| 含 4,096 字节元数据扇区的最小槽 | 238,080 |
| 开发 A/B 槽，每槽 | 262,144 |
| 每槽剩余空间 | 24,064 |
| 开发 factory app | 7,798,784 |
| 保留 512 KiB 余量后的实际 app 上限 | 7,274,496 |

默认 `partitions.csv` 与 `sdkconfig.defaults` 未改。可选的
`config/partitions-pocket-visual.csv` 保留 0x9000 NVS、0xf000 PHY，factory
仍从 0x10000 开始，pocket_a/pocket_b 为 0x780000/0x7c0000。
独立干净构建按顺序使用 defaults：
`sdkconfig.defaults;config/sdkconfig-pocket-visual.defaults`。随后执行
`python3 tools/h2h/resource_budget.py --app <实际-app.bin>`，超预算即失败。
未获刷写授权。旧 factory 镜像可能覆盖新资源槽，改变分区须另行审核安装方案。

保留嵌入回退并新增完整音频+视觉双槽至少需要 10,266,811 字节，超过 Flash
至少 1,878,203 字节，尚未计入应用代码、对齐和运行元数据。完整音乐切换需要
获准缩小的真实素材，或移除嵌入音频、可信预装外置回退并实测 app/RAM，或更大硬件；
本阶段没有交付。固件 A/B OTA 还需要 otadata、两个实测可执行槽、启动自检和回退。
默认 factory 与本视觉分区均不启用固件 OTA，60 MB 网页包不适用于此硬件。

## 签名格式与状态切换

后端合同为 `random-dance-platform/docs/POCKET_PASSPORT.md`，版本 3.9.0。
签名覆盖以下 UTF-8 JSON 元组：

```text
["pocket-visual-v2",kind,"esp32c3-pocket-8m",2,resource_id,version,
 min_firmware,max_firmware,233984,15360,sha256,key_id]
```

Ed25519 验证所有字段与包内容 SHA-256。ID 为 1–32 位小写字母、数字、下划线或连字符，
版本为正 uint32。文字限定现有字体支持的可打印 ASCII。二进制采用固定小端布局和
规范零填充，使用前验证尺寸、长度、偏移、保留字节及颜色范围。未知公钥、型号、格式、
不兼容固件、超预算、修改签名/内容、网络版本降级均拒绝。每资源版本高水位表最多
十六个 ID，并跨换绑保留；同版本重装必须同 hash，第十七个资源族在擦除前拒绝。

`h2h_resource_runtime` 只接受上述精确可选布局，默认分区上报资源容量零。
`h2h_resource_key_at` 默认不信任任何公钥，生产公钥注入仍须审核。
后端目录只读，未明确配置时为空；没有新增生产签名密钥、登记或上传入口。

网络 worker 使用逐机认证下载 1 KiB Range 到非活动槽，验证精确 Content-Range、长度及
权属 generation。中断后在后续同步从零重试，每次同步至多尝试一次；不启用部分下载。
完成 SHA、签名及包头验证后，最后写入槽元数据。服务端 `verified` ACK 只批准当前
选择 revision/epoch，并不代表已显示。

渲染器映射已验证 Flash，在 LVGL/资源锁下原子提交 1,536 字节
`h2h_resources/visual_v2` NVS 活动/前一槽记录，然后重建图像。旧映射在旧屏幕删除后
才释放。下载不得擦写活动或仍被映射的槽，不把完整包复制到堆；最大单图 15,360 字节。
可选 defaults 将主任务栈设为 12 KiB，配置更小则禁用安装；worker 栈由 8 增至 16 KiB，engine 副本、hash 缓冲、TLS、LVGL 和映射仍须板上实测
峰值 RAM 与耗时。

commit 或映射失败保留旧显示、上报失败，等待手动 Sync/retry。持久化和渲染调用完成
后才 ACK `installed`，仍不等于仪器证明屏幕正确显示。启动验证活动槽，必要时回退至
已验证前一槽或内置图像，不降低网络版本高水位。NVS 损坏会关闭资源安装，不擦除/重置。
换绑后隐藏旧主人的图像，排队任务不得跨权属 generation。已激活离线继续使用最后
有效的本地外观，至下次认证同步协调。

## 复现与边界

`python3 tools/h2h/build_visual_resource.py /tmp/pocket-visual` 从仓库 IAN 数组生成
**未签名**包，不是可信发布包。后端资源测试仅用临时内存夹具密钥签这些真实字节。
`tests/test_h2h_resource.c` 覆盖签名、容量、CAS、回退，以及全部 230 次数据/元数据
写入中断和半写后重启。实际 main/cloud C 还通过测试 SDK/NVS/HTTP 适配器接入真实
Nest 路由，覆盖完整性、换绑、映射/存储失败与重试。

Build: NOT RUN — 缺少 ESP-IDF 5.5.3 与原始 Opus 文件。
Host tests: 资源测试 PASS；完整 static gate 在缺失原始音频处失败。
这些测试不能替代 ESP-IDF 或 Flash 硬件验证。
Device tests: NOT RUN — 明确禁止刷写。
Unverified: app 实际大小/余量、峰值 RAM、任务/Flash 并发、mmap 与 LVGL 生命周期、
长文字布局、无线/TLS/BLE、真实掉电、生产素材/信任根、完整音乐切换及固件 OTA。

## 第三方密码库来源

`components/pocket_crypto` 包含未修改的 Monocypher **4.0.2** core 与可选 Ed25519
源码，来自[上游版本](https://github.com/LoupVaillant/Monocypher/tree/4.0.2)。
保留原始 BSD-2-Clause/CC0 双许可 `LICENCE.md`，只增加项目 CMake 接入。
上游 Markdown 在 `tools/check_repo.py` 中明确登记；项目文档与所有敏感内容/冲突
检查继续生效。API 参考：[Monocypher Ed25519](https://monocypher.org/manual/ed25519)、
[ESP-IDF 5.5.3 partition API](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/partition.html)。

| 上游文件 | SHA-256 |
| --- | --- |
| monocypher.c | 02174117935699d418443c75a558a287deb06ef8cf7c1adced61d9047d2f323d |
| monocypher.h | fcaf6ed771358bb4f40fba016f6518ae86ec02b1b877d2cc35ad92d3a26fd7b3 |
| monocypher-ed25519.c | 97d581639dfa72be08a6d57deb7d79b736be001cb416819cab196d22559d242b |
| monocypher-ed25519.h | 3a3035181f991a158d0e1c7567258f0bae8ba0f1f23c5512b4a1db1b3c9730ce |
| LICENCE.md | a5781770269d2516e52ba4863f790c10a16da4089a1e81823aee19ff1e9026b0 |
