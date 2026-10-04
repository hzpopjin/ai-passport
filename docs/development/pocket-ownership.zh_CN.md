<p align="right"><strong>简体中文</strong> · <a href="pocket-ownership.md">English</a></p>

# Pocket Room 权属协议基础

本次新增不依赖 SDK、可由主机测试的 `h2h_ownership` 状态机。
**尚未连接** `main.c`、`h2h_cloud.c` 或 NVS，因此当前固件尚未实现新的首次激活门禁、
离线解绑、epoch 云备份与资源安装器。不得将本分支作为这些功能已完成的固件刷写。

跨仓权威合同：`random-dance-platform/docs/POCKET_PASSPORT.md`（2026-10-04）。
每设备一个主人，每账号可多设备，各自独立进度和备份；迁移需显式确认替换目标，
不自动合并，也不跨设备按最后写入覆盖。

纯状态机锁定未激活设备，已激活设备允许离线玩，本地解绑请求立即锁定。
待同步操作编号与 expected epoch 可通过状态序列化保留；匹配服务端收据才清除请求。
拒绝旧响应和同 epoch 换主人；epoch 变化后保持锁定，直至本地存档与资源重置持久化完成。

后续接入要求：

- NVS 中以版本化记录原子保存，校验损坏；读写失败保持锁定。不信任旧 `bound` 字节。
- 解绑保留逐机身份；界面立即锁定，慢速存储和联网交给 worker。
  仅 pending 记录持久化成功后，才报告解绑已接受。
- 同步先认证、提交 pending unbind，再判定权属，最后处理同 epoch 存档。
  重试不得把旧 expected epoch 改成新值。
- 持久化重置旧进度和资源选择后才解除 epoch 屏障。异步存档/资源按 epoch 与 generation 隔离。
- 现有 register/pairing_id HTTP 流程需适配 begin/claim/confirm 与新 sync 合同。
  生产逐机身份可信注入/注册流程仍待设计；自生成且未登记的凭据不能授权注册。
- 保持 BLE aa00/aa01/aa02、20 字节帧、操作码 1–5、SC/MITM/passkey。
  不向卡片下发手机 JWT 或共享长期密钥。
- 16 字节主题索引不是资源包。现有 factory 单槽无安全 OTA；资源与固件各自的 manifest、
  Flash/RAM 预算、非活动区校验、原子切换、断电恢复和回退仍待接入。本次不生成生产签名密钥。
- NTAG213 为被动固定随机 slug HTTPS URL，不能证明所有权，MCU 不重写；标签写入和锁定需真机。

验证：`tests/test_h2h_ownership.c` 在 C11、警告视为错误模式下通过。
完整静态门禁因缺少生成的音频（如 `assets/music/h2h/track_000.ogg`）失败；
仓库检查及此前主机测试通过。固件构建缺 ESP-IDF 5.5.3。未刷写设备。
