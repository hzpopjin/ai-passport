<p align="right"><strong>简体中文</strong> · <a href="pocket-ownership.md">English</a></p>

# Pocket Room 权属与云同步接入

产品应用已连接 `main.c`、`h2h_cloud.c`、纯 `h2h_ownership`/`h2h_pocket` 引擎及 NVS。
这是经过主机测试的源码，不是已发布或真机验证的固件。跨仓权威合同为
`random-dance-platform/docs/POCKET_PASSPORT.md`（2026-10-04，后端 3.8.1）。
每设备一个主人，每账号可多设备，各自独立进度；迁移需显式确认替换目标，不合并。

实际主循环在按键和音频之前启用激活门禁。未配对不能离开账号页玩游戏。
门禁覆盖所有按键事件、倒计时/游戏计时、听歌奖励、跳转、音频结束与播放请求。
已激活设备允许离线玩。播放和本地保存同时校验 ownership epoch 与 model generation，
排队任务不能使用上一位主人的模型。

## 持久化与解绑

`h2h_cloud/pocket_v2` 是单个 384 字节、版本 2、固定小端 NVS 记录，带损坏校验。
记录包含权属、待同步解绑 ID/expected epoch、48 字节 schema-1 存档、revision、
冲突/恢复确认状态及一个本地 recovery 快照。该校验不是密码学防篡改。
权属变化与旧周期存档重置使用同一 commit；无变化的 sync 不重复写 NVS。
损坏、截断或无法读取的记录保持锁定。旧 bound 字节和无账号存档不删除、不自动导入，
也不能解锁。逐机身份单独保存，解绑仍保留。

Unbind 和冲突 Restore 需五秒内两次物理点击，移动选择会取消确认。
按键回调仅入队；应用主任务提交本地解绑后才报告已接受，网络 worker 再提交原 ID/epoch。
commit 成功后立即锁定，重启仍锁定直至同步协调。commit 失败会锁定当前运行并报错；
若旧记录仍在 Flash，重启可能重新加载旧激活状态，未持久化成功不能称为跨重启解绑成功。
真实 NVS 掉电行为需板级验证。远程解绑在下次成功同步时锁定。

## HTTP 与存档同步

移除匿名 register 和旧 pairing_id HTTP 流程。请求保留 TLS 证书验证，使用逐机认证头，
不使用手机 JWT 或共享密钥。begin 返回五分钟二维码，手机登录 claim 仅选定领取者，
卡片物理确认捕获当次 code/account/epoch；sync 与原子 NVS 重置成功后才解锁。
现有身份生成器不能自行登记：生产可信登记/身份注入仍是独立前置条件。

联网时每十五秒和手动 Sync 同步。先处理待同步解绑，再判定权属和存档。
过期解绑 ID 明确返回 rejected，不改 expected epoch，也不写当前主人存档。
epoch 变化先清除旧进度/recovery，再接受当前周期的云快照；本地记录丢失可恢复该快照，
不先上传默认值。拒绝陈旧/小数 epoch、错误 schema/长度/hash 和非法存档。

脏存档以 revision CAS 上传。提交成功但响应丢失可幂等重试；迟到确认不覆盖新本地输入。
普通冲突暂停游戏并保留两个快照，用户明确 Restore 才替换。
手机确认迁移后先持久化收到的快照，再发送 revision/hash ACK；ACK 被接受前不上传新存档。
目标设备离线进度保留在一个 recovery 槽。尚无该快照导出/恢复界面或版本历史；
再次恢复可替换该槽，新主人周期会清除它。

BLE aa00/aa01/aa02、20 字节帧、操作码 1–5 和 SC/MITM/passkey 无变化。
操作码 5 请求 sync，不安装资源包。账号页还提供 Forget Wi-Fi、Unbind、Restore。
二维码/PIN/按钮布局需真实 240×320 屏幕和相机识别测试。

## 资源、NFC 与验证边界

16 字节主题索引不是资源包，factory 单槽无安全 OTA。资源与固件各自的 manifest、
实测 Flash/RAM 预算、非活动区校验、原子切换、断电恢复与回退仍待实现。
本次未修改分区或生产签名信任根。NTAG213 保存固定随机 slug HTTPS URL，不证明权属、
不带 secret，MCU 不能重写。服务端公开字段需主动开启；域名路由与标签写入/锁定待验。

ownership、activation、pocket 持久化主机测试在 C11、警告视为错误模式下通过。
`tests/build_h2h_cloud_host.py` 使用测试专用 SDK/NVS/HTTP 适配器、固定版本 cJSON 与真实主机
加密库编译实际 main/cloud C。执行生成的二进制及 `--activated-main`；后端
`test:passport-firmware` 覆盖六项 C 到 HTTP/JWT/NFC 场景。
适配器不证明实际 SDK API、任务并发、射频、TLS 或 Flash 硬件行为。

Build: NOT RUN — 缺少 ESP-IDF 5.5.3。
Host tests: 相关套件 PASS；完整 static gate 因缺少现有 Opus 资产
（如 `assets/music/h2h/track_000.ogg`）FAIL。
Device tests: NOT RUN — 用户明确禁止刷写。
Unverified: 完整 SDK 构建/容量、NVS 掉电、BLE 安全配对/权限、网络故障、显示、音频、
NFC 及资源/OTA 安装。
