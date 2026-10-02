**简体中文** · [English](h2h-agent-handoff.md)

# H2H Pocket agent 接手文档

## 仓库与范围

在 [hzpopjin/ai-passport](https://github.com/hzpopjin/ai-passport) 的 `feature/h2h-pocket` 分支继续开发。父级仓库为 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)，本地应用以其 `0b9e4c8` 提交为基线。上游 `main` 作为板级与演示基线保留。先读 [AGENTS.md](../../AGENTS.md)，按任务路由使用五个必需技能。[应用说明](h2h-pocket.zh_CN.md)继续作为按键、存档、音频和验收的权威文档。

产品需求包含账户／扫码绑定、网页或小程序选择外观，以及手机 NFC 轻触打开个人信息卡。目前主题包仅含固件内置资源的外观编号，不会重刷固件。固件更新机制、回滚和任意主题打包仍待开发。

## 实现位置

| 范围 | 文件与行为 |
| --- | --- |
| 应用 | `main/main.c`、`main/h2h/h2h_model.c`、`h2h_motion.c`、`h2h_ui.c`：按键、状态、进度、动画与重新设计的 LVGL 页面 |
| 音频 | `main/h2h/h2h_audio.c`、`assets/music/h2h/`：独立 Opus 播放、快进退、完整／副歌配对与目录 |
| 设备账户 | `main/h2h/h2h_ble.c`、`h2h_cloud.c`：认证蓝牙、NVS 中的 Wi-Fi 配置、TLS 注册、五分钟二维码与实体确认 |
| 主题 | `main/h2h/h2h_theme.c`、`tools/h2h/build_theme_catalog.py`、`integrations/randomdance-backend/official-themes/`：三套 16 字节官方包及 SHA-256 校验 |
| 本地预览 | `preview/h2h/`、`tools/h2h/preview.py`：共享 C 模型、画布和可选浏览器音频；账号页为视觉占位 |
| 网站 | `tools/h2h/web_server.py`、`passport_account_web.py`、`passport_ble.js`、`passport_card_web.py`、`package_web.py`、`deploy/`：访客预览、SSO 会话、设备控制与公开／私密 NFC 卡路由 |
| 后端 | [集成说明](../../integrations/randomdance-backend/README.zh_CN.md)和 `passport.patch`：增量 NestJS 模块、数据库迁移、设备认证、绑定、归属、主题及信息卡 API |
| 小程序 | [集成快照](../../integrations/randomdance-miniapp/README.zh_CN.md)：独立随机舞蹈小程序的六个 Passport 文件及定向路由／设置页补丁 |
| NFC | `tools/h2h/nfc_ndef.py`：为被动 NTAG213 标签生成仅含 URL 的 NDEF 镜像；实体标签需要外部写卡器写入 |

## 完整验证前恢复本地音乐

Git／源码包不包含个人 `.ogg`、`.bin` 录音、原始 MP3、`build/`、本地存档、固件与调试二进制、`sdkconfig`、托管依赖及凭证。音乐元数据、生成的 C 目录、图像、字体及字体许可证均已包含。两份字体与生成图像是必需资源，不是构建缓存。

要复现原目录，请向所有者取得匹配的 `track_000` 到 `track_013` 的 `.ogg`／`.bin` 资源，放入 `assets/music/h2h/`，按 `catalog.json` 校验哈希。顺序为七首完整歌曲，再接相同顺序的七段副歌。不要将录音上传到 GitHub。

也可通过打包器清单提供有授权的本地源文件，重新生成目录：

```sh
python3 tools/h2h/pack_music.py --manifest /path/to/songs.json --song-count 7
```

清单格式见[打包音乐](h2h-pocket.zh_CN.md#打包音乐)。重新生成可能改变时长与哈希，应审查 `catalog.json`、`tracks.c`、`music.cmake`。需要带 libopus 的 FFmpeg 和 ffprobe。不要擅自用测试音频替换所有者的录音。

未恢复音频时，`python3 tools/h2h/preview.py` 仍可进行视觉导航；音频请求失败、资源完整性测试失败，固件嵌入步骤无法完成。对不依赖录音的开发可使用纯模型／主题／音频 worker 测试及账户／信息卡／NFC 测试；恢复后再运行完整验证。

## 验证与当前验收状态

交接准备在含本地音频资源的工作目录运行 `./tools/validate.sh`。GitHub 发布说明记录所上传提交的结果。新 clone 必须先恢复音频才能复现完整验证。

```sh
source /path/to/esp-idf-v5.5.3/export.sh
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

分别报告 Build、Host tests、Device tests、Unverified。当前版本的实物验收仍待完成：音频稳定性、中文字形、BLE／Wi-Fi／TLS 并行内存、重连和错误路径、NVS 存档、账号归属确认、主题刷新及手机 NFC 轻触。刷写前取得批准；此次 GitHub 上传不包含设备刷写授权。

## 继续账户与 NFC 集成

1. 在当前后端目录审查并应用已准备的补丁，检查数据库结构、日志脱敏及注册限流。迁移或重启前遵守后端自身的发布规则。
2. 为 `https://ai-passport.randomdance.cn/sso/callback` 注册独立 SSO 客户端，私下配置 `PASSPORT_SSO_CLIENT_ID`、`PASSPORT_SSO_CLIENT_SECRET`，部署官方主题目录。凭证保留在本仓库之外。
3. 打包 Python 网站，验证 SSO、归属、错误与会话。按应用说明，现有托管预览是较早的单曲版本；本次交接不更新它，部署前检查线上状态。
4. 将小程序定向快照集成到独立客户端，在微信开发者工具编译，并用正式账号实测蓝牙权限、加密配对、Wi-Fi 配网、扫码认领与卡片实体确认。本次交接未上传或发布小程序。
5. 注册设备，使用返回的稳定 `card_url`，检查实体标签，再用外部写卡器写入 NDEF URL。验证公开／私密状态、所有者控制、头像昵称呈现与手机轻触。被动标签不保存 token 或资料。

公开契约使用 `https://apps.randomdance.cn/api/v1/passport` 和 `https://ai-passport.randomdance.cn`。本功能的后端端点与 SSO 部署仍待完成，网站布局检查通过不等于账号绑定成功。
