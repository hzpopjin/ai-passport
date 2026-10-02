**简体中文** · [English](README.md)

# 随机舞蹈 AI Passport 后端集成

此目录是为现有 `apps.randomdance.cn` 后端准备的实现，**尚未应用到生产环境**。

- `passport.patch` 新增 NestJS Passport 模块、设备认证、五分钟二维码认领与卡片端再次确认、账号下的主题选择，以及 NFC 信息卡公开／私密状态。Passport API 日志只保留元数据，避免记录设备凭证和绑定码。
- `official-themes/` 包含目录及三份带 SHA-256 校验的 16 字节主题包；主题只选择固件内已有素材，不含可执行固件。后端从 `PASSPORT_THEME_DIR` 读取，默认路径为 `/var/lib/random-dance/passport-themes`。
- 补丁包含 `backend/scripts/migrations/passport-devices.sql`，只新建 `passport_devices` 和 `passport_pairings`。执行前应检查现有数据库结构并记录回滚点；本机构建不会执行迁移。

## 审查与发布准备

把补丁应用到与已检查基线一致的后端检出，再核对生产环境期间发生的其他改动。运行 `tsc --noEmit`、后端测试和后端自己的发布门禁。正式重建或重启前，按该后端 AGENTS 要求更新 package 版本、`docs/api.md` 及相关双语发布文档，并添加 changelog。还要在入口处限制设备注册频率，设置 `PASSPORT_THEME_DIR` 权限，确认 Passport 路径不会把凭证或绑定网址写入访问日志。不要把生产 `.env` 或数据库内容复制到本仓库。

网站需要为 `https://ai-passport.randomdance.cn/sso/callback` 注册独立的 WordPress SSO 客户端。将 `PASSPORT_SSO_CLIENT_ID` 和 `PASSPORT_SSO_CLIENT_SECRET` 放入网站服务私有的 `shared/passport-sso.env`（权限 0600，`www` 服务账号可读），并检查登录、退出和会话撤销。网站包、后端模块、迁移和官方主题应协同发布。本补丁不修改生产环境。

## NFC

设备注册后，用后端分配的 `card_url` 通过 `tools/h2h/nfc_ndef.py` 生成 NTAG213 NDEF 用户区镜像。实体标签是被动式的，需要外部写卡器写入。稳定网址不含个人资料或 token。账号所有者决定是否公开昵称、头像与已应用主题；默认不公开。写入前先检查现有标签内容及手机碰触表现。
