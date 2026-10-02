**简体中文** · [English](README.md)

# H2H Pocket — IAN 版

一个 ESP32-C3 AI 通行证应用，包含像素小屋、音乐播放、接柠檬游戏、收藏卡和粉丝徽章。可选的随机舞蹈账号绑定提供蓝牙配网、二维码认领与卡片确认、官方外观包，以及 NFC 个人信息网页。

本仓库是 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的 fork。应用开发位于 `feature/h2h-pocket`，`main` 保留上游基线。此次源码交接是开发快照，实体设备与账号联调验收仍待完成。

请先阅读 [AGENTS.md](AGENTS.md)、[应用说明](docs/assets/h2h-pocket.zh_CN.md)和 [agent 接手文档](docs/assets/h2h-agent-handoff.zh_CN.md)。接手文档说明实现位置、外部客户端集成、验证要求与剩余工作。

```sh
git clone --branch feature/h2h-pocket https://github.com/hzpopjin/ai-passport.git
cd ai-passport
python3 tools/h2h/preview.py
```

打开 <http://127.0.0.1:8877>，使用页面按钮或方向键、回车键操作，Escape 返回。本地预览支持导航与布局检查；账户画面不模拟真实 SSO、蓝牙或 NFC。

个人音乐录音未纳入 Git 和源码包。没有这些资源仍可运行视觉预览；播放、资源完整性测试和固件构建需要匹配的本地音频。按[接手文档](docs/assets/h2h-agent-handoff.zh_CN.md#完整验证前恢复本地音乐)恢复或重新生成。构建使用 ESP-IDF **5.5.3**，目标为 **8 MB Flash、无 PSRAM 的 ESP32-C3**。构建通过不代表设备行为已验收。

账号与后端变更已准备供审查，本快照不部署它们或发布小程序。三套官方外观包选择固件内已有的服装与背景；固件重刷和任意主题上传仍需后续开发。
