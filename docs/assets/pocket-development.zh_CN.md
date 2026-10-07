**简体中文** · [English](pocket-development.md)

# Pocket 开发指南

Pocket 是应用名称，团体／成员是用户可选内容。当前源树的本地构建材料使用一个具体团体集合；下表保留其真实文件路径，用于验证与授权核对。文件标识不是项目名称。当前卡片实现不证明已支持任意内容包的运行时切换。当前应用分支为 `feature/pocket-paired-firmware`，`main` 是上游板级测试基线。先阅读 [AGENTS.md](../../AGENTS.md)、[使用指南](pocket.zh_CN.md)、[版权声明](copyright-notice.zh_CN.md)和[开源前检查](../contribution/open-source-review.zh_CN.md)。历史 H2H 文档描述的是其他应用及通信流程。

## 公开源码与本地前置材料

公开分支排除了再分发许可尚未明确的材料。另行获得授权的输入应保留在原本地路径：

| 仅本地输入 | 必要用途 |
| --- | --- |
| `assets/images/stayc/` | 原始图片图集／参考模块、生成的 `actions.c`、`sprites.c`、目录及 `stayc_visuals.bin` |
| `tools/h2h/reference/v15/` | 真实参考 WASM、JS 模块和完整性测试材料 |
| `tests/stayc_v15_*.json` | 记录的玩法比较轨迹 |
| `main/h2h/stayc_game.{c,h}`、`stayc_game_motion.{c,h}`、`stayc_game_life.{c,h}`、`stayc_game_decor.{c,h}`、`stayc_game_reaction.{c,h}` | 移植的原始玩法／场景实现及接口 |
| `assets/music/stayc/track_000`–`track_011`，`.ogg` 和 `.bin` | 与提交目录哈希一致的六组完整版／副歌 |

这些路径已被 Git 忽略，不代表提供下载或授权素材包。未经许可不得获取或重构版权输入，不得用 `git add -f` 提交，也不得发布嵌入这些材料的固件。当前源码无法仅从 GitHub 独立复现。完整检查在缺失输入时明确失败，不会静默跳过覆盖。公开仓库与文档检查仍可运行 `python3 tools/check_repo.py`。

对于获得授权的本地音乐库，`tools/h2h/pack_music.py` 支持 `--manifest`、`--song-count 6` 及 `--output assets/music/stayc`。先列六首完整版，再按同一顺序列六段对应副歌，配对标题相同，艺人为 `STAYC`。实验时使用单独的被忽略暂存目录；现有完整性测试要求原目录精确一致，因此换录音需要明确更新目录、测试和字体。打包需要带 libopus 的 FFmpeg／ffprobe。使用 `--reindex --output assets/music/stayc` 可对已有授权 Ogg 输入直接提取包，不重新编码。任何重新生成前都应先验证现有文件。

## 环境与验证

使用 ESP-IDF **5.5.3**、ESP32-C3、8 MiB Flash、无 PSRAM。遵循[环境设置](../development/engineering/environment-setup.zh_CN.md)和[构建测试](../development/engineering/build-and-test.zh_CN.md)，保留固定的 Managed Components 及 `dependencies.lock`。主机测试还需要 Python、Node.js、C 编译器、验证过的 actionlint 和 OpenSSL 开发头文件／库。`H2H_CJSON_DIR` 可指向经过验证的 cJSON 1.7.19；否则主机辅助脚本从上游填充 `build/host-dependencies/cjson-v1.7.19` 并验证 SHA-256。

```sh
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version
python3 tools/check_repo.py
./tools/validate.sh --static
./tools/validate.sh --all --segmented
```

macOS 已安装 Homebrew OpenSSL 时，可使用仅针对当前命令的 `CPATH="$(brew --prefix openssl@3)/include"`、`LIBRARY_PATH="$(brew --prefix openssl@3)/lib"` 提供头文件／库，无需替换用户的 shell 配置。分段检查从独立的受控默认配置构建，在被忽略的 `build/firmware-segmented/<app-sha256>/` 保存 app、bootloader、分区表和匹配 ELF／MAP。用 `python3 tools/archive_segmented_firmware.py verify <archive-directory>` 核验；该命令不会刷写设备。

## 当前固件契约

正常构建通过 `h2h_phone_bridge.c` 使用 `h2h_phone`。账号 JWT 及后端请求留在手机。只有显式选择旧版主机测试配置时才使用 `h2h_cloud_legacy.c`，它不是默认卡片通信方式。参见[手机协议](../development/pocket-phone-jwt.zh_CN.md)及[原生存储契约](pocket-native-port-contract.zh_CN.md)。

默认分区保持 NVS `0x9000/0x6000`、PHY `0xF000/0x1000`、factory `0x10000/0x7F0000`。资源／资料／日志候选分区表用于评审，默认不开启。应用分区剩余空间不等于资源可用容量。资料和诊断的数据影响分别见[资料文档](../development/engineering/pocket-account-profile.zh_CN.md)及[诊断文档](../development/engineering/pocket-public-logs.zh_CN.md)。

非 LVGL 任务的绘制必须持有 BSP LVGL 锁；按钮回调保持非阻塞，音频、协议和持久化在所属工作任务执行。无效／未来版本记录受保护，恢复需明确操作，归属 epoch 隔离存档，购买只有持久化成功后才生效。开发预览是临时沙盒，不是激活或账号权益。

## 实卡验收与交付

本地构建和主机模拟不能证明真实卡片／手机／账号互通。需要测试启动、中文字形、按键时序、全部游戏／场景分支、实际声音及快进退、蓝牙重连／配对码、激活、完整存档冲突／恢复、归属切换、断电及运行时 heap／栈。旧浏览器预览针对 H2H，不是当前 Pocket 应用。

任何刷写前，先识别准确设备与已验证镜像，核对现有布局，说明数据影响并获得批准。现有最小布局兼容时，经评审的 app 单段 `0x10000` 写入可保留 NVS／PHY；不得换用未验证镜像或填充的 `0x0` 镜像。遵循[刷写政策](../development/engineering/firmware-layout.zh_CN.md)。读取备份原固件不是前置条件，构建命令也不授权全片擦除。

交付分别报告 Build、Host tests、Device tests、Unverified。被排除的材料获得许可或被替换、且全新克隆通过完整检查前，公开分发仍存在阻塞。服务发布、候选布局启用及固件分发是独立操作。
