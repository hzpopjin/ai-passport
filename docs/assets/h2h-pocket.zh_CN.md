[English](h2h-pocket.md) · **简体中文**

# H2H Pocket — IAN 的随舞像素小屋

为 AI Passport 制作的随身小伙伴：像素房间、七首 Hearts2Hearts 歌曲的全曲／副歌播放器、柠檬接接乐、八张收藏卡、飘动爱心应援牌，并可选择绑定随机舞蹈账号。音乐、爱心、收藏卡和设置仍保存在本机。账号页会启动蓝牙供手机配网，再通过 Wi-Fi 绑定账号和下载官方主题。随机舞蹈 iOS App 及其公开接口保持原样。

## 按键与收藏

| 页面 | 上 / 下 | 短按确定 | 长按确定 |
| --- | --- | --- | --- |
| 主页和菜单 | 上一项 / 下一项，循环选择 | 进入 | 返回 |
| 小房间 | 打招呼 / 比心 / 换装 / 换背景 | 互动或切换已解锁物品 | 主页 |
| 播放器 | 短按一／两次：每次音量 +5 / -5；短按三次：上一曲／下一曲；长按前进／后退 10 秒 | 播放 / 暂停；取消倒计时；故障后重试 | 音乐菜单；音乐继续，尚未结束的倒计时取消 |
| 音乐菜单 | 选择播放器、歌曲、全曲／副歌、循环模式或倒计时 | 应用选项 | 主页 |
| 小游戏 | 左移 / 右移 | 暂停 / 继续 | 退出，不结算奖励 |
| 收藏册 | 八张卡，然后是服装、背景 | 查看已拥有卡片或切换装扮 | 主页 |
| 卡片 | — | 设为应援牌 | 收藏册 |
| 结算 | — | 再玩一局 | 主页 |
| 应援牌 | — | — | 主页 |
| 设置 | 音量 / 倒计时 / 模式 / AI Passport | 循环更改或打开账号页 | 主页 |
| AI Passport | 确认 / 刷新 / 清除 Wi-Fi | 确认扫码账号、手动下载所选主题或清除配网 | 设置 |

## 账号、官方主题与 NFC

进入「设置 → AI PASSPORT」，在随机舞蹈小程序或支持 Web Bluetooth 的浏览器中连接 `RDP-<设备 ID 后八位>`，核对卡片与手机上的六位蓝牙配对码。小程序向卡片发送 2.4 GHz Wi-Fi 名称和密码；卡片将其存入 NVS，经 TLS 连接随机舞蹈 apps API。用小程序或网站扫描卡片显示的五分钟有效二维码，登录正式随机舞蹈账号，再按卡片确定键批准绑定。仅扫码认领不会完成绑定。账号页可以清除 Wi-Fi 配置。

网站和小程序仅提供晴空、柠檬、粉色三套官方主题。先在网站或小程序中选择，再通过蓝牙连接卡片并手动点「刷新」。卡片经 Wi-Fi 下载 16 字节主题包，核对 SHA-256 和格式后应用、保存服装与背景编号。这是外观选择，不是固件重刷或音乐更新。本机的游玩进度和设置不会上传。蓝牙、Wi-Fi、TLS 并行及实际屏幕效果仍需真机验证。

产品使用被动式 NTAG213 标签，本固件不能通过 ESP32-C3 写入它。先将注册设备返回的 `card_url` 设为 `CARD_URL`；`python3 tools/h2h/nfc_ndef.py "$CARD_URL" new-ntag213.bin` 可生成仅含 `https://ai-passport.randomdance.cn/card/<标识>` 的 NDEF 用户区镜像；写入实体标签需要另用 NFC 写卡器。网页实时读取 apps API 的公开状态：默认不公开，用户可在网站或小程序中自愿公开昵称、头像和已应用主题。标签不保存账号 token 或个人资料。现有标签内容及手机碰触效果需检查实物。

后端实现以[可审查补丁](../../integrations/randomdance-backend/passport.patch)和增量数据库迁移准备，尚未修改生产环境。网站还需为 `https://ai-passport.randomdance.cn/sso/callback` 注册独立 SSO 客户端，配置 `PASSPORT_SSO_CLIENT_ID`、`PASSPORT_SSO_CLIENT_SECRET`，并部署官方主题目录。

长按阈值为 500 毫秒，松手不会再触发短按。播放器中长按上键前进 10 秒，长按下键后退 10 秒；每次长按跳转一次，音量不变。同一方向连续短按三次、相邻两次间隔不超过 800 毫秒，可切上一曲（上键）或下一曲（下键），只在当前全曲／副歌模式内切换。只短按一／两次时，会在 800 毫秒窗口结束后按每次 5 点调整音量；换方向或按确定键会立即结算此前的音量调整。三连短按从新录音开头播放并保留播放／暂停状态，取消尚未完成的倒计时。暂停时跳转后仍保持暂停；跳转会取消倒计时，并限制在歌曲有效范围内。跳过和解码预热的时间不计入爱心奖励。新歌曲可选择先倒计时五秒；倒计时中暂停或返回会取消。暂停后从原位置继续，切歌或切换全曲／副歌会从对应录音开头开始；暂停时切换仍保持暂停。全曲／副歌偏好会保存。顺序与随机模式只在当前模式的七首录音内切换；单曲模式循环当前歌。一首歌时三种模式都能继续播放。

- 每累计成功写入音频输出的 120 秒 PCM，获得一颗爱心。暂停和倒计时不计入；这是软件统计，不是声学测量。
- 每局 45 秒有效游戏时间；接住每五件物品获得一颗爱心，每局最多五颗，仅完成整局后发放。暂停不计时，中途退出不奖励。
- 每五颗累计爱心随机解锁一张未拥有的卡，不消耗爱心。收藏第 2／4 张卡解锁服装，第 6／8 张解锁背景。集齐八张后仍可积累爱心。
- 三套服装：晴空水手服、柠檬针织衫、粉色舞台装。三个场景：晴空小屋、柠檬花园、心动舞台。收藏卡由原创像素姿态、服装、色彩及标题组合而成。
- 保存音量、全曲／副歌偏好、循环模式、倒计时偏好、听歌累计余量、爱心、卡片、装扮、背景和应援卡。重启后不恢复当前歌曲、播放位置和未完成的游戏。
- NVS 存档带版本号与校验。损坏记录使用默认值；存储错误不会自动擦除 NVS。事件在三秒内保存，听歌余量每 30 秒检查保存。突然断电可能丢失上一次保存后的进度。
- 除游戏和应援牌外，30 秒未操作后背光由 70% 降至 20%，没有进入深度睡眠。


小房间下方的四个内置像素图标，从左到右依次为挥手、爱心、衣服、风景，分别对应打招呼、比心、换装、换背景。方向箭头表示选择，实心圆表示确定键。人物区域不放装饰文字，仅在选择装扮或背景时于人物下方显示名称。

人物导出忽略透明杂点，15 张动作帧统一为 74 像素高、脚底对齐；比心与待机保持相同缩放。动画包含轻晃、间歇挥手、左右踏步、小跳和比心爱心，网页与固件共用动作逻辑。

## 电脑预览

在项目根目录运行：

```sh
python3 tools/h2h/preview.py
```

打开 <http://127.0.0.1:8877>。可点击屏幕旁的按键，或用方向键、Enter、Escape；声音按钮用于允许浏览器播放。预览直接运行固件的 C 状态逻辑，在 240 × 320 画布上使用同一套素材，并由浏览器解码 Opus。它不模拟 LVGL、ESP32 调度或扬声器。需要 C 编译器与 Python 3.9+。预览进度单独存放于 `build/h2h-preview-save.bin`。

## 打包歌曲

需要带 libopus 的 FFmpeg 和 ffprobe；输入原文件只读，不修改。

```sh
python3 tools/h2h/pack_music.py --input /path/to/song.mp3 --title 'Lemon Tang' --artist Hearts2Hearts
```

多首歌使用 `--manifest /path/to/songs.json`，文件内容：

```json
[
  {"file": "song.mp3", "title": "Lemon Tang", "artist": "Hearts2Hearts"}
]
```

相对歌曲路径以清单所在目录为基准。全曲／副歌成对打包时，先列全部全曲，再按相同顺序列对应副歌，并添加 `--song-count 7`。输出到 `assets/music/h2h/`：预览使用的 Ogg、固件使用的固定长度 Opus 原始帧、C 歌曲目录、CMake 嵌入列表和容量／哈希报告。参数固定为 **16 kbps CBR、单声道、48 kHz、20 ms 帧的 Opus**。缺失文件、无效元数据、全曲与副歌未配对、转码或解码失败、帧长不是 40 字节、音乐总量超过 5 MiB 都会报错。支持 1–99 段录音。歌曲随下一版固件更新，设备没有文件上传入口。

这 14 段录音来自用户的私有曲库：Lemon Tang、FOCUS、ICONIC HEART、Pretty Please、RUDE!、STYLE、The Chase 的全曲和各自副歌。原始 MP3 不修改，只暂存于 Git 忽略的 `build/h2h-source-audio/`。14 个 Ogg 合计 3,280,459 字节；实际嵌入固件的原始帧合计 3,156,760 字节。音频资源被 Git 忽略，不属于仓库 MIT 许可的内容。

歌曲标题或界面文字变化后，先重新生成字体，再构建固件：

```sh
python3 -m venv build/asset-env
build/asset-env/bin/pip install Pillow==11.3.0 fonttools==4.60.0
npm install --prefix build/font-tools lv_font_conv@1.5.3
build/asset-env/bin/python tools/h2h/build_font.py
```

Noto Sans SC 原字体、OFL 许可及生成的子集位于 `assets/fonts/h2h/`。可用 `build/asset-env/bin/python tools/h2h/convert_sprites.py` 重新导出精灵。素材来源见[资源说明](../../assets/README.zh_CN.md#h2h-pocket-素材)。

## 构建与交付

使用 ESP-IDF **5.5.3**，目标 ESP32-C3，8 MiB Flash，沿用默认 NVS／PHY／factory 分区。激活本机 ESP-IDF 环境后运行：

```sh
./tools/validate.sh
```

完整检查包含仓库规范、原有主机测试、H2H 状态逻辑／音频任务／资源测试、隔离固件构建、合并镜像校验和归档。容量检查要求应用分区至少剩余 512 KiB，合并文件不超过 8 MiB。`esp_audio_codec` 固定为 2.6.2，LVGL 固定为 9.5.0。独立音频任务从 Flash 连续读取每个 40 字节的 Opus 帧，不再需要跳转索引，并输出到有界 PCM 缓冲。跳转先解码并丢弃 500 毫秒历史数据，超过 [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.html#section-4.6) 的下限，且已用全部 14 段录音对照验证；首尾裁切保持正确，切换页面不销毁任务。中文组件统一使用生成字库；新标题包含字体不支持的字符时，字体生成会报错。

交付文件为 `build/FoloToy-AI-Passport-full.bin`，获得用户授权后从 **0x0** 刷入。`build/firmware/<SHA-256>/` 归档保留匹配的 ELF／MAP 和清单，可用 `python3 tools/archive_firmware.py verify <archive-directory>` 校验。合并刷写可能重置原有数据，不代表允许全片擦除。详见[固件数据策略](../development/engineering/firmware-layout.zh_CN.md)。

代码入口：`main/h2h/h2h_model.c` 管理行为与存档校验，`h2h_audio.c` 管理解码，`h2h_ui.c` 管理 LVGL 页面，`main/main.c` 连接 BSP 按键、NVS、电量及播放器事件。工具位于 `tools/h2h/`。不录制麦克风数据。

## 验收状态

实际构建、主机测试结果、文件哈希及预览检查记录在本地生成的 `build/h2h-validation.json`。音频主机测试通过模拟解码器／调度器来验证真实音频任务的首尾裁切、暂停继续、快进快退、暂停跳转、预热时间不计奖励、重新选曲与故障处理；不证明 ESP32 上的 Opus 性能。实际完整 Ogg 文件另经 FFmpeg 解码检查。

可选的电脑跳转验证：安装本地 libopus 动态库后，运行 `python3 tools/h2h/verify_seek.py`。脚本将每段完整解码和五个跳转窗口与 FFmpeg 的 libopus 解码结果对照，覆盖首尾裁切和解码器状态收敛；不测量 ESP32 性能。

**本版尚未进行真机测试。** 上一版的刷机与启动记录不代表本版验收。授权刷机后验收：

1. 全部文案与页面，三套服装 × 三个背景，打招呼／比心／跳舞及应援爱心动画；检查物理圆角不会遮挡内容。
2. 至少连续播放 30 分钟，同时切换页面、玩游戏、改音量、暂停继续。检查音质、断音、响应、重启与剩余堆内存日志。
3. 取消倒计时、播放和暂停时前后跳转 10 秒（含首尾边界）、短按三次切歌且不改变音量、短按一／两次调整音量、单首歌曲随机／循环、游戏暂停，以及每局奖励只发放一次。
4. 获得爱心和卡片、装备物品，等待至少三秒后断电重启检查恢复；听歌进度检查点最多需等 30 秒。核验实际背光和电量显示。

构建与电脑预览不代表设备音质、续航或温度验收通过。

## 服务器 Python 网站

生产网址为 `https://ai-passport.randomdance.cn`，服务器目录为
`/www/wwwroot/ai-passport.randomdance.cn`。现有网站仍提供上一版 32 kbps 单曲内容；
本次固件曲库修改尚未发布到网站。Nginx 只提供白名单打包后的 `public/`
目录，API 转发到 `127.0.0.1:8878` 的 Gunicorn。`h2h-pocket-web.service`
由 systemd 管理，开机启动、故障后重启。
必须使用 **一个 worker、八个线程**：C 桥接访问受锁保护，每位访客拥有独立的模型快照。
随机访客 cookie 使用 HttpOnly、Secure、SameSite=Lax；SQLite 存档位于公开目录外。
存档约三秒检查一次，突然停止可能丢失最近进度。更换浏览器或清除 cookie 会进入新小屋。
访客 cookie 与闲置存档保留 30 天。网站与实体硬件、本机预览分别存档，没有账号同步。

用 `python3 tools/h2h/package_web.py build/web-release-N` 生成新发布包。
默认排除音频；获得公开上传授权后才添加 `--include-audio`。
此前用户授权的是网站公开上传完整的 Lemon Tang 测试歌曲，不包含此次新增的 14 段曲库。
Linux 服务器上重新编译桥接：

```sh
mkdir -p build
cc -O2 -std=c11 -Wall -Wextra -Werror -shared -fPIC -Imain/h2h main/h2h/h2h_model.c main/h2h/h2h_motion.c tools/h2h/web_bridge.c -o build/h2h-web.so
```

配置源文件在 `tools/h2h/deploy/`。版本放在 `releases/`，`current` 指向当前版本；
Python 环境与存档留在 `shared/`。更新时切换 `current` 并仅重启
`h2h-pocket-web.service`；回滚时恢复上一版本链接并重启同一服务，不能删除 `shared/data`。
不上传整个仓库，不把本地开发用的 HTTP 服务直接作为公网服务。
实际安装的 Gunicorn 版本记录在发布目录的 `requirements.lock`。
服务器验证请求大小、参数范围、同源写入、访客隔离及损坏存档。
网页在已有请求尚未完成时跳过后台轮询，避免慢网络不断堆积请求。

运维检查：`systemctl status h2h-pocket-web`、`journalctl -u h2h-pocket-web`，
以及 `curl https://ai-passport.randomdance.cn/healthz`。
部署验证记录在本地 `build/h2h-web-deployment.json`。网站部署不会刷写实体设备。
参考 [Gunicorn 配置](https://gunicorn.org/reference/settings/) 和
[Nginx 静态资源与反向代理](https://nginx.org/en/docs/beginners_guide.html)。
