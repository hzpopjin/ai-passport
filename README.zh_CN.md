**简体中文** · [English](README.md)

# Pocket

一款运行在 FoloToy AI Passport 上的口袋伙伴：选择喜欢的团体和成员，布置像素小房间，听音乐，玩接接乐和记忆游戏，收集小卡，并展示应援牌。

**选择自己喜欢的团体和成员。** 团体名称用于标识可选内容包；成员图片、歌曲、衣装和房间主题属于所选内容，应用名称始终为 Pocket。可用选项以设备安装的固件和已获授权的资源集合为准。

当前应用位于 **`feature/pocket-paired-firmware`** 分支，`main` 保留上游硬件测试基线。本分支是**源码开发快照**，尚不是可直接刷写的公开版本：歌曲录音、提供的图片、参考 JS／WASM、记录的参考运行轨迹以及五组移植玩法模块，因再分发授权尚未明确而排除。全新克隆不能完成应用构建或完整主机测试；带本地材料的验证通过，也不等于公开源码可独立构建。

- **已有设备，想了解使用方法：**阅读 [Pocket 使用指南](docs/assets/pocket.zh_CN.md)，了解激活、按键、存档和故障处理。
- **想构建或参与开发：**阅读[开发指南](docs/assets/pocket-development.zh_CN.md)、[开源前检查](docs/contribution/open-source-review.zh_CN.md)和 [AGENTS.md](AGENTS.md)。
- **版权声明：**相关版权内容仅供个人学习、研究，禁止商用。请阅读[版权与素材声明](docs/assets/copyright-notice.zh_CN.md)。该声明不授予第三方内容的传播许可；[MIT](LICENSE) 覆盖的源码继续遵循原许可。

```sh
git clone --branch feature/pocket-paired-firmware https://github.com/hzpopjin/ai-passport.git
cd ai-passport
python3 tools/check_repo.py
```

仓库检查验证公开文档和仓库规范。固件开发需要 **ESP-IDF 5.5.3**、**ESP32-C3**、**8 MiB Flash**、**无 PSRAM**，以及开发指南所列、另行获得授权的本地输入。本仓库不发布固件镜像或私有素材包。

默认构建通过手机蓝牙完成激活与同步。账号登录令牌保留在手机，卡片不接收该令牌，也不直接访问后端。兼容的手机客户端及后端服务需要另行准备。外部外观包、头像存储和循环日志需要经过评审的候选分区；默认分区不提供这些容量。

[旧版 H2H／IAN 指南](docs/assets/h2h-pocket.zh_CN.md)及其[桌面预览](docs/assets/h2h-pocket.zh_CN.md#桌面预览)描述的是先前应用。预览可在没有音乐时查看旧界面，但不模拟当前 Pocket 固件或真实账号激活。

基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)。可复现问题请提交至 [GitHub Issues](https://github.com/hzpopjin/ai-passport/issues)；敏感问题遵循[安全政策](.github/SECURITY.zh_CN.md)。请勿附上登录令牌、完整激活二维码链接、Wi-Fi 密码、私钥或原始设备日志。
