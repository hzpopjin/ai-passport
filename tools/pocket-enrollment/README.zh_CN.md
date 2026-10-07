<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 受信任的 Pocket relay 登记

本工具补齐 ESP32-C3 / 8 MiB 卡片的单键登记缺口，包含临时无界面固件、
用户本地安全终端客户端和服务器本地 CLI。不修改正常 R6 应用，也不新增 HTTP
登记接口。Mac 与 SSH 服务器的操作员属于信任边界；回执证明与传入的新密钥一致，
不构成硬件身份认证。

## 精确数据影响

临时固件读取既有公开 `h2h_cloud/id`、出厂 MAC，以及 `relay_key` 的长度/存在性。
不读取旧 `secret` 或旧 relay 密钥。公开 ID 缺失/格式错误、已有 relay 密钥、NVS
初始化失败、ADC 样本过期、目标不符、按键跳变遗漏或操作超时均拒绝。
用户必须先松开全部按键，再长按 OK 至少三秒，并在六十秒内松开。

唯一显式 NVS 写入是缺失的 32 字节 `h2h_cloud/relay_key`，随后提交。
没有擦除 API、分区导出、密钥覆盖、资源分区、eFuse 或安全设置操作。
临时固件关闭背光，避免残留的旧二维码看似仍有效。
正常 NVS 初始化可能执行 SDK 恢复；存储损坏或断电时不能保证逐字节保留。

服务器仅向缺失的 `passport_relay_keys` 写入一行，用途为
`independent-enrolled-relay-key`、修订 1、`revoked=false`。
既有设备 state、所有者、epoch 与存档不被读取或改写；提交时所有者必须仍与审核值
一致。未知既有登记拒绝；服务器单独重试仅在 SQL 内比较本操作传入的新密钥，不查询
旧密钥字节。查询日志或未审核触发器会阻止操作，不修改数据库设置。
管理员在操作期间改变日志配置仍超出工具控制。
常规受保护数据库存储、二进制日志和备份可能保存新密钥，须纳入批准范围。

若公开设备记录缺失，单独明确批准 `allow_create_relay_only` 后可创建未绑定记录，
不含所有者、存档或配对，`credentialHash=null`。既有 R6 HTTPS 认证拒绝非字符串
校验值。这不会登记旧 HTTPS token，也不会修复该认证路径。
relay 登记与账号绑定、激活是不同步骤。

## 审核与合成验证

使用已安装的 ESP-IDF 5.5.3 Python（含 cryptography 与 pyserial）、CMake/Ninja、
Node.js、宿主编译器与 OpenSSL 开发库。以下命令不安装工具或改变系统配置。
在本目录执行：

```sh
python -B validate.py --idf "$IDF_PATH" --openssl /path/to/openssl
IDF_COMPONENT_MANAGER=0 idf.py -C device -B /absolute/ignored/build/enrollment \
  -DPOCKET_BOARD_REPO=/absolute/ai-passport \
  -DSDKCONFIG=/absolute/ignored/build/enrollment/sdkconfig build
python -B archive_helper.py create /absolute/ignored/build/enrollment \
  --board-repo /absolute/ai-passport \
  --archive-root /absolute/ignored/build/enrollment-segmented
```

专用检查使用固定且公开标明的合成向量，不创建真实密钥、SSH 连接或 USB 操作。
以真实 cJSON/HMAC 和仅允许 NVS 元数据的探针编译实际卡片适配代码，并执行带
sanitizer 的状态机、Mac/服务器故障测试。产物 hash 绑定应用/ELF/MAP 及既有
NVS/PHY/factory 布局；编译与模拟不等于设备验收。
保留的正常应用还须通过仓库常规检查。

## 动作时批准与安全输入

批准须明确唯一 USB 指纹与出厂 MAC、既有公开设备 ID、审核过的临时 BIN/ELF
hash、恢复 R6 的 hash、精确服务器/源码 hash、预期登记所有者、可选 relay-only
设备记录，以及仅操作员可访问的恢复目录。
先核对既有布局，再仅向 `0x10000` 写审核应用，避开 NVS/PHY 擦除扇区。
临时固件与正常 R6 是不同产物，临时刷写必须纳入批准。
维持已观察的 Secure Boot / Flash Encryption 状态；本工具不会让未加密设备获得
硬件保护。

审核 JSON 是本地操作员门禁，并非签名授权服务。保留 mode 600，仅在用户明确
授权后把 `approval` 标记为 `approved`。精确字段由 [`host.py`](host.py) 的
`check_approval()` 强制校验。使用操作员拥有的既有 mode 700 目录。
把审核过的 [`server.cjs`](server.cjs) 放到批准后端的忽略目录
`build/pocket-enrollment`；独立 CLI 不要求应用重启。保留其他文件并核验 SHA。

```sh
python -B host.py apply --approval /private/approved.json --port /dev/cu.usbmodem101
```

该命令须在用户自己的安全 Mac Terminal 中执行。
新恢复口令仅通过 `getpass` 输入，不发到聊天、argv、环境变量或 Codex 工具输入。
只有明确批准、唯一设备校验和安全终端输入通过后才生成 32 字节匹配密钥。
通过 scrypt 与 AES-GCM 加密的恢复包会在向卡片发送密钥前，以独占文件写入并执行
文件/目录 fsync，AAD 绑定公开身份与操作。恢复包只包含本次新密钥，不含旧设备
秘密。口令与恢复包分开保管。Python/JS 不可变对象及系统内存无法保证完全清除；
可变密钥缓冲区和卡片输入缓冲区会清零，秘密不会有意进入日志、命令参数或明文文件。

公开操作批准通过既有严格 known-host SSH 传输；取得有效设备写入回执后，新密钥
通过加密 SSH stdin 进入参数化 SQL。不导出存量秘密，不提取 cookie 或账号 token。
账号必须另行通过 App/小程序的正常登录流程登录。

## 中断边界

| 中断 | 结果及允许恢复 |
| --- | --- |
| 恢复包持久化失败 | 不 arm、不写 NVS；先修复存储问题。 |
| 物理确认或身份校验失败 | 不写密钥；检查拒绝原因与目标。 |
| set/commit/USB ACK 丢失 | 设备写入未确认；保留恢复包，拒绝覆盖。 |
| 有效设备回执无法持久化 | 保留恢复包，不自动登记服务器。 |
| 已有有效回执、服务器不可用 | 以相同恢复包/密钥仅重试服务器。 |
| DB commit/响应丢失 | 同键服务器重试判定已提交/缺失，不轮换。 |
| 未知既有密钥或所有者变化 | 停止；不覆盖、不重置所有者或存档。 |

仅当有效设备回执已持久化时，用户可在安全终端解密新恢复包并执行：

```sh
python -B host.py resume-server --approval /private/approved.json \
  --capsule /private/operation.capsule.json
```

重试不打开或写卡片。若在 NVS 写入之后、有效回执持久化之前断电，本工具不会仅凭
密钥长度推定成功，也不会重写密钥。恢复审核过的正常 R6 应用，停止登记，另行监督
使用其正常签名 relay 协议核对。仅有服务器行不能证明实体卡密钥匹配；本版不提供
设备 ACK 丢失后的自动核对功能。

恢复 R6 后，使用指定账号正常登录、BLE 配对、卡片账号确认、二维码退出和断线重连/
存档检查，不把 Local 调试绕过当作激活验收。
启用 v2 并重启后端属于独立可逆配置/版本步骤；资源门禁、信任根与分区迁移不在本工具
范围内。
