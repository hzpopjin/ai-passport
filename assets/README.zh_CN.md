<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## H2H Pocket 素材

- `images/h2h/ian-atlas-source.png`：2026-09-29 使用内置图像工具生成的原创透明图集，五种姿态 × 三套服装。美术方向：IAN 风格的红棕色头发、蓝白水手服、柠檬针织衫、粉色舞台装；待机、挥手、比心、跳舞、庆祝姿态；奶油白／天蓝／柠檬黄／粉色。公开造型参考：[Hearts2Hearts 官方 IAN 帖文](https://x.com/Hearts2Hearts/status/2064905464455053601)。这是原创粉丝像素创作，不是官方肖像或官方背书；未打包参考照片。
- `images/h2h/ian_<outfit>_<pose>.png`：15 张透明 64 × 80 精灵帧；`sprites.c` 使用 RGB565A8 平面数据，每帧 15,360 字节，总计 230,400 字节。转换工具测量边界时忽略 alpha 小于 128 的透明杂点，裁切内部保留原始透明度。最近邻导出统一脚底位置，15 张人物均为 74 像素高，避免切换比心等动作时缩小。固件使用 1×／2× 整数缩放。房间背景、卡片布局与爱心由应用绘制，没有嵌入第三方背景画。
- `fonts/h2h/NotoSansSC.ttf`：来自 [Google Fonts Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc)，按附带的 `fonts/h2h/OFL.txt` 分发。`h2h_font_16.c` 为 16 像素、4 bpp 的界面与歌曲文案子集；`glyphs.txt` 是字符清单。预览字体位于 `preview/h2h/font.ttf`。通过 `tools/h2h/build_font.py` 重新生成，导出前检查字符覆盖。
- `music/h2h/track_000`–`track_013`：从用户的私有曲库读取七首 Hearts2Hearts 全曲和各自副歌。原 MP3 不修改，暂存于 Git 忽略的 `build/h2h-source-audio/`。转码参数为 16 kbps CBR Opus、单声道、48 kHz、20 ms 帧，解码输出为 16 位 PCM。`.ogg` 供本机预览，`.bin` 存放固定 40 字节的 Opus 帧并嵌入固件。14 个 Ogg 合计 3,280,459 字节，固件帧合计 3,156,760 字节。音乐文件被 Git 忽略；歌曲权利归权利人，未提供再分发许可。

使用与重新生成命令见 [H2H Pocket 说明](../docs/assets/h2h-pocket.zh_CN.md)。

操作栏图标使用 `images/h2h/icons.json` 中四个原创 16 × 16 像素图形，由 `tools/h2h/build_icons.py` 导出对应的 `icons.c` RGB565A8 资源（3,072 字节）。电脑预览读取同一份图形，不依赖 Emoji 或特殊符号字体。

## STAYC 原生素材

- `images/stayc/source/`：所有者提供的 Pocket v15 视觉资源，源提交为 `ff013044ebbeaf9c781b1a6c93bfc572b5cf4d89`。原 PNG／JSON 保留六名成员、每人两套服装、每套 32 姿态，共 384 姿态。这是既有粉丝项目图像，清单保留生成与验收来源；不声称官方背书或商业再分发许可。
- `images/stayc/stayc_visuals.bin`：1,853,859 字节的 RGB565+A8 显示像素有界 RLE。`sprites.c` 保留实测锚点、登记偏移与场景比例。内存中同时只保留一帧 15,360 字节解码数据。RGB888 转 RGB565 降低显示色彩精度，后续 RLE 无损。重建命令为 `python3 tools/h2h/pack_stayc_visuals.py assets/images/stayc/source --output assets/images/stayc`。
- `images/stayc/actions.c`：由提供的原 JS 适配的阶段表，以及三个房间与装饰几何。阶段／几何导出本身不等于完整实现 v15 生活、装饰和小卡反应状态契约。
- `music/stayc/track_000`–`track_011`：所有者传入的既有 Opus 资源，依次为六首真实 STAYC 全曲及对应六段副歌。Ogg 合计 2,778,319 字节，固件嵌入的原样 40 字节数据包合计 2,673,560 字节。提取保留数据包，不进行有损重编码。参数为 16 kbps CBR Opus、单声道、48 kHz、20 ms，解码为 16 位 PCM。曲名、时长与哈希见 `music/stayc/catalog.json`；录音保持 Git 忽略。权利归权利人，未提供再分发许可。

原生目标明确选择 STAYC 素材，原 H2H 目录独立保留。
[当前原生交接](../docs/assets/h2h-agent-handoff.zh_CN.md#stayc-原生继续开发)
记录实现边界与验证结果。

## 公开分发边界

相关版权内容仅供个人学习、研究，禁止商用，参见[版权与素材声明](../docs/assets/copyright-notice.zh_CN.md)。STAYC 提供的图片、生成图片资源、参考 JS／WASM、记录的参考轨迹及五组直接移植模块已排除出 Git，音乐继续排除；上文描述的是保留本地材料的清单，不是公开授权或下载内容。MIT 覆盖的源码和 OFL 字体继续遵循各自许可。
